#!/usr/bin/env python3
"""A synthetic slow-motion video of two tiles playing the counter GIF with a
known skew, to test sync_video.py before trusting it (bench 24 Nov 2026).
Each video frame integrates its whole exposure (8 sub-samples); the tiles
step on the 60 fps tick grid like the wall; noise and a slow drift of the
picture stand in for a hand-held phone.

Usage: synth_sync_video.py SKEW_MS SECONDS OUT.mp4 [FPS]
"""
import math
import os
import random
import subprocess
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "..", "..", "..", "pc"))
import sync_gif as sg  # noqa: E402
import asset_builder as ab  # noqa: E402

W, H, LED, PITCH = 480, 270, 7, 12
TICKS = ab.resample_60fps([sg.STEP_MS] * sg.FRAMES)


def value_at(t):
    """Counter value a tile shows at time t (s): the GIF on the tick grid."""
    tick = int(t * 60.0)
    return TICKS[tick % len(TICKS)]


def draw(img, x0, y0, value, weight):
    pattern = sg.cell(value)
    d = ImageDraw.Draw(img)
    for i, v in enumerate(pattern):
        if v:
            r, c = divmod(i, 8)
            x, y = x0 + c * PITCH, y0 + r * PITCH
            d.rectangle([x, y, x + LED, y + LED], fill=int(200 * weight))


def main():
    skew = float(sys.argv[1]) / 1000.0
    seconds = float(sys.argv[2])
    out = sys.argv[3]
    fps = float(sys.argv[4]) if len(sys.argv) > 4 else 240.0
    rnd = random.Random(7)
    t0 = 0.0123  # The camera starts at an arbitrary phase.
    enc = subprocess.Popen(
        ["ffmpeg", "-v", "error", "-y", "-f", "rawvideo", "-pix_fmt", "gray",
         "-s", "%dx%d" % (W, H), "-r", str(fps), "-i", "-", "-c:v",
         "libx264", "-crf", "16", "-pix_fmt", "yuv420p", out],
        stdin=subprocess.PIPE)
    sub = 8
    for f in range(int(seconds * fps)):
        acc = [0.0] * (W * H)
        dx = 4.0 * math.sin(f / fps * 1.3)
        dy = 3.0 * math.sin(f / fps * 0.9)
        for k in range(sub):
            t = t0 + (f + (k + 0.5) / sub) / fps
            img = Image.new("L", (W, H))
            draw(img, int(60 + dx), int(80 + dy), value_at(t), 1.0)
            draw(img, int(60 + 9 * PITCH + 30 + dx), int(80 + dy),
                 value_at(t - skew), 1.0)
            for i, v in enumerate(img.getdata()):
                acc[i] += v / sub
        noisy = bytes(min(255, max(0, int(v + rnd.gauss(0, 4))))
                      for v in acc)
        enc.stdin.write(noisy)
    enc.stdin.close()
    enc.wait()
    print("wrote %s: skew %.2f ms, %d frames at %.0f fps"
          % (out, skew * 1000, int(seconds * fps), fps))


if __name__ == "__main__":
    main()
