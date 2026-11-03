#!/usr/bin/env python3
"""Check numbered-pattern tile assets (T-B1-06).

Owner: Jolyn (Buddy 1), task T-B1-06. Implemented by Ahsanul (B4) for
Jolyn, written 29 Oct 2026 in Jia Jun's (B5) session.

Each tile asset must be 600 frames x 96 B, carry its manifest CRC32, and in
every frame decode to its own Node ID and the source frame of that tick
(numbered_pattern.py encoding, after the PC scaling of 255 to the cap).
"""

from __future__ import annotations

import os
import sys
import zlib
from typing import List

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                ".."))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asset_builder as ab  # noqa: E402
import numbered_pattern as npat  # noqa: E402
import proto_layout as pl  # noqa: E402

#: Frames per asset: 10.0 s at 60 fps.
FRAMES = 600


def decode_frame(frame: bytes, cap: int) -> tuple:
    """(Node ID, frame number) of one packed frame."""
    values = pl.unpack_frame(frame)
    red = [values[led * 3] for led in range(ab.TILE * ab.TILE)]
    green = [values[led * 3 + 1] for led in range(ab.TILE * ab.TILE)]
    node = sum(1 for led in range(pl.NODE_MAX) if red[led] == cap)
    number = sum(1 << bit for bit in range(npat.FRAME_BITS)
                 if green[npat.FRAME_LED + bit] == cap)
    return node, number


def check_asset(asset: ab.TileAsset,
                cap: int = ab.BRIGHTNESS_MAX) -> List[str]:
    """Problems found in one tile asset; empty when it passes."""
    problems = []
    if asset.frame_count != FRAMES:
        problems.append("node %d: %d frames" % (asset.node_id,
                                                asset.frame_count))
    if len(asset.payload) != FRAMES * pl.FRAME_BYTES:
        problems.append("node %d: %d B" % (asset.node_id, len(asset.payload)))
    if zlib.crc32(asset.payload) != asset.crc32:
        problems.append("node %d: CRC32 mismatch" % asset.node_id)
    for tick in range(asset.frame_count):
        frame = asset.payload[tick * pl.FRAME_BYTES:
                              (tick + 1) * pl.FRAME_BYTES]
        node, number = decode_frame(frame, cap)
        if (node, number) != (asset.node_id, tick // npat.TICKS_PER_SOURCE):
            problems.append("node %d tick %d decodes as node %d frame %d" % (
                asset.node_id, tick, node, number))
            break
    return problems


def main() -> int:
    """Build and check the 1x1, 2x2 and 3x3 patterns: 14 tile assets."""
    passed = total = 0
    for layout, node_map in sorted(npat.DEFAULT_MAPS.items()):
        frames, delays = npat.make_frames(layout, node_map)
        assets = ab.build_assets(frames, delays, layout, node_map)
        for node, asset in sorted(assets.items()):
            problems = check_asset(asset)
            total += 1
            passed += 0 if problems else 1
            print("%s node %d: %s" % (layout, node,
                                      "; ".join(problems) or "PASS"))
    print("Pass = %d of %d" % (passed, total))
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())
