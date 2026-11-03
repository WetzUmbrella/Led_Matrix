"""pytest: pc/uploader.py (T-B1-04) against a fake tile on loopback.

Owner: Jolyn (Buddy 1). Implemented by Ahsanul (B4) for Jolyn. The fake
tile follows the tile rules of panel_sync/main/asset_rx.c: ACK every chunk
of the open session, repeat ACKs for repeats, NACK reason 3 when its
4-slot queue would overflow (optional), BUSY when playing.
"""

from __future__ import annotations

import os
import socket
import sys
import threading
import zlib
from typing import Dict, List, Optional

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import proto_layout as pl  # noqa: E402
import uploader as up  # noqa: E402


class FakeTile(threading.Thread):
    """Minimal tile on 127.0.0.1:<port>."""

    def __init__(self, busy: bool = False, nack_every: int = 0) -> None:
        super().__init__(daemon=True)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.settimeout(0.05)
        self.port = self.sock.getsockname()[1]
        self.busy = busy
        self.nack_every = nack_every
        self.chunks: Dict[int, bytes] = {}
        self.manifest: Optional[dict] = None
        self.received = 0
        self.running = True

    def run(self) -> None:
        while self.running:
            try:
                data, addr = self.sock.recvfrom(2048)
            except socket.timeout:
                continue
            msg = pl.decode(data)
            if self.busy:
                self.sock.sendto(pl.encode_busy(
                    session_id=msg["session_id"], seq=1, node_id=4,
                    ref_seq=msg["seq"]), addr)
                continue
            if msg["type"] == "MANIFEST":
                self.manifest = msg
                self.chunks = {}
                continue
            self.received += 1
            if self.nack_every and self.received % self.nack_every == 0:
                self.sock.sendto(pl.encode_nack(
                    session_id=msg["session_id"], seq=1, node_id=4, reason=3,
                    ref_seq=msg["seq"]), addr)
                continue
            self.chunks[int(msg["chunk_index"])] = msg["payload"]
            self.sock.sendto(pl.encode_chunk_ack(
                session_id=msg["session_id"], seq=1, node_id=4,
                chunk_index=msg["chunk_index"]), addr)

    def payload(self) -> bytes:
        return b"".join(self.chunks[i] for i in sorted(self.chunks))

    def stop(self) -> None:
        self.running = False
        self.join()
        self.sock.close()


@pytest.fixture
def asset() -> bytes:
    return bytes((i * 7) & 0xFF for i in range(57600))


def _upload(tile: FakeTile, payload: bytes,
            drop: List[int] = ()):  # type: ignore[assignment]
    uploader = up.Uploader(rate=0, settle_s=0.01, ack_timeout_s=0.05)
    up.TILE_PORT = tile.port
    stats = uploader.upload_asset("127.0.0.1", 5, payload, drop)
    uploader.sock.close()
    return stats


def test_57_chunks_last_256() -> None:
    assert up.chunk_count(57600) == 57
    assert len(up.chunk_bytes(bytes(57600), 56)) == 256


def test_ten_uploads_all_acked_crc_equal(asset: bytes) -> None:
    for _ in range(10):
        tile = FakeTile()
        tile.start()
        stats = _upload(tile, asset)
        tile.stop()
        assert len(stats.acked) == 57
        assert tile.manifest["crc32"] == zlib.crc32(asset)
        assert zlib.crc32(tile.payload()) == tile.manifest["crc32"]


def test_six_dropped_chunks_resent_exactly_six(asset: bytes) -> None:
    tile = FakeTile()
    tile.start()
    drops = up.pick_drops(57, 6)
    stats = _upload(tile, asset, drops)
    tile.stop()
    assert len(stats.acked) == 57
    assert stats.resent == 6


def test_queue_full_nack_chunks_are_retried(asset: bytes) -> None:
    tile = FakeTile(nack_every=5)
    tile.start()
    stats = _upload(tile, asset)
    tile.stop()
    assert len(stats.acked) == 57
    assert stats.queue_full_nacks > 0


def test_busy_stops_the_upload(asset: bytes) -> None:
    tile = FakeTile(busy=True)
    tile.start()
    with pytest.raises(up.UploadError, match="BUSY"):
        _upload(tile, asset)
    tile.stop()


def test_count_busy_57_of_57(asset: bytes) -> None:
    tile = FakeTile(busy=True)
    tile.start()
    up.TILE_PORT = tile.port
    uploader = up.Uploader(rate=0)
    assert uploader.count_busy("127.0.0.1", 5, asset, wait_s=0.5) == 57
    uploader.sock.close()
    tile.stop()


def test_rate_limiter_spaces_sends() -> None:
    now = [0.0]
    slept: List[float] = []

    def sleep(seconds: float) -> None:
        slept.append(seconds)
        now[0] += seconds

    limiter = up.RateLimiter(100.0, clock=lambda: now[0], sleep=sleep)
    for _ in range(5):
        limiter.wait()
    assert slept == pytest.approx([0.01] * 4)
