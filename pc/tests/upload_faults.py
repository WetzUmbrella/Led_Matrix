#!/usr/bin/env python3
"""Upload fault cases against a real tile (T-X-08).

Owner: Cheston (Buddy 3), task T-X-08 (support Jolyn, Benjamin). Implemented
by Ahsanul (B4) for Cheston, written 30 Oct 2026 in Jia Jun's (B5) session.

Plan row, five cases: "(1) one corrupted chunk byte: CRC fails, retry,
ASSET_READY only after a valid asset; (2) WiFi interrupted mid-upload: only
missing chunks resent; (3) manifest with another session_id: rejected;
(4) unknown format_version vector: NACK, ASSET_FORMAT, no ASSET_READY;
(5) 115,200 B payload: NACK, no ASSET_READY. In (3) to (5) the Pico
readiness bit stays 0 and the dashboard keeps Start disabled."

What the tool checks from the PC side, and what the operator reads from the
tile log, the Pico STATUS (``dashboard.py`` ``status``) and the telemetry:
  1. all chunks ACKed with a manifest CRC that does not match; tile log
     ``ASSET verify failed``, CRC_FAIL in TELEMETRY; then a clean upload,
     and only then ``tile_net: ASSET_READY``.
  2. the first half of the chunks, a pause (switch the router's WiFi off and
     on during it), then the rest: ``resent`` counts only chunks that were
     not ACKed before the pause.
  3. a manifest and chunks with session_id + 1: 0 CHUNK_ACK, tile
     ``upload_wrong_session`` rises.
  4. manifests with format_version 0 and 1: NACK reason 0 for both on the
     shipped build (1 is accepted only by the DE_PROFILE build, T-B5-02);
     ASSET_FORMAT set.
  5. ``protocol/vectors/payload_115200.bin``: NACK reason 1 at the manifest.

Usage:
    python3 pc/tests/upload_faults.py --tile <ip> --session <id>
        --asset assets_2x2/node_1.bin [--cases 1,2,3,4,5] [--pause 20]
"""

from __future__ import annotations

import argparse
import os
import socket
import sys
import time
import zlib
from typing import Dict, List, Optional

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                ".."))
import proto_layout as pl  # noqa: E402
import uploader as up  # noqa: E402

VECTORS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..",
                       "..", "protocol", "vectors")

#: Chunk whose byte case 1 corrupts (plan choice).
CORRUPT_CHUNK = 10

#: Seconds to listen for replies in cases 3 to 5 (plan choice).
LISTEN_S = 2.0


def corrupted(payload: bytes, chunk: int = CORRUPT_CHUNK) -> bytes:
    """``payload`` with one byte of ``chunk`` flipped."""
    data = bytearray(payload)
    data[chunk * up.CHUNK] ^= 0xFF
    return bytes(data)


def listen(sock: socket.socket, seconds: float) -> List[Dict[str, object]]:
    """Every decodable reply within ``seconds``."""
    replies = []
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        sock.settimeout(max(deadline - time.monotonic(), 0.001))
        try:
            data, _addr = sock.recvfrom(64)
        except socket.timeout:
            break
        try:
            replies.append(pl.decode(data))
        except pl.LayoutError:
            continue
    return replies


def nack_reasons(replies: List[Dict[str, object]]) -> List[int]:
    """Reasons of the NACKs among the replies."""
    return [int(r["reason"]) for r in replies  # type: ignore[call-overload]
            if r["type"] == "NACK"]


def case1(u: up.Uploader, tile: str, session: int, payload: bytes) -> str:
    """Corrupted chunk, then a clean upload."""
    bad = u.upload_asset(tile, session, corrupted(payload),
                         manifest_crc=zlib.crc32(payload))
    time.sleep(LISTEN_S)  # The tile verifies and finds the mismatch.
    good = u.upload_asset(tile, session, payload)
    return ("corrupted upload %d of %d ACKed, then clean %d of %d ACKed; "
            "check the tile log: 'ASSET verify failed' before the only "
            "'tile_net: ASSET_READY'" % (len(bad.acked), bad.chunks,
                                         len(good.acked), good.chunks))


