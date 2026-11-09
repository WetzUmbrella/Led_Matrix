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

#: Packed asset format and the OD-15 8-bit format (section 5.4).
FORMAT_PACKED = 2
FORMAT_8BIT = 1

#: Bytes per 8-bit frame (DE_PROFILE build only).
FRAME_8BIT_BYTES = 192

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
    #: True when the tile already held this asset: it answered the MANIFEST
    #: with ASSET_READY to the Pico and sent no CHUNK_ACK (bench 9 Nov).
    already_ready: bool = False
    #: Replies dropped because they answer an earlier upload (stale BUSY or
    #: NACK left in the socket) rather than a packet of this one.
    stale_replies: int = 0
    #: Seq of every MANIFEST and ASSET_CHUNK this upload sent.
    sent_seqs: Set[int] = field(default_factory=set)


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

    def _drain(self) -> int:
        """Discard every datagram already waiting on the socket.

        A BUSY or NACK from an earlier upload would otherwise be read as the
        answer to this one (bench 9 Nov: one BUSY run made every later
        ``upload`` fail at once). Returns how many were discarded.
        """
        dropped = 0
        self.sock.settimeout(0.0)
        while True:
            try:
                self.sock.recvfrom(64)
            except (BlockingIOError, socket.timeout):
                return dropped
            except OSError:
                # Windows reports an earlier ICMP port unreachable here.
                return dropped
            dropped += 1

    def _send(self, data: bytes, seq: int, tile_ip: str,
              stats: UploadStats) -> None:
        """Send one MANIFEST or ASSET_CHUNK and remember its seq."""
        stats.sent_seqs.add(seq)
        self.sock.sendto(data, (tile_ip, TILE_PORT))

    def _read_replies(self, deadline: float, session_id: int,
                      stats: UploadStats) -> None:
        """Collect CHUNK_ACK, NACK and BUSY until ``deadline``.

        BUSY and NACK carry the seq they answer (``ref_seq``); one that does
        not answer a packet of this upload is stale and only counted.
        """
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
            if kind in ("BUSY", "NACK") and \
                    int(msg["ref_seq"]) not in stats.sent_seqs:  # type: ignore
                stats.stale_replies += 1
                continue
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
                     drop: Iterable[int] = (),
                     manifest_crc: Optional[int] = None,
                     format_version: int = FORMAT_PACKED,
                     already_ready: Optional[Callable[[], bool]] = None
                     ) -> UploadStats:
        """MANIFEST, then chunks until all are ACKed.

        ``already_ready`` is asked, while no chunk has been ACKed, whether
        the Pico already counts this tile ready. A tile that already holds
        the asset answers the MANIFEST with ASSET_READY to the Pico and sends
        the PC nothing, so without it the upload ends "0 of N chunks ACKed"
        although it worked (bench 9 Nov). True ends the upload with
        ``stats.already_ready`` set.

        ``drop`` is the T-B1-04 test hook: the first send of each listed
        chunk is skipped, as if lost, so it must be resent.
        ``manifest_crc`` is the T-X-08 case 1 hook: the MANIFEST carries this
        CRC32 instead of the CRC32 of ``payload``. ``format_version`` 1 is
        the OD-15 8-bit asset (192 B frames) for the DE_PROFILE build.
        """
        stats = UploadStats(chunks=chunk_count(len(payload)))
        to_drop = set(drop)
        frame_bytes = FRAME_8BIT_BYTES if format_version == FORMAT_8BIT \
            else FRAME_BYTES
        manifest_seq = self._next_seq()
        manifest = pl.encode_manifest(
            session_id=session_id, seq=manifest_seq,
            format_version=format_version,
            frame_count=len(payload) // frame_bytes,
            crc32=zlib.crc32(payload) if manifest_crc is None
            else manifest_crc)
        start = time.monotonic()
        self._drain()
        self._send(manifest, manifest_seq, tile_ip, stats)
        self._read_replies(time.monotonic() + self.settle_s, session_id,
                           stats)
        self.retry_missing(tile_ip, session_id, payload, stats, to_drop,
                           already_ready)
        stats.seconds = time.monotonic() - start
        return stats

    def retry_missing(self, tile_ip: str, session_id: int, payload: bytes,
                      stats: UploadStats, to_drop: Set[int],
                      already_ready: Optional[Callable[[], bool]] = None
                      ) -> None:
        """Send every chunk not yet ACKed, a window at a time, until all
        are ACKed or MAX_ROUNDS pass. Stops early when nothing is ACKed and
        ``already_ready()`` says the Pico counts the tile ready."""
        tried: Set[int] = set()
        while len(stats.acked) < stats.chunks:
            if (not stats.acked) and (already_ready is not None) and \
                    already_ready():
                stats.already_ready = True
                return
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
                    seq = self._next_seq()
                    data = pl.encode_asset_chunk(
                        session_id=session_id, seq=seq,
                        chunk_index=index,
                        payload=chunk_bytes(payload, index))
                    self._send(data, seq, tile_ip, stats)
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
    parser.add_argument("--format", type=int, default=FORMAT_PACKED,
                        choices=(FORMAT_8BIT, FORMAT_PACKED),
                        help="1 = 8-bit, DE_PROFILE build only (OD-15)")
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
        stats = uploader.upload_asset(args.tile, args.session, payload, drops,
                                      format_version=args.format)
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
