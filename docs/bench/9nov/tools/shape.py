"""DS-02 input: how long each event's shake stays over a level (10 ms windows).

Usage: python shape.py <f05_taps.csv> <f05_tap_4.log> <f05_tap_5.log> <f06_taps.csv> <f06_turn_4.log> <f06_turn_5.log>
"""
import sys
import statistics as st
from common import load, events


def width(rows, t0, t1, level):
    win = [(t, pk) for t, pk in rows if t0 <= t < t1]
    if not win:
        return None
    i = max(range(len(win)), key=lambda k: win[k][1])
    if win[i][1] < level:
        return None
    a = b = i
    while a > 0 and win[a - 1][1] >= level:
        a -= 1
    while b < len(win) - 1 and win[b + 1][1] >= level:
        b += 1
    return (b - a + 1) * 10


a = sys.argv[1:]
runs = (("taps", a[0], {"4": a[1], "5": a[2]}, ("c", "e")), ("turns", a[3], {"4": a[4], "5": a[5]}, ("b",)))
for name, csvp, logs, kinds in runs:
    tiles = {k: load(v)[0] for k, v in logs.items()}
    for kind in kinds:
        for level in (150, 300):
            w = [width(tiles[t], at, at + (2500 if kind == "b" else 1200), level) for at, t, k in events(csvp) if k == kind]
            w = sorted(x for x in w if x is not None)
            print("%-5s %-9s ms over %3d mg: n=%2d  min %3d  median %3d  p90 %3d  max %3d"
                  % (name, {"c": "soft tap", "e": "palm slap", "b": "turn"}[kind], level, len(w), w[0], st.median(w), w[int(len(w) * 0.9)], w[-1]))
