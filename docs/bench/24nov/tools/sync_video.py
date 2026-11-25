#!/usr/bin/env python3
"""Measure the video sync skew between two tiles from a slow-motion video
(bench 24 Nov 2026, NR-07: the tiles have no pins to wire a recorder to).

The wall plays the counter GIF of ``sync_gif.py``. Every 20 ms one block of
each tile changes (Gray code). This tool finds, for each tile, the time of
every counter step to a fraction of a video frame, and pairs the steps of
tile A and tile B by counter value: skew = t_B - t_A for each step.

A step is timed from the one block that changes: in the video frame whose
exposure straddles the change the block is partly lit, and with the
exposure taken as the whole frame period (the default, ``--exposure 1``)
the change happened at f + 1 - b for a block brightening to level b in
frame f (f + b when it darkens). With a shorter exposure the error per
step is at most one frame, the same for both tiles' timing method.

Steps:
  1. ``sync_video.py VIDEO --preview p.png`` writes the brightest pixels of
     the first second (scaled to --width), to pick each tile's box.
  2. ``sync_video.py VIDEO --box-a X,Y,W,H --box-b X,Y,W,H`` (boxes in the
     preview's pixels, a little larger than each tile's lit area).

Frame times are frame index / --fps (default: the stream's frame rate). The
report also gives the frame rate implied by the counter (50 steps a second)
as a check on slow-motion files.
"""
import argparse
import json
import os
import statistics
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sync_gif as sg  # noqa: E402

#: A block reads 0 below this share of the lit level and 1 above 1 - this.
CLEAN = 0.25

#: Least difference between the lit and the dark level, grey levels.
MIN_CONTRAST = 20


def probe(path):
    """Width, height and frame rate of the video stream."""
    out = subprocess.run(
        ["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries",
         "stream=width,height,r_frame_rate,avg_frame_rate,nb_frames",
         "-of", "json", path], capture_output=True, text=True, check=True)
    stream = json.loads(out.stdout)["streams"][0]
    num, den = stream["r_frame_rate"].split("/")
    return stream, float(num) / float(den)


