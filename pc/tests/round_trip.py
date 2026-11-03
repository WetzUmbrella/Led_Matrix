#!/usr/bin/env python3
"""PC to tile round trip of a packed asset (T-X-01).

Owner: Jolyn (Buddy 1), task T-X-01 (support Cheston, Jia Jun). Implemented
by Ahsanul (B4) for Jolyn, written 30 Oct 2026 in Jia Jun's (B5) session.

Plan row: "Known pattern (every channel value 0 to 255 present across the
frames) is packed on the PC, uploaded, stored, unpacked and dumped from the
back buffer for all 600 frames, then compared with the PC reference."
Pass: "192 of 192 bytes equal in each of 600 frames; every byte in 0 to 8
(catches double scaling)."

Two steps:
  build  writes the 600-frame pattern asset (one tile, no GIF needed):
         python3 pc/tests/round_trip.py build --out round_trip.bin
  check  compares the DE_DUMP lines of a tile log from the DE_DUMP_FRAMES
         build (display_engine.c) with the PC reference:
         python3 pc/tests/round_trip.py check tile.log

Reference: the PC packs round(v x 8 / 255); the tile writes each nibble into
the wire buffer in G, R, B order (display_frame.c), orientation 0.
"""

from __future__ import annotations

import argparse
import os
import sys
from typing import Dict, List, Tuple

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                ".."))
import asset_builder as ab  # noqa: E402
import proto_layout as pl  # noqa: E402

#: Frames in the asset.
FRAMES = 600

#: Bytes in one wire buffer.
WIRE_BYTES = 192


def pattern_frame(frame: int) -> ab.Pixels:
    """Frame ``frame`` of the pattern: channel k of the frame holds
    (frame x 192 + k) mod 256, so 0 to 255 all appear within 2 frames."""
    values = [(frame * WIRE_BYTES + k) % 256 for k in range(WIRE_BYTES)]
    return [(values[3 * p], values[3 * p + 1], values[3 * p + 2])
            for p in range(64)]


def build_asset() -> ab.TileAsset:
    """The 600-frame round-trip asset for Node ID 1."""
    return ab.build_tile(1, [pattern_frame(f) for f in range(FRAMES)])


def reference_wire(frame: int) -> bytes:
    """What the tile's back buffer must hold for ``frame``."""
    out = bytearray()
    for red, green, blue in pattern_frame(frame):
        out += bytes((ab.scale(green), ab.scale(red), ab.scale(blue)))
    return bytes(out)


def parse_dump(lines: List[str]) -> Dict[int, bytes]:
    """``DE_DUMP,<frame>,<hex>`` lines to frame -> wire bytes."""
    dumps: Dict[int, bytes] = {}
    for line in lines:
        line = line.strip()
        if line.startswith("DE_DUMP,"):
            _tag, frame, hexdata = line.split(",", 2)
            dumps[int(frame)] = bytes.fromhex(hexdata)
    return dumps


def check(dumps: Dict[int, bytes]) -> Tuple[int, List[str]]:
    """Frames passing, and one line per problem."""
    problems = []
    passed = 0
    for frame in range(FRAMES):
        got = dumps.get(frame)
        if got is None:
            problems.append("frame %d: no dump" % frame)
            continue
        want = reference_wire(frame)
        equal = sum(1 for a, b in zip(got, want) if a == b)
        in_range = all(byte <= pl.NIBBLE_MAX for byte in got)
        if equal == WIRE_BYTES and len(got) == WIRE_BYTES and in_range:
            passed += 1
        else:
            problems.append("frame %d: %d of %d bytes equal, all 0 to 8: %s"
                            % (frame, equal, WIRE_BYTES, in_range))
    return passed, problems


def main(argv: List[str] = None) -> int:  # type: ignore[assignment]
    """build or check."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="cmd", required=True)
    build = sub.add_parser("build")
    build.add_argument("--out", required=True)
    chk = sub.add_parser("check")
    chk.add_argument("log")
    args = parser.parse_args(argv)
    if args.cmd == "build":
        asset = build_asset()
        with open(args.out, "wb") as handle:
            handle.write(asset.payload)
        print("wrote %s: %d frames, crc32 0x%08x" % (
            args.out, asset.frame_count, asset.crc32))
        return 0
    with open(args.log, encoding="utf-8", errors="replace") as handle:
        passed, problems = check(parse_dump(handle.readlines()))
    for line in problems[:20]:
        print(line)
    print("Round trip: %d of %d frames pass" % (passed, FRAMES))
    return 0 if passed == FRAMES else 1


if __name__ == "__main__":
    sys.exit(main())
