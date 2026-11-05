#!/usr/bin/env python3
"""Showcase GIF for a two-tile wall (bench 5 Nov 2026 demonstration).

Owner: Cheston (Buddy 3).

Builds a 16 x 16 GIF for the 2x2 layout with Node IDs 1 and 2 on the top
row (map 1,2,0,0,0,0,0,0,0), so only rows 0 to 7 carry content and the
bottom half stays black. Two scenes, chosen to make the sync visible:

  * sweep: a white bar crosses both tiles left to right and back. The bar
    spans the seam between tile 1 and tile 2, so any skew between the
    tiles shows as a broken bar;
  * rainbow: diagonal colour bands scroll across both tiles; a seam shows
    as a step in the bands.

Usage:
    python pc/tools/make_showcase_gif.py --out showcase.gif
    python pc/tools/make_showcase_gif.py --scene sweep --seconds 4 --out s.gif
"""

from __future__ import annotations

import argparse
import colorsys
import sys
from typing import List, Optional, Tuple

from PIL import Image

#: Wall size for the 2x2 layout (the asset builder resizes to this).
WALL = 16

#: Rows that the two top-row tiles show.
ROWS_SHOWN = 8

#: GIF frame delay in ms (a multiple of the 10 ms GIF unit; 20 fps source,
#: resampled to 60 fps by the asset builder).
DELAY_MS = 50

#: Width of the sweeping bar in pixels.
BAR_WIDTH = 2

#: Pixels the rainbow bands move per frame.
RAINBOW_STEP = 1

#: Width of one full rainbow cycle in pixels.
RAINBOW_PERIOD = 16

Pixel = Tuple[int, int, int]


def blank() -> Image.Image:
    """A black wall frame."""
    return Image.new("RGB", (WALL, WALL), (0, 0, 0))


def sweep_frames(count: int) -> List[Image.Image]:
    """A white bar moving across both tiles and back (ping-pong)."""
    span = WALL - BAR_WIDTH
    frames = []
    for index in range(count):
        phase = index % (2 * span)
        left = phase if phase < span else (2 * span) - phase
        frame = blank()
        for x in range(left, left + BAR_WIDTH):
            for y in range(ROWS_SHOWN):
                frame.putpixel((x, y), (255, 255, 255))
        frames.append(frame)
    return frames


def hue_pixel(hue: float) -> Pixel:
    """Fully saturated colour for a hue in 0 to 1."""
    red, green, blue = colorsys.hsv_to_rgb(hue % 1.0, 1.0, 1.0)
    return (round(red * 255), round(green * 255), round(blue * 255))


def rainbow_frames(count: int) -> List[Image.Image]:
    """Diagonal rainbow bands scrolling left to right."""
    frames = []
    for index in range(count):
        frame = blank()
        shift = index * RAINBOW_STEP
        for x in range(WALL):
            for y in range(ROWS_SHOWN):
                hue = ((x + y) - shift) / RAINBOW_PERIOD
                frame.putpixel((x, y), hue_pixel(hue))
        frames.append(frame)
    return frames


def build(scene: str, seconds: float) -> List[Image.Image]:
    """Frames for one scene, or both scenes one after the other."""
    count = max(1, round(seconds * 1000 / DELAY_MS))
    if scene == "sweep":
        return sweep_frames(count)
    if scene == "rainbow":
        return rainbow_frames(count)
    half = max(1, count // 2)
    return sweep_frames(half) + rainbow_frames(count - half)


def save(frames: List[Image.Image], path: str) -> None:
    """Write an endlessly looping GIF."""
    frames[0].save(path, save_all=True, append_images=frames[1:],
                   duration=DELAY_MS, loop=0, disposal=1)


def main(argv: Optional[List[str]] = None) -> int:
    """Command line entry point."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--scene", choices=("both", "sweep", "rainbow"),
                        default="both")
    parser.add_argument("--seconds", type=float, default=8.0,
                        help="loop length; at most 10 s (PostTri NFR3)")
    parser.add_argument("--out", required=True)
    args = parser.parse_args(argv)
    if not 0 < args.seconds <= 10:
        parser.error("--seconds must be above 0 and at most 10")
    frames = build(args.scene, args.seconds)
    save(frames, args.out)
    print("wrote %s (%d frames, %d ms each)" % (args.out, len(frames),
                                                 DELAY_MS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
