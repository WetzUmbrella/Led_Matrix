"""Replay of the Pico's tap grouping (bench 24 Nov 2026): joins the Pico's
GAME_TAP events (USB log) with the website tap log's peaks, then reports
delivery times, stamp spread and, per collect wait and group memory, how
many taps would be judged on an echo.

Usage (from Led_Matrix): python3 docs/bench/24nov/tools/echo_sim.py PICO_LOG TAPS_CSV
"""
import csv, re, sys
log, taps_csv = sys.argv[1], sys.argv[2]
ev = []
for line in open(log, errors="replace"):
    m = re.search(r"CL_EVENT t_ms=(\d+) (\w+) node=(\d+) value=(\d+)", line)
    if m: ev.append((int(m[1]), m[2], int(m[3]), int(m[4])))
# rounds: from STATE 12 to next STATE 7
rounds = []; cur = None
for t, k, n, v in ev:
    if k == "STATE" and v == 12: cur = [t, None]
    elif k == "STATE" and v == 7 and cur: cur[1] = t; rounds.append(cur); cur = None
print("rounds (Pico s):", [(a // 1000, b // 1000) for a, b in rounds])
pc = [r for r in csv.DictReader(open(taps_csv)) if r["pc_time"] >= "2026-11-24 15:15"]
peaks = {}
for r in pc: peaks.setdefault(int(r["tile"]), []).append((int(r["t_tap_us"]), int(r["peak_mg"])))
taps = []
for t, k, n, v in ev:
    if k != "GAME_TAP": continue
    if v >= 2**31: v -= 2**32
    stamp_ms = t - v / 1000.0
    cand = [p for (s, p) in peaks.get(n, []) if abs(s / 1000.0 - stamp_ms) < 3]
    taps.append(dict(rx=t, node=n, stamp=stamp_ms, lat=v / 1000.0, peak=cand[0] if cand else None))
print("Pico taps", len(taps), "matched peaks", sum(t["peak"] is not None for t in taps))
for ri, (a, b) in enumerate(rounds):
    rt = sorted([t for t in taps if a <= t["rx"] <= b and t["peak"] is not None], key=lambda t: t["stamp"])
    groups = []
    for t in rt:
        if groups and t["stamp"] - groups[-1][0]["stamp"] < 30: groups[-1].append(t)
        else: groups.append([t])
    fb = [e for e in ev if e[1] == "GAME_FEEDBACK" and a <= e[0] <= b]
    print("\nround %d: taps %d, physical %d, feedback hit %d miss %d" % (ri + 1, len(rt), len(groups), sum(e[3] == 1 for e in fb), sum(e[3] == 0 for e in fb)))
    late = []
    for g in groups:
        first = min(t["rx"] for t in g)
        win = max(g, key=lambda t: (t["peak"], -t["stamp"]))
        late.append((win["rx"] - first, g, win))
    multi = [x for x in late if len(x[1]) > 1]
    ls = sorted(x[0] for x in multi)
    q = lambda p: ls[min(len(ls) - 1, int(p * len(ls)))] if ls else 0
    print(" multi-tile taps %d; winner arrives after first: p50 %d p90 %d p95 %d max %d ms" % (len(multi), q(.5), q(.9), q(.95), ls[-1] if ls else 0))
    lat = sorted(t["lat"] for t in rt); print(" delivery latency stamp->Pico ms p50 %.0f p95 %.0f max %.0f" % (lat[len(lat)//2], lat[int(.95*len(lat))], lat[-1]))
    for collect in (20, 40, 60, 80, 100, 150):
        bad = 0
        for _, g, win in late:
            first_stamp = min(t["stamp"] for t in g); first_rx = min(t["rx"] for t in g)
            due = max(first_stamp + 30, first_rx + collect)
            if win["rx"] > due: bad += 1
        print("  collect %3d ms: taps judged without their winner %d of %d" % (collect, bad, len(groups)))
print("\n-- stamp spread when grouping loosely (gap < 150 ms between successive stamps)")
for ri, (a, b) in enumerate(rounds):
    rt = sorted([t for t in taps if a <= t["rx"] <= b and t["peak"] is not None], key=lambda t: t["stamp"])
    groups = []
    for t in rt:
        if groups and t["stamp"] - groups[-1][-1]["stamp"] < 150: groups[-1].append(t)
        else: groups.append([t])
    sp = sorted(g[-1]["stamp"] - g[0]["stamp"] for g in groups if len(g) > 1)
    over = [g for g in groups if g[-1]["stamp"] - g[0]["stamp"] >= 30]
    print("round %d: loose groups %d, stamp spread p50 %.0f p90 %.0f max %.0f ms, groups spread>=30 ms: %d" % (ri+1, len(groups), sp[len(sp)//2], sp[int(.9*len(sp))], sp[-1], len(over)))
    for g in over[:4]:
        print("   ", [(t["node"], round(t["stamp"]-g[0]["stamp"]), t["peak"]) for t in g])

def simulate(rt, collect, memory):
    """Pico rules: pending taps, judged at max(first stamp+30, first rx+collect);
    a tap stamped within 30 ms either side of one of the last `memory` judged
    group starts is dropped. Returns (judgements, judged without the true winner)."""
    truth = []  # physical taps by stamp
    for t in sorted(rt, key=lambda t: t["stamp"]):
        if truth and t["stamp"] - truth[-1][0]["stamp"] < 30: truth[-1].append(t)
        else: truth.append([t])
    winner = {}
    for g in truth:
        w = max(g, key=lambda t: (t["peak"], -t["stamp"]))
        for t in g: winner[id(t)] = w
    pending, windows, judged, bad = [], [], 0, 0
    def due():
        return max(min(t["stamp"] for t in pending) + 30, min(t["rx"] for t in pending) + collect)
    def resolve():
        nonlocal judged, bad
        start = min(t["stamp"] for t in pending)
        grp = [t for t in pending if t["stamp"] < start + 30]
        best = max(grp, key=lambda t: (t["peak"], -t["stamp"]))
        judged += 1
        if best is not winner[id(best)]: bad += 1
        pending[:] = [t for t in pending if t["stamp"] >= start + 30]
        windows.append(start); del windows[:-memory]
    for t in sorted(rt, key=lambda t: t["rx"]):
        while pending and due() <= t["rx"]: resolve()
        if any(abs(t["stamp"] - w) < 30 for w in windows): continue
        pending.append(t)
    while pending: resolve()
    return judged, bad, len(truth)

print("\n-- simulation: judgements (Pico logged), judged on an echo, physical taps")
for ri, (a, b) in enumerate(rounds):
    rt = [t for t in taps if a <= t["rx"] <= b and t["peak"] is not None]
    fb = [e for e in ev if e[1] == "GAME_FEEDBACK" and a <= e[0] <= b]
    print("round %d (Pico judged %d):" % (ri + 1, len(fb)))
    for collect, memory in ((20, 1), (20, 8), (100, 1), (100, 8), (150, 8), (200, 8)):
        j, bad, phys = simulate(rt, collect, memory)
        print("   collect %3d ms, remember %d groups: judged %d, on an echo %d, physical %d, extra %d" % (collect, memory, j, bad, phys, j - phys))
