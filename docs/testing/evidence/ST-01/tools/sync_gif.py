#!/usr/bin/env python3
"""A GIF for measuring video sync with a slow-motion camera (bench 24 Nov
2026, NR-07, no wiring possible): every 8x8 cell of the wall shows the same
6-bit Gray-code counter that steps every 20 ms (50 steps a second, so
exactly one block changes per step), as a 4x4 grid of 2x2 LED blocks:

    row 0:  anchor  bit0  bit1  anchor
    row 1:  bit2    bit3  bit4  bit5
    row 2:  dark    dark  dark  dark
    row 3:  anchor  anchor anchor anchor

The always-lit anchors give every video frame the tile's outline (a
hand-held phone may drift) and the lit level; row 2 gives the dark level.

Usage: sync_gif.py ROWS COLS OUT.gif     (e.g. 3 3 pc/gifs/sync_3x3.gif)
"""
import sys
from PIL import Image

STEP_MS = 20      # Counter step; 0 or 10 ms would be read as 100 ms.
FRAMES = 500      # 10 s, 600 ticks at 60 fps (limit 1023).
ON = 255


def gray(value):
    return value ^ (value >> 1)


#: Block (row, column) of each counter bit, and the anchor blocks.
BIT_BLOCKS = [(0, 1), (0, 2), (1, 0), (1, 1), (1, 2), (1, 3)]
ANCHORS = [(0, 0), (0, 3), (3, 0), (3, 1), (3, 2), (3, 3)]
DARK = [(2, 0), (2, 1), (2, 2), (2, 3)]
BITS = len(BIT_BLOCKS)


def cell(value):
    """8x8 pixels (row-major list of 0/255) for one counter value."""
    px = [0] * 64
    code = gray(value % (1 << BITS))
    lit = list(ANCHORS) + [BIT_BLOCKS[bit] for bit in range(BITS)
                           if code & (1 << bit)]
    for brow, bcol in lit:
        for dr in (0, 1):
            for dc in (0, 1):
                px[(2 * brow + dr) * 8 + 2 * bcol + dc] = ON
    return px


def main():
    rows, cols, out = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
    frames = []
    for value in range(FRAMES):
        img = Image.new("RGB", (cols * 8, rows * 8))
        px = cell(value)
        for r in range(rows * 8):
            for c in range(cols * 8):
                v = px[(r % 8) * 8 + (c % 8)]
                img.putpixel((c, r), (v, v, v))
        frames.append(img)
    frames[0].save(out, save_all=True, append_images=frames[1:],
                   duration=STEP_MS, loop=0)


if __name__ == "__main__":
    main()
