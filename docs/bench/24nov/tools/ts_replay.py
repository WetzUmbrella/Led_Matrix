"""Replay each tile's time-sync filter (panel_sync/main/timesync.c) on the
BCN lines of its serial log, and estimate each tile's clock error and the
skew between tiles (scratch, NR-07 follow-up).

Per tile: samples o = t_l - t_p. Filter as in the firmware: window of 16,
estimate = smallest o (and its t_l), drift = (min of newer 8 - min of older
8) / (their t_l span) once full, target = est + drift x (t - est_ref), the
published offset moves to the target by at most 100 us per 60 Hz tick;
reseed rule (5 s since 24 Nov evening, 3 samples) included.
Reference ("true" offset): lower envelope of o, the smallest o in each
BLOCK_S block joined by straight lines. Error = published - reference: a
tile with error e fires its ticks e us late. Skew B - A = e_B - e_A at the
same Pico time.
Usage: ts_replay.py LOG_A LOG_B [LOG_C ...]
"""
import bisect
import statistics
import sys
import os

# RESEED_US: 5 s since 24 Nov evening (TIMESYNC_RESEED_US); RESEED_US=500000
# in the environment replays the old rule.
WINDOW, HALF, SLEW, RESEED_N = 16, 8, 100, 3
RESEED_US = int(os.environ.get("RESEED_US", "5000000"))
TICK_US = 1000000.0 / 60.0
BLOCK_S = 5.0
import os
_M = os.environ.get("DRIFT_MODE", "fw")
def MODE(d):
    if _M == "none":
        return 0.0
    if _M == "clamp50":
        return max(-50e-6, min(50e-6, d))
    return d


def load(path):
    out = []
    for line in open(path, encoding="utf-8", errors="replace"):
        i = line.find("BCN,")
        if i < 0:
            continue
        parts = line[i:].strip().split(",")
        if len(parts) != 4:
            continue
        try:
            out.append((int(parts[1]), int(parts[2]), int(parts[3])))
        except ValueError:
            continue
    # One sample per beacon seq, in local-time order.
    seen, uniq = set(), []
    for s in sorted(out, key=lambda r: r[2]):
        if s[0] not in seen:
            seen.add(s[0])
            uniq.append(s)
    return uniq


def envelope(samples):
    pts = []
    t0 = samples[0][2]
    block = {}
    for _, tp, tl in samples:
        k = int((tl - t0) / (BLOCK_S * 1e6))
        o = tl - tp
        if k not in block or o < block[k][1]:
            block[k] = (tl, o)
    pts = sorted(block.values())
    xs = [p[0] for p in pts]

    def ref(tl):
        j = bisect.bisect_left(xs, tl)
        if j <= 0:
            return pts[0][1]
        if j >= len(pts):
            return pts[-1][1]
        (x0, y0), (x1, y1) = pts[j - 1], pts[j]
        return y0 + (y1 - y0) * (tl - x0) / (x1 - x0)
    return ref


def replay(samples):
    """List of (pico_time, local_time, published_offset) per tick."""
    win, est, est_ref, drift, pub = [], None, None, None, None
    off_run, ticks, i = 0, [], 0
    t = samples[0][2]
    end = samples[-1][2]
    while t <= end:
        while i < len(samples) and samples[i][2] <= t:
            _, tp, tl = samples[i]
            o = tl - tp
            b_off = est is not None and abs(o - est) > RESEED_US
            off_run = off_run + 1 if b_off else 0
            if b_off and off_run >= RESEED_N:
                win, est, drift, pub, off_run = [], None, None, None, 0
            if not b_off or off_run == 0:
                win = (win + [(tl, o)])[-WINDOW:]
                m = min(win, key=lambda w: w[1])
                est, est_ref = m[1], m[0]
                if len(win) == WINDOW:
                    a = min(win[:HALF], key=lambda w: w[1])
                    b = min(win[HALF:], key=lambda w: w[1])
                    if b[0] != a[0]:
                        drift = (b[1] - a[1]) / (b[0] - a[0])  # us per us
                        drift = MODE(drift)
            i += 1
        if est is not None:
            d = drift or 0.0
            target = est + d * (t - est_ref)
            if pub is None:
                pub = (target, t, d)
            else:
                applied = pub[0] + pub[2] * (t - pub[1])
                step = max(-SLEW, min(SLEW, target - applied))
                pub = (applied + step, t, d)
            ticks.append((t - pub[0], t, pub[0]))
        t += TICK_US
    return ticks


