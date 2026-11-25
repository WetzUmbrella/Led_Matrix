"""Diagnostic (scratch, step 4): cut a video into pieces, find each tile's
LED extent per piece (every 10th frame, pixels >= LED_LEVEL), set boxes at
that extent plus a margin, and run sync_video.py on each piece.
Usage: segments2.py VIDEO SEG_S WIDTH XA0,XA1,YA0,YA1 XB0,XB1,YB0,YB1 OUTDIR
(from Led_Matrix). Like segments.py, with a height range per tile, for
videos with more than one row of tiles.
"""
import os
import subprocess
import sys

from PIL import Image

sys.path.insert(0, os.path.abspath("docs/bench/24nov/tools"))
import sync_video as sv  # noqa: E402

LED_LEVEL, MARGIN, THRESHOLD = 240, 8, 200
FPS = os.environ.get("SEG_FPS", "240")  # 120 for Slo-mo 120 fps files
video, seg_s, width = sys.argv[1], float(sys.argv[2]), int(sys.argv[3])
reg_a = [int(v) for v in sys.argv[4].split(",")]
reg_b = [int(v) for v in sys.argv[5].split(",")]
outdir = sys.argv[6]
total = float(subprocess.run(
    ["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of",
     "default=nw=1:nk=1", video], capture_output=True, text=True,
    check=True).stdout.strip())
py = os.path.join(".venv", "Scripts", "python.exe")
start = 0.0
while start < total - 1.0:
    seg = os.path.join(outdir, "seg_%03d.mov" % int(start))
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-ss", str(start), "-i",
                    video, "-t", str(seg_s), "-an", "-c", "copy", seg],
                   check=True)
    boxes = {}
    for name, (x0, x1, y0, y1) in (("A", reg_a), ("B", reg_b)):
        bb = None
        for i, (h, data) in enumerate(sv.frames(seg, width)):
            if i % 10:
                continue
            img = Image.frombytes("L", (width, h), data).crop((x0, y0, x1, y1))
            b = img.point(lambda v: 255 if v >= LED_LEVEL else 0).getbbox()
            if b:
                b = (b[0] + x0, b[1] + y0, b[2] + x0, b[3] + y0)
                bb = b if bb is None else (min(bb[0], b[0]), min(bb[1], b[1]),
                                           max(bb[2], b[2]), max(bb[3], b[3]))
        if bb is None:
            boxes[name] = None
            continue
        bx, by = max(0, bb[0] - MARGIN), max(0, bb[1] - MARGIN)
        bw = min(width, bb[2] + MARGIN) - bx
        bh = min(h, bb[3] + MARGIN) - by
        boxes[name] = "%d,%d,%d,%d" % (bx, by, bw, bh)
    print("=== %s  start %.0f s  boxes A %s  B %s" % (
        os.path.basename(video), start, boxes["A"], boxes["B"]), flush=True)
    if boxes["A"] and boxes["B"]:
        out = subprocess.run(
            [py, "-I", "docs/bench/24nov/tools/sync_video.py", seg,
             "--width", str(width), "--fps", FPS, "--threshold",
             str(THRESHOLD), "--box-a", boxes["A"], "--box-b", boxes["B"]],
            capture_output=True, text=True)
        print("\n".join(out.stdout.splitlines()[1:]) + out.stderr, flush=True)
    os.remove(seg)
    start += seg_s
