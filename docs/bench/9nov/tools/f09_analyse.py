"""F-09: gyroscope around taps, slaps and turns (tap logger with G lines).

Usage: python f09_analyse.py <f09_taps.csv> 4=<f09_gyro_4.log> 5=<f09_gyro_5.log>
"""
import sys
import statistics as st
from common import load, events

csvp, logs = sys.argv[1], dict(a.split("=") for a in sys.argv[2:])
tiles = {k: load(v) for k, v in logs.items()}
ev = events(csvp)
TH = 200
out = {}
for t, tile, kind in ev:
    for tt, (T, G) in tiles.items():
        role = "own" if tt == tile else "nbr"
        span = 2500 if kind == "b" else 1200
        cross = next((x for x, pk in T if t <= x < t + span and pk >= TH), None)
        if cross is None:
            continue
        near = [gz for x, gz, _, _ in G if cross - 20 <= x < cross + 40]
        allgz = [gz for x, gz, _, _ in G if t <= x < t + span]
        ang = abs(sum(a for x, _, _, a in G if t <= x < t + span))
        out.setdefault((kind, role), []).append((max(near or [0]), max(allgz or [0]), ang))
names = {"c": "soft tap", "e": "palm slap", "b": "turn"}
for kind in "ceb":
    for role in ("own", "nbr"):
        v = out.get((kind, role))
        if not v:
            print("%-9s %s: never crossed %d mg" % (names[kind], role, TH))
            continue
        near = sorted(a for a, _, _ in v); pk = sorted(b for _, b, _ in v); ang = sorted(c for _, _, c in v)
        print("%-9s %-3s n=%2d | gz within 40 ms of the spike: median %4d max %4d dps | peak gz: median %4d max %4d | angle: median %5.1f max %5.1f deg"
              % (names[kind], role, len(v), st.median(near), near[-1], st.median(pk), pk[-1], st.median(ang), ang[-1]))
print()
for th in (20, 30, 50, 80):
    taps = [a for k in "ce" for r in ("own", "nbr") for a, _, _ in out.get((k, r), [])]
    turns = [a for a, _, _ in out.get(("b", "own"), [])]
    print("rule 'tap only if gz < %2d dps within 40 ms': taps+slaps kept %d/%d, turns rejected %d/%d"
          % (th, sum(a < th for a in taps), len(taps), sum(a >= th for a in turns), len(turns)))


def own_events():
    for t, tile, kind in ev:
        T, G = tiles[tile]
        span = 2500 if kind == "b" else 1200
        cross = next((x for x, pk in T if t <= x < t + span and pk >= TH), None)
        if cross is not None:
            yield t, kind, cross, G


print("angle turned from the spike to N ms after it (|sum gz dt|):")
for n in (30, 50, 80, 120, 200):
    taps, turns = [], []
    for _, kind, cross, G in own_events():
        a = abs(sum(g for x, _, _, g in G if cross - 10 <= x < cross + n))
        (turns if kind == "b" else taps).append(a)
    taps.sort(); turns.sort()
    print("  %3d ms: taps+slaps max %5.1f deg (p95 %5.1f) | turns min %5.1f median %5.1f deg"
          % (n, taps[-1], taps[int(len(taps) * 0.95)], turns[0], st.median(turns)))
print("rule 'tap if under 8 deg turned by N ms after the spike':")
for n in (50, 80, 120):
    taps = turns = tk = tr = 0
    for _, kind, cross, G in own_events():
        a = abs(sum(g for x, _, _, g in G if cross - 10 <= x < cross + n))
        if kind == "b":
            turns += 1; tr += a >= 8
        else:
            taps += 1; tk += a < 8
    print("  %3d ms: taps+slaps kept %d/%d, turns rejected %d/%d" % (n, tk, taps, tr, turns))
lead = []
for t, tile, kind in ev:
    if kind != "b":
        continue
    T, G = tiles[tile]
    cross = next((x for x, pk in T if t <= x < t + 2500 and pk >= TH), None)
    run, start = 0.0, None
    for x, _, _, g in G:
        if x < t:
            continue
        run += g
        if abs(run) >= 8:
            start = x
            break
    if cross is not None and start is not None:
        lead.append(int(start - cross))
print("turns: ms from the first 200 mg spike to 8 deg turned:", sorted(lead))
