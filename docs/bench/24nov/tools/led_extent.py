"""Per-frame extent of near-saturated pixels in two regions, and the
brightest pixel just outside them (scratch, step 4 box choice).
Usage: led_extent.py VIDEO WIDTH LEVEL XA0 XA1 XB0 XB1 Y0 Y1"""
import os
import sys

sys.path.insert(0, os.path.abspath("docs/bench/24nov/tools"))
import sync_video as sv  # noqa: E402

video, width, level = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
xa0, xa1, xb0, xb1, y0, y1 = (int(v) for v in sys.argv[4:10])
ext = {"A": [], "B": []}
frames = 0
for h, data in sv.frames(video, width):
    frames += 1
    for name, (x0, x1) in (("A", (xa0, xa1)), ("B", (xb0, xb1))):
        xs, ys = [], []
        for y in range(y0, y1):
            row = data[y * width + x0:y * width + x1]
            for i, v in enumerate(row):
                if v >= level:
                    xs.append(x0 + i)
                    ys.append(y)
        if xs:
            ext[name].append((min(xs), max(xs), min(ys), max(ys)))
print("frames", frames)
for name, e in ext.items():
    if not e:
        print(name, "nothing at level", level)
        continue
    print("%s: x0 %d..%d  x1 %d..%d  y0 %d..%d  y1 %d..%d  (frames %d)" % (
        name, min(v[0] for v in e), max(v[0] for v in e),
        min(v[1] for v in e), max(v[1] for v in e),
        min(v[2] for v in e), max(v[2] for v in e),
        min(v[3] for v in e), max(v[3] for v in e), len(e)))
