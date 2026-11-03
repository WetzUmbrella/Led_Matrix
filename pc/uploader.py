#!/usr/bin/env python3
"""Upload one tile asset over UDP (T-B1-04).

Owner: Jolyn (Buddy 1), task T-B1-04. Implemented by Ahsanul (B4) for
Jolyn, written 29 Oct 2026 in Jia Jun's (B5) session. Replaces
``panel_sync/tools/upload_frames.sh`` (USB only).

Plan row T-B1-04: "Binary UDP chunks of 1,024 B (57 chunks, last 256 B),
per-chunk ACK, only missing chunks resent. Uploads run before PLAYING;
during PLAYING only a rejoin re-upload is allowed, and it is rate-limited at
the OD-13 rate."

Protocol (layout_table.md MANIFEST, ASSET_CHUNK, CHUNK_ACK, NACK, BUSY and
section 11): MANIFEST, then ASSET_CHUNKs, unicast to the tile's port 4214
from one socket; the tile answers each chunk with CHUNK_ACK {chunk_index}
to that socket. NACK reason 3 (Upload Queue full) leaves the chunk missing
for the next round; NACK reason 0 or 1 and BUSY stop the upload.

PROVISIONAL choices (owner Jolyn):
  * window of 4 chunks per round, the tile's Upload Queue length (OD-11);
  * 1.0 s pause after the MANIFEST while the tile erases the frames
    partition (about 16 sectors);
  * rate limit 100 chunks/s, about 100 KB/s, until OD-13 is measured in
    T-X-04;
  * at most 20 rounds before giving up.

Time-to-ready (first chunk to ASSET_READY seen in Pico STATUS) is measured by
``pc/dashboard.py`` with ``pc/pico_link.py``, because the tile sends
ASSET_READY to the Pico, not to the PC.

Usage:
    python3 pc/uploader.py --tile 192.168.1.6 assets/node_4.bin
        [--session 1] [--drop 6]
"""

from __future__ import annotations

import argparse
import os
import random
import socket
import sys
import time
import zlib
from dataclasses import dataclass, field
from typing import Callable, Iterable, List, Optional, Set

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import proto_layout as pl  # noqa: E402

#: Tile port for MANIFEST and ASSET_CHUNK (layout_table.md section 11).
TILE_PORT = 4214

#: Chunk size (LDC-8).
CHUNK = pl.CHUNK_PAYLOAD_MAX

#: Chunks in flight per round: the tile's Upload Queue length (OD-11).
WINDOW = 4

#: Wait after MANIFEST for the tile's erase, seconds (PROVISIONAL).
MANIFEST_SETTLE_S = 1.0

#: Wait for the ACKs of one window, seconds (plan choice).
ACK_TIMEOUT_S = 0.25

#: Default rate limit, chunks per second (PROVISIONAL until OD-13).
DEFAULT_RATE = 100.0

#: Rounds before the upload is abandoned (plan choice).
MAX_ROUNDS = 20

#: Packed asset format (layout_table.md section 5.4).
FORMAT_PACKED = 2

#: Seconds per frame of payload at 96 B per frame, for the manifest.
FRAME_BYTES = pl.FRAME_BYTES


class UploadError(RuntimeError):
    """The tile refused the upload (BUSY, NACK reason 0 or 1)."""


class RateLimiter:
    """At most ``rate`` sends per second, spaced evenly."""

    def __init__(self, rate: float,
                 clock: Callable[[], float] = time.monotonic,
                 sleep: Callable[[float], None] = time.sleep) -> None:
        self.period = 1.0 / rate if rate > 0 else 0.0
        self._clock = clock
        self._sleep = sleep
        self._next = 0.0

    def wait(self) -> None:
        """Block until the next send is allowed."""
        now = self._clock()
        if now < self._next:
            self._sleep(self._next - now)
            now = self._next
        self._next = now + self.period


