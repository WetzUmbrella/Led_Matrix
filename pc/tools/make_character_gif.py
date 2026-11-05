#!/usr/bin/env python3
"""Character GIF for the 2x2 showcase wall: "Blip", an original robot.

Owner: Cheston (Buddy 3).

Blip walks left to right across the 16 x 16 wall over a starry sky, with a
two-step walk cycle, a blinking antenna and blinking eyes. Walking across
every tile seam makes skew or a wrong partition visible. Colours are fully
saturated so they survive the 8/255 brightness cap (9 levels per channel).

Usage:
    python pc/tools/make_character_gif.py --out blip.gif
"""

from __future__ import annotations

import argparse
import sys
from typing import List, Optional, Tuple

from PIL import Image

#: Wall size for the 2x2 layout.
WALL = 16

#: GIF frame delay in ms (20 fps source, resampled to 60 fps on the PC).
DELAY_MS = 50

#: Frames per walk step (one pixel to the right).
FRAMES_PER_STEP = 4

#: Frames between blinks, and how long a blink lasts.
BLINK_PERIOD = 40
BLINK_FRAMES = 3

#: Ground row and sprite size.
GROUND_ROW = 15
SPRITE_W = 9
SPRITE_H = 12

Pixel = Tuple[int, int, int]

BLACK: Pixel = (0, 0, 0)
BODY: Pixel = (0, 220, 200)
BODY_DARK: Pixel = (0, 110, 160)
EYE: Pixel = (255, 255, 255)
PUPIL: Pixel = (0, 0, 0)
MOUTH: Pixel = (255, 60, 120)
FOOT: Pixel = (255, 140, 0)
ANTENNA: Pixel = (180, 180, 180)
TIP_ON: Pixel = (255, 0, 0)
TIP_OFF: Pixel = (255, 200, 0)
GROUND: Pixel = (0, 160, 0)
STAR: Pixel = (255, 255, 140)

#: Fixed stars (x, y) and their twinkle phase.
STARS = ((1, 1, 0), (6, 3, 7), (11, 1, 13), (14, 4, 4), (3, 6, 10),
         (9, 6, 2))


def put(frame: Image.Image, x: int, y: int, colour: Pixel) -> None:
    """Set one pixel if it lies on the wall."""
    if 0 <= x < WALL and 0 <= y < WALL:
        frame.putpixel((x, y), colour)


def draw_sky(frame: Image.Image, index: int) -> None:
    """Twinkling stars and the ground line."""
    for x, y, phase in STARS:
        if (index + phase) % 16 < 11:
            put(frame, x, y, STAR)
    for x in range(WALL):
        put(frame, x, GROUND_ROW, GROUND)


def draw_blip(frame: Image.Image, left: int, index: int) -> None:
    """Blip with its top-left corner at column ``left``."""
    top = GROUND_ROW - SPRITE_H
    step = (index // FRAMES_PER_STEP) % 2
    bob = step  # The body dips one pixel on every other step.
    blink = (index % BLINK_PERIOD) < BLINK_FRAMES

    # Antenna: stem and a tip that alternates red and yellow.
    put(frame, left + 4, top + bob + 1, ANTENNA)
    put(frame, left + 4, top + bob, TIP_ON if step else TIP_OFF)

    # Head and body: a rounded 9 x 7 block with a darker belly line.
    body_top = top + 2 + bob
    for dy in range(7):
        for dx in range(SPRITE_W):
            corner = dy in (0, 6) and dx in (0, SPRITE_W - 1)
            if not corner:
                put(frame, left + dx, body_top + dy, BODY)
    for dx in range(2, 7):
        put(frame, left + dx, body_top + 5, BODY_DARK)

    # Eyes: 2 x 2 white with a pupil looking ahead; a blink is a line.
    for eye_x in (left + 1, left + 5):
        if blink:
            put(frame, eye_x, body_top + 2, PUPIL)
            put(frame, eye_x + 1, body_top + 2, PUPIL)
        else:
            for dx in range(2):
                for dy in range(2):
                    put(frame, eye_x + dx, body_top + 1 + dy, EYE)
            put(frame, eye_x + 1, body_top + 2, PUPIL)

    # Smile.
    put(frame, left + 3, body_top + 4, MOUTH)
    put(frame, left + 4, body_top + 4, MOUTH)
    put(frame, left + 5, body_top + 4, MOUTH)
    put(frame, left + 2, body_top + 3, MOUTH)
    put(frame, left + 6, body_top + 3, MOUTH)

    # Feet: alternate which one is lifted.
    foot_row = GROUND_ROW - 1
    lifted = foot_row - 1
    put(frame, left + 2, lifted if step else foot_row, FOOT)
    put(frame, left + 1, lifted if step else foot_row, FOOT)
    put(frame, left + 6, foot_row if step else lifted, FOOT)
    put(frame, left + 7, foot_row if step else lifted, FOOT)


def build() -> List[Image.Image]:
    """One full walk: Blip enters on the left and leaves on the right."""
    positions = WALL + SPRITE_W  # From fully off the left to off the right.
    frames = []
    for index in range(positions * FRAMES_PER_STEP):
        frame = Image.new("RGB", (WALL, WALL), BLACK)
        draw_sky(frame, index)
        draw_blip(frame, (index // FRAMES_PER_STEP) - SPRITE_W, index)
        frames.append(frame)
    return frames


def main(argv: Optional[List[str]] = None) -> int:
    """Command line entry point."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", required=True)
    args = parser.parse_args(argv)
    frames = build()
    frames[0].save(args.out, save_all=True, append_images=frames[1:],
                   duration=DELAY_MS, loop=0, disposal=1)
    print("wrote %s (%d frames, %.1f s)" % (args.out, len(frames),
                                            len(frames) * DELAY_MS / 1000))
    return 0


if __name__ == "__main__":
    sys.exit(main())