def pct(v, q):
    s = sorted(v)
    return s[min(len(s) - 1, int(q * (len(s) - 1)))]


def main():
    tiles = {}
    for path in sys.argv[1:]:
        name = path.replace("\\", "/").split("/")[-1].split(".")[0]
        smp = load(path)
        if len(smp) < 100:
            print("%s: only %d BCN samples" % (name, len(smp)))
            continue
        ref = envelope(smp)
        excess = [(tl - tp) - ref(tl) for _, tp, tl in smp]
        ticks = replay(smp)
        err = [(pt, pub - ref(tl)) for pt, tl, pub in ticks]
        tiles[name] = err
        seqs = [s[0] for s in smp]
        print("%s: %d beacons (seq %d..%d, %d missing), %.0f s" % (
            name, len(smp), seqs[0], seqs[-1],
            seqs[-1] - seqs[0] + 1 - len(smp),
            (smp[-1][2] - smp[0][2]) / 1e6))
        print("   beacon delay over the floor (ms): median %.2f, p90 %.2f, "
              "p99 %.2f, max %.2f" % tuple(
                  x / 1000 for x in (statistics.median(excess),
                                     pct(excess, 0.9), pct(excess, 0.99),
                                     max(excess))))
        ev = [e for _, e in err[int(2 * 60):]]  # skip the first 2 s
        print("   clock error, published - floor (ms): median %.2f, "
              "p1 %.2f, p99 %.2f, min %.2f, max %.2f" % tuple(
                  x / 1000 for x in (statistics.median(ev), pct(ev, 0.01),
                                     pct(ev, 0.99), min(ev), max(ev))))
    names = list(tiles)
    for a_i in range(len(names)):
        for b_i in range(a_i + 1, len(names)):
            a, b = tiles[names[a_i]], tiles[names[b_i]]
            ax = [p for p, _ in a]
            lo = max(a[0][0], b[0][0]) + 2e6
            hi = min(a[-1][0], b[-1][0])
            skews = []
            for pt, eb in b:
                if pt < lo or pt > hi:
                    continue
                j = bisect.bisect_left(ax, pt)
                if 0 < j < len(a):
                    skews.append((pt, eb - a[j][1]))
            if not skews:
                continue
            v = [s for _, s in skews]
            over = [s for s in v if abs(s) >= 5000]
            print("skew %s - %s over %.0f s (ms): median %.2f, p1 %.2f, "
                  "p99 %.2f, min %.2f, max %.2f; ticks over 5 ms: %d of %d" % (
                      names[b_i], names[a_i], (hi - lo) / 1e6,
                      statistics.median(v) / 1000, pct(v, 0.01) / 1000,
                      pct(v, 0.99) / 1000, min(v) / 1000, max(v) / 1000,
                      len(over), len(v)))
            # Episodes: runs of |skew| over 2 ms.
            run = None
            for pt, s in skews:
                if abs(s) > 2000:
                    run = run or [pt, pt, s]
                    run[1] = pt
                    run[2] = s if abs(s) > abs(run[2]) else run[2]
                elif run:
                    print("   episode at pico %.1f s, %.2f s long, peak "
                          "%.2f ms" % (run[0] / 1e6, (run[1] - run[0]) / 1e6,
                                       run[2] / 1000))
                    run = None


if __name__ == "__main__":
    main()