def frames(path, width):
    """Yield (height, bytes) per frame, grey, scaled to width."""
    stream, _ = probe(path)
    height = int(round(stream["height"] * width / stream["width"] / 2) * 2)
    proc = subprocess.Popen(
        ["ffmpeg", "-v", "error", "-i", path, "-vf",
         "scale=%d:%d,format=gray" % (width, height), "-f", "rawvideo", "-"],
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    size = width * height
    while True:
        data = proc.stdout.read(size)
        if len(data) < size:
            break
        yield height, data
    proc.wait()


def preview(path, width, out, seconds=1.0):
    from PIL import Image
    _, fps = probe(path)
    peak = None
    for index, (height, data) in enumerate(frames(path, width)):
        peak = bytearray(data) if peak is None else \
            bytearray(max(a, b) for a, b in zip(peak, data))
        if index >= fps * seconds:
            break
    Image.frombytes("L", (width, height), bytes(peak)).save(out)
    print("preview %s (%dx%d)" % (out, width, height))


def read_tile(data, width, box, threshold):
    """The six counter blocks of one tile in one frame, each 0 to 1 (share
    of the lit level), or None when the tile cannot be read."""
    x, y, w, h = box
    lit = [(px, py) for py in range(y, y + h) for px in range(x, x + w)
           if data[py * width + px] >= threshold]
    if len(lit) < 12:
        return None
    x0 = min(p[0] for p in lit)
    x1 = max(p[0] for p in lit) + 1
    y0 = min(p[1] for p in lit)
    y1 = max(p[1] for p in lit) + 1
    bw, bh = (x1 - x0) / 4.0, (y1 - y0) / 4.0

    def level(block):
        brow, bcol = block
        cx, cy = x0 + (bcol + 0.5) * bw, y0 + (brow + 0.5) * bh
        rx, ry = max(1, int(bw / 4)), max(1, int(bh / 4))
        vals = [data[py * width + px]
                for py in range(int(cy) - ry, int(cy) + ry + 1)
                for px in range(int(cx) - rx, int(cx) + rx + 1)]
        return sum(vals) / len(vals)

    on = statistics.mean(level(b) for b in sg.ANCHORS)
    off = statistics.mean(level(b) for b in sg.DARK)
    if on - off < MIN_CONTRAST:
        return None
    return [min(1.0, max(0.0, (level(b) - off) / (on - off)))
            for b in sg.BIT_BLOCKS]


def ungray(code):
    value = 0
    while code:
        value ^= code
        code >>= 1
    return value


def decode(bits):
    """Counter value of a clean reading, or None."""
    if bits is None or any(CLEAN <= b <= 1 - CLEAN for b in bits):
        return None
    code = sum(1 << i for i, b in enumerate(bits) if b > 0.5)
    return ungray(code)


def steps(series):
    """(time in frames, new value) of every single-block step of a tile.
    ``series`` holds each frame's block levels (or None)."""
    out = []
    clean = [(f, decode(b)) for f, b in enumerate(series)
             if decode(b) is not None]
    for (fa, va), (fb, vb) in zip(clean, clean[1:]):
        if va == vb:
            continue
        diff = sg.gray(va) ^ sg.gray(vb)
        if diff & (diff - 1):
            continue  # More than one block changed: not one step.
        bit = diff.bit_length() - 1
        rising = bool(sg.gray(vb) & diff)
        when = None
        # From fa itself: a frame whose block is only slightly lit (under
        # CLEAN) still reads as the old value but already holds the change.
        for f in range(fa, fb + 1):
            levels = series[f]
            if levels is None:
                break
            b = levels[bit] if rising else 1.0 - levels[bit]
            if b > 0.02:
                when = f + (1.0 - b if b < 0.98 else 0.0)
                break
        if when is not None:
            out.append((when, vb))
    return out


def pair(steps_a, steps_b, window):
    """Skews in frames: for each step of A, the step of B to the same value
    nearest in time, within the window (frames)."""
    skews = []
    for ta, va in steps_a:
        best = None
        for tb, vb in steps_b:
            if vb == va and abs(tb - ta) <= window and \
                    (best is None or abs(tb - ta) < abs(best)):
                best = tb - ta
        if best is not None:
            skews.append(best)
    return skews


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("video")
    ap.add_argument("--width", type=int, default=480)
    ap.add_argument("--preview")
    ap.add_argument("--box-a")
    ap.add_argument("--box-b")
    ap.add_argument("--fps", type=float)
    ap.add_argument("--threshold", type=int,
                    help="lit grey level (default: half the brightest)")
    args = ap.parse_args()
    if args.preview:
        preview(args.video, args.width, args.preview)
        return
    box_a = [int(v) for v in args.box_a.split(",")]
    box_b = [int(v) for v in args.box_b.split(",")]
    stream, fps = probe(args.video)
    fps = args.fps or fps
    series_a, series_b, threshold = [], [], args.threshold
    for height, data in frames(args.video, args.width):
        if threshold is None:
            threshold = max(data) // 2
        series_a.append(read_tile(data, args.width, box_a, threshold))
        series_b.append(read_tile(data, args.width, box_b, threshold))
    steps_a, steps_b = steps(series_a), steps(series_b)
    # The counter wraps every 64 steps (1.28 s): pair within half of it.
    skews = pair(steps_a, steps_b, 0.6 * fps)
    ms = [1000.0 * s / fps for s in skews]
    span = (steps_a[-1][0] - steps_a[0][0]) if len(steps_a) > 1 else 0
    print("video: %s, %d frames, stream %.3f fps (avg %s), used %.3f fps"
          % (args.video, len(series_a), probe(args.video)[1],
             stream.get("avg_frame_rate"), fps))
    print("readable frames: A %d, B %d"
          % (sum(s is not None for s in series_a),
             sum(s is not None for s in series_b)))
    if span:
        print("steps: A %d, B %d; implied frame rate %.1f fps (50 steps/s)"
              % (len(steps_a), len(steps_b),
                 50.0 * span / (len(steps_a) - 1)))
    if not ms:
        print("no paired steps")
        return
    absolute = sorted(abs(v) for v in ms)
    print("paired steps: %d" % len(ms))
    print("skew B - A: median %.2f ms, mean %.2f ms" %
          (statistics.median(ms), statistics.mean(ms)))
    print("|skew|: median %.2f ms, p95 %.2f ms, max %.2f ms" %
          (statistics.median(absolute),
           absolute[int(0.95 * (len(absolute) - 1))], absolute[-1]))
    print("NR-07 (under 5 ms): %s" % ("PASS" if absolute[-1] < 5.0
                                      else "FAIL"))


if __name__ == "__main__":
    main()