@dataclass
class UploadStats:
    """What one upload did."""

    chunks: int = 0
    acked: Set[int] = field(default_factory=set)
    sent: int = 0
    resent: int = 0
    queue_full_nacks: int = 0
    rounds: int = 0
    seconds: float = 0.0


def chunk_count(payload_len: int) -> int:
    """ceil(len / 1,024): 57 for 57,600 B."""
    return -(-payload_len // CHUNK)


def chunk_bytes(payload: bytes, index: int) -> bytes:
    """Payload of chunk ``index``: 1,024 B, the last one the remainder."""
    return payload[index * CHUNK:(index + 1) * CHUNK]


class Uploader:
    """One socket that uploads assets to tiles and reads their replies."""

    def __init__(self, sock: Optional[socket.socket] = None,
                 rate: float = DEFAULT_RATE,
                 settle_s: float = MANIFEST_SETTLE_S,
                 ack_timeout_s: float = ACK_TIMEOUT_S) -> None:
        self.sock = sock or socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.limiter = RateLimiter(rate)
        self.settle_s = settle_s
        self.ack_timeout_s = ack_timeout_s
        self.seq = 0

    def _next_seq(self) -> int:
        self.seq = (self.seq + 1) & 0xFFFF
        return self.seq

    def _read_replies(self, deadline: float, session_id: int,
                      stats: UploadStats) -> None:
        """Collect CHUNK_ACK, NACK and BUSY until ``deadline``."""
        while True:
            left = deadline - time.monotonic()
            if left <= 0:
                return
            self.sock.settimeout(left)
            try:
                data, _addr = self.sock.recvfrom(64)
            except socket.timeout:
                return
            try:
                msg = pl.decode(data)
            except pl.LayoutError:
                continue
            kind = msg["type"]
            if kind == "CHUNK_ACK" and msg["session_id"] == session_id:
                stats.acked.add(int(msg["chunk_index"]))  # type: ignore
            elif kind == "BUSY":
                raise UploadError("tile is playing (BUSY)")
            elif kind == "NACK":
                reason = int(msg["reason"])  # type: ignore[call-overload]
                if reason == 3:
                    stats.queue_full_nacks += 1
                else:
                    raise UploadError("tile refused the asset, NACK reason "
                                      "%d" % reason)

    def upload_asset(self, tile_ip: str, session_id: int, payload: bytes,
                     drop: Iterable[int] = ()) -> UploadStats:
        """MANIFEST, then chunks until all are ACKed.

        ``drop`` is the T-B1-04 test hook: the first send of each listed
        chunk is skipped, as if lost, so it must be resent.
        """
        stats = UploadStats(chunks=chunk_count(len(payload)))
        to_drop = set(drop)
        manifest = pl.encode_manifest(
            session_id=session_id, seq=self._next_seq(),
            format_version=FORMAT_PACKED,
            frame_count=len(payload) // FRAME_BYTES,
            crc32=zlib.crc32(payload))
        start = time.monotonic()
        self.sock.sendto(manifest, (tile_ip, TILE_PORT))
        self._read_replies(time.monotonic() + self.settle_s, session_id,
                           stats)
        self.retry_missing(tile_ip, session_id, payload, stats, to_drop)
        stats.seconds = time.monotonic() - start
        return stats

    def retry_missing(self, tile_ip: str, session_id: int, payload: bytes,
                      stats: UploadStats, to_drop: Set[int]) -> None:
        """Send every chunk not yet ACKed, a window at a time, until all
        are ACKed or MAX_ROUNDS pass."""
        tried: Set[int] = set()
        while len(stats.acked) < stats.chunks:
            stats.rounds += 1
            if stats.rounds > MAX_ROUNDS:
                raise UploadError("%d of %d chunks ACKed after %d rounds" % (
                    len(stats.acked), stats.chunks, MAX_ROUNDS))
            missing = [i for i in range(stats.chunks) if i not in stats.acked]
            for first in range(0, len(missing), WINDOW):
                for index in missing[first:first + WINDOW]:
                    if index in tried:
                        stats.resent += 1
                    tried.add(index)
                    if index in to_drop:
                        to_drop.discard(index)  # Lost once (test hook).
                        continue
                    self.limiter.wait()
                    data = pl.encode_asset_chunk(
                        session_id=session_id, seq=self._next_seq(),
                        chunk_index=index,
                        payload=chunk_bytes(payload, index))
                    self.sock.sendto(data, (tile_ip, TILE_PORT))
                    stats.sent += 1
                self._read_replies(time.monotonic() + self.ack_timeout_s,
                                   session_id, stats)

    def count_busy(self, tile_ip: str, session_id: int, payload: bytes,
                   wait_s: float = 2.0) -> int:
        """T-B3-04 acceptance (1): send a whole re-upload (MANIFEST and
        every chunk once) to a tile that is PLAYING and count the BUSY
        replies. Every chunk should be refused: 57 of 57 for 57,600 B."""
        chunks = chunk_count(len(payload))
        self.sock.sendto(pl.encode_manifest(
            session_id=session_id, seq=self._next_seq(),
            format_version=FORMAT_PACKED,
            frame_count=len(payload) // FRAME_BYTES,
            crc32=zlib.crc32(payload)), (tile_ip, TILE_PORT))
        busy_seqs: Set[int] = set()
        chunk_seqs: Set[int] = set()
        for index in range(chunks):
            self.limiter.wait()
            seq = self._next_seq()
            chunk_seqs.add(seq)
            self.sock.sendto(pl.encode_asset_chunk(
                session_id=session_id, seq=seq, chunk_index=index,
                payload=chunk_bytes(payload, index)), (tile_ip, TILE_PORT))
        deadline = time.monotonic() + wait_s
        while time.monotonic() < deadline:
            self.sock.settimeout(max(deadline - time.monotonic(), 0.001))
            try:
                data, _addr = self.sock.recvfrom(64)
            except socket.timeout:
                break
            try:
                msg = pl.decode(data)
            except pl.LayoutError:
                continue
            if msg["type"] == "BUSY" and msg["ref_seq"] in chunk_seqs:
                busy_seqs.add(int(msg["ref_seq"]))  # type: ignore
        return len(busy_seqs)


def pick_drops(chunks: int, count: int, seed: int = 1) -> List[int]:
    """``count`` distinct chunk indices for the drop test hook."""
    return sorted(random.Random(seed).sample(range(chunks), count))


def main(argv: Optional[List[str]] = None) -> int:
    """Command line entry point."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("asset", help="node_<n>.bin from asset_builder")
    parser.add_argument("--tile", required=True, help="tile IP address")
    parser.add_argument("--session", type=int, default=1)
    parser.add_argument("--rate", type=float, default=DEFAULT_RATE)
    parser.add_argument("--drop", type=int, default=0,
                        help="test hook: lose this many chunks once")
    parser.add_argument("--expect-busy", action="store_true",
                        help="T-B3-04: re-upload during PLAYING and count "
                             "BUSY replies")
    args = parser.parse_args(argv)
    with open(args.asset, "rb") as handle:
        payload = handle.read()
    uploader = Uploader(rate=args.rate)
    if args.expect_busy:
        busy = uploader.count_busy(args.tile, args.session, payload)
        chunks = chunk_count(len(payload))
        print("BUSY %d of %d chunks refused" % (busy, chunks))
        return 0 if busy == chunks else 1
    drops = pick_drops(chunk_count(len(payload)), args.drop)
    try:
        stats = uploader.upload_asset(args.tile, args.session, payload, drops)
    except UploadError as err:
        print("uploader: %s" % err, file=sys.stderr)
        return 1
    print("UPLOAD tile=%s chunks=%d acked=%d sent=%d resent=%d dropped=%s "
          "queue_full_nacks=%d rounds=%d seconds=%.3f crc32=0x%08x" % (
              args.tile, stats.chunks, len(stats.acked), stats.sent,
              stats.resent, drops, stats.queue_full_nacks, stats.rounds,
              stats.seconds, zlib.crc32(payload)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
