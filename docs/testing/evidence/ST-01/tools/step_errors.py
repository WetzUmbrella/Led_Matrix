"""Per-step error of sync_video.py against a known skew (scratch, step 1).
Usage: step_errors.py VIDEO TRUE_MS FPS (run from Led_Matrix)."""
import os
import sys

sys.path.insert(0, os.path.abspath("docs/testing/evidence/ST-01/tools"))
import sync_video as sv  # noqa: E402

video, true_ms, fps = sys.argv[1], float(sys.argv[2]), float(sys.argv[3])
box_a, box_b = [50, 70, 115, 115], [190, 70, 115, 115]
sa, sb, thr = [], [], None
for _h, data in sv.frames(video, 480):
    if thr is None:
        thr = max(data) // 2
    sa.append(sv.read_tile(data, 480, box_a, thr))
    sb.append(sv.read_tile(data, 480, box_b, thr))
ms = [1000.0 * s / fps for s in sv.pair(sv.steps(sa), sv.steps(sb), 0.6 * fps)]
err = [m - true_ms for m in ms]
frame_ms = 1000.0 / fps
print("true %.1f: n %d, skew min %.2f max %.2f; error min %.2f max %.2f; "
      "steps off by more than a frame (%.2f ms): %d"
      % (true_ms, len(ms), min(ms), max(ms), min(err), max(err), frame_ms,
         sum(abs(e) > frame_ms for e in err)))