def case2(u: up.Uploader, tile: str, session: int, payload: bytes,
          pause_s: float) -> str:
    """Half the chunks, a pause for the WiFi cut, then only the rest."""
    stats = up.UploadStats(chunks=up.chunk_count(len(payload)))
    u.sock.sendto(pl.encode_manifest(
        session_id=session, seq=u._next_seq(),
        format_version=up.FORMAT_PACKED,
        frame_count=len(payload) // up.FRAME_BYTES,
        crc32=zlib.crc32(payload)), (tile, up.TILE_PORT))
    time.sleep(u.settle_s)
    half = stats.chunks // 2
    for index in range(half):
        u.limiter.wait()
        u.sock.sendto(pl.encode_asset_chunk(
            session_id=session, seq=u._next_seq(), chunk_index=index,
            payload=up.chunk_bytes(payload, index)), (tile, up.TILE_PORT))
        stats.sent += 1
    u._read_replies(time.monotonic() + 0.5, session, stats)
    acked_before = set(stats.acked)
    print("case 2: %d chunks ACKed; cut the router WiFi now, restore it "
          "within %.0f s" % (len(acked_before), pause_s))
    time.sleep(pause_s)
    sent_before = stats.sent
    u.retry_missing(tile, session, payload, stats, set())
    after = stats.sent - sent_before
    missing = stats.chunks - len(acked_before)
    # "Only missing chunks resent": with no further loss, every chunk sent
    # after the pause was missing before it. A loss after the pause adds
    # sends, so a larger count is reported as CHECK, not FAIL.
    return ("%d of %d ACKed; %d sent after the pause for %d missing: %s" % (
        len(stats.acked), stats.chunks, after, missing,
        "PASS" if after == missing else "CHECK"))


def case3(u: up.Uploader, tile: str, session: int, payload: bytes) -> str:
    """Manifest and chunks for another session."""
    other = (session + 1) & 0xFF
    u.sock.sendto(pl.encode_manifest(
        session_id=other, seq=u._next_seq(), format_version=up.FORMAT_PACKED,
        frame_count=len(payload) // up.FRAME_BYTES,
        crc32=zlib.crc32(payload)), (tile, up.TILE_PORT))
    for index in range(3):
        u.sock.sendto(pl.encode_asset_chunk(
            session_id=other, seq=u._next_seq(), chunk_index=index,
            payload=up.chunk_bytes(payload, index)), (tile, up.TILE_PORT))
    acks = [r for r in listen(u.sock, LISTEN_S) if r["type"] == "CHUNK_ACK"]
    return "%d CHUNK_ACK for session %d: %s" % (
        len(acks), other, "PASS" if not acks else "FAIL")


def case4(u: up.Uploader, tile: str, session: int) -> str:
    """Manifests with format_version 0 (unknown, T-X-08) and 1 (8-bit,
    refused by the shipped build, T-B5-02 and VC-5)."""
    results = []
    for version in (0, 1):
        u.sock.sendto(pl.encode_manifest(
            session_id=session, seq=u._next_seq(), format_version=version,
            frame_count=600, crc32=0x11223344), (tile, up.TILE_PORT))
        reasons = nack_reasons(listen(u.sock, LISTEN_S))
        results.append("format %d: NACK %s" % (version, reasons))
    ok = all(r.endswith("[0]") for r in results)
    return "%s (want [0] for both on the shipped build): %s" % (
        "; ".join(results), "PASS" if ok else "FAIL")


def case5(u: up.Uploader, tile: str, session: int) -> str:
    """The 115,200 B VC-5 payload."""
    with open(os.path.join(VECTORS, "payload_115200.bin"), "rb") as handle:
        payload = handle.read()
    try:
        u.upload_asset(tile, session, payload)
    except up.UploadError as err:
        ok = "reason 1" in str(err)
        return "%s: %s" % (err, "PASS" if ok else "FAIL")
    return "no NACK: FAIL"


def main(argv: Optional[List[str]] = None) -> int:
    """Run the selected cases."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--tile", required=True)
    parser.add_argument("--session", type=int, required=True,
                        help="the session_id the Pico's beacons carry")
    parser.add_argument("--asset", required=True)
    parser.add_argument("--cases", default="1,2,3,4,5")
    parser.add_argument("--pause", type=float, default=20.0)
    args = parser.parse_args(argv)
    with open(args.asset, "rb") as handle:
        payload = handle.read()
    u = up.Uploader()
    for case in [int(c) for c in args.cases.split(",")]:
        if case == 1:
            result = case1(u, args.tile, args.session, payload)
        elif case == 2:
            result = case2(u, args.tile, args.session, payload, args.pause)
        elif case == 3:
            result = case3(u, args.tile, args.session, payload)
        elif case == 4:
            result = case4(u, args.tile, args.session)
        else:
            result = case5(u, args.tile, args.session)
        print("CASE %d: %s" % (case, result))
    return 0


if __name__ == "__main__":
    sys.exit(main())
