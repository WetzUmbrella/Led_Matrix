#!/usr/bin/env python3
"""Numbered test pattern (T-B1-06): each tile region shows its Node ID and
the frame number, so assets can be checked on the PC and photographed on
the tiles.

Owner: Jolyn (Buddy 1), task T-B1-06. Implemented by Ahsanul (B4) for
Jolyn, written 29 Oct 2026 in Jia Jun's (B5) session.

Region encoding (plan choice), per 8x8 tile region, LED = 8 x row + column:
  * Node ID: LEDs 0 to (Node ID - 1) red at 255 (1 to 9 LEDs from the top
    left), easy to count on a photo;
  * frame number: LEDs 48 to 57 green at 255 for the set bits of the source
    frame number, bit 0 first (10 bits, frame numbers up to 1023).
The GIF has SOURCE_FRAMES frames of SOURCE_DELAY_MS, so 10.0 s and 600
ticks at 60 fps; tick k shows source frame k // 6.

Usage:
    python3 pc/tests/numbered_pattern.py --layout 2x2 --out numbered_2x2.gif
"""

from __future__ import annotations

import argparse
import os
import sys
from typing import List, Sequence, Tuple

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                ".."))
import asset_builder as ab  # noqa: E402

#: Source frames and their delay: 100 x 100 ms = 10.0 s (600 ticks).
SOURCE_FRAMES = 100
SOURCE_DELAY_MS = 100

#: Ticks per source frame: 100 ms x 60 / 1000.
TICKS_PER_SOURCE = 6

#: First LED of the frame-number bits and how many bits.
FRAME_LED = 48
FRAME_BITS = 10

#: Full channel value before scaling.
FULL = 255

#: Default map per layout (positions row by row over rows x cols).
DEFAULT_MAPS = {"1x1": [1, 0, 0, 0, 0, 0, 0, 0, 0],
                "2x2": [1, 2, 3, 4, 0, 0, 0, 0, 0],
                "3x3": [1, 2, 3, 4, 5, 6, 7, 8, 9]}

#: Strip layouts (build step 6). 1x9 is the strip GIF for the demo.
STRIP_MAPS = {"1x5": [1, 2, 3, 4, 5, 0, 0, 0, 0],
              "1x9": [1, 2, 3, 4, 5, 6, 7, 8, 9]}


def region_pixels(node_id: int, frame: int) -> List[Tuple[int, int, int]]:
    """The 64 pixels of one tile region."""
    pixels = [(0, 0, 0)] * (ab.TILE * ab.TILE)
    for led in range(node_id):
        pixels[led] = (FULL, 0, 0)
    for bit in range(FRAME_BITS):
        if frame & (1 << bit):
            pixels[FRAME_LED + bit] = (0, FULL, 0)
    return pixels


def make_frames(layout: str, node_map: Sequence[int]):  # type: ignore
    """PIL frames and delays of the pattern for a layout."""
    from PIL import Image
    rows, cols = ab.parse_layout(layout, node_map)
    frames = []
    for frame in range(SOURCE_FRAMES):
        wall = Image.new("RGB", (cols * ab.TILE, rows * ab.TILE))
        for slot, node in enumerate(node_map):
            if not node:
                continue
            row, col = divmod(slot, cols)
            for led, pixel in enumerate(region_pixels(node, frame)):
                y, x = divmod(led, ab.TILE)
                wall.putpixel((col * ab.TILE + x, row * ab.TILE + y), pixel)
        frames.append(wall)
    return frames, [SOURCE_DELAY_MS] * SOURCE_FRAMES


def main(argv: List[str] = None) -> int:  # type: ignore[assignment]
    """Write the pattern as a GIF."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--layout", default="2x2",
                        choices=sorted(DEFAULT_MAPS) + sorted(STRIP_MAPS),
                        help="1x9 makes the strip GIF (build step 6)")
    parser.add_argument("--out", required=True)
    args = parser.parse_args(argv)
    maps = dict(DEFAULT_MAPS, **STRIP_MAPS)
    frames, delays = make_frames(args.layout, maps[args.layout])
    frames[0].save(args.out, save_all=True, append_images=frames[1:],
                   duration=delays, loop=0)
    print("wrote %s (%d frames)" % (args.out, len(frames)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
