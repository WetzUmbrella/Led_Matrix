"""Step 4 outlier check (scratch). For one piece of a video: the reader's
steps and pairs (its own functions, same threshold and boxes), each tile's
missed counter steps, every pair with |skew| > LIMIT_MS, and for each such
pair a crop strip of both tiles from f-3 to f+4 (f = the earlier step's
frame) with a bar over the frames where each tile's step was timed.
Usage: outliers.py VIDEO START_S BOX_A BOX_B OUTDIR TAG (from Led_Matrix)
"""
import os
import subprocess
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.abspath("docs/bench/24nov/tools"))
import sync_video as sv  # noqa: E402

WIDTH, THRESHOLD, FPS, LIMIT_MS = 960, 200, 240.0, 4.0
video, start = sys.argv[1], float(sys.argv[2])
box_a = [int(v) for v in sys.argv[3].split(",")]
box_b = [int(v) for v in sys.argv[4].split(",")]
outdir, tag = sys.argv[5], sys.argv[6]
seg = os.path.join(outdir, "%s.mov" % tag)
subprocess.run(["ffmpeg", "-v", "error", "-y", "-ss", str(start), "-i", video,
                "-t", "10", "-an", "-c", "copy", seg], check=True)
sa, sb, crops = [], [], []
for h, data in sv.frames(seg, WIDTH):
    sa.append(sv.read_tile(data, WIDTH, box_a, THRESHOLD))
    sb.append(sv.read_tile(data, WIDTH, box_b, THRESHOLD))
    img = Image.frombytes("L", (WIDTH, h), data)
    crops.append((img.crop((box_a[0], box_a[1], box_a[0] + box_a[2],
                            box_a[1] + box_a[3])),
                  img.crop((box_b[0], box_b[1], box_b[0] + box_b[2],
                            box_b[1] + box_b[3]))))
os.remove(seg)
steps_a, steps_b = sv.steps(sa), sv.steps(sb)


def missed(steps):
    return sum(((vb - va) % 64) - 1 for (_, va), (_, vb) in
               zip(steps, steps[1:]) if (vb - va) % 64 > 1)


print("%s: frames %d, steps A %d B %d, missed A %d B %d" % (
    tag, len(sa), len(steps_a), len(steps_b), missed(steps_a),
    missed(steps_b)))
window = 0.6 * FPS
n = 0
for ta, va in steps_a:
    best = None
    for tb, vb in steps_b:
        if vb == va and abs(tb - ta) <= window and \
                (best is None or abs(tb - ta) < abs(best[0] - ta)):
            best = (tb, vb)
    if best is None:
        continue
    tb = best[0]
    skew = 1000.0 * (tb - ta) / FPS
    if abs(skew) <= LIMIT_MS:
        continue
    n += 1
    f0 = max(0, int(min(ta, tb)) - 3)
    f1 = min(len(crops) - 1, int(max(ta, tb)) + 4)
    cw = max(c[0].width for c in crops)
    ch = crops[0][0].height + crops[0][1].height + 8
    strip = Image.new("L", ((f1 - f0 + 1) * (cw + 4), ch + 8), 0)
    d = ImageDraw.Draw(strip)
    for i, f in enumerate(range(f0, f1 + 1)):
        x = i * (cw + 4)
        strip.paste(crops[f][0], (x, 8))
        strip.paste(crops[f][1], (x, 8 + crops[f][0].height + 8))
        if f == int(ta):
            d.rectangle([x, 0, x + cw // 2, 5], fill=255)  # A step: left bar
        if f == int(tb):
            d.rectangle([x + cw // 2, 0, x + cw, 5], fill=128)  # B: right
    name = "%s_out%02d.png" % (tag, n)
    strip.save(os.path.join(outdir, name))

    def trace(series, t):
        out = []
        for f in range(f0, f1 + 1):
            lv = series[f]
            out.append("--" if lv is None else
                       "%s" % (sv.decode(lv) if sv.decode(lv) is not None
                               else "~"))
        return " ".join("%3s" % v for v in out)
    print("  out%02d value %2d: A at f%.2f, B at f%.2f, skew %+.2f ms; "
          "frames %d..%d -> %s" % (n, va, ta, tb, skew, f0, f1, name))
    print("      A decoded: %s" % trace(sa, ta))
    print("      B decoded: %s" % trace(sb, tb))
print("%s: %d pairs over %.0f ms" % (tag, n, LIMIT_MS))
