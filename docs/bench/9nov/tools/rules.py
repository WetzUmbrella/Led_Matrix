"""DS-03: replay real taps under candidate neighbour rules (F-05 box, F-10 sponge).

Usage: python rules.py <taps.csv> 4=<tile4.log> 5=<tile5.log>
For every soft (c) or palm (e) event: the tapped tile's peak and the other
tile's peak (1.2 s window). "Child hits the mole": the tapped tile is the
target. "Child hits the tile next to it": the other tile is the target.
"""
import sys
from common import load, events, peak

TH = 200
csvp, logs = sys.argv[1], dict(a.split("=") for a in sys.argv[2:])
tiles = {k: load(v)[0] for k, v in logs.items()}
rows = []
for t, a, kind in events(csvp):
    if kind not in "ce":
        continue
    b = [x for x in tiles if x != a][0]
    rows.append((kind, peak(tiles[a], t, t + 1200), peak(tiles[b], t, t + 1200)))


def rule(k):
    def hit(target, other):
        return target >= TH and (other < TH or target >= k * other)
    return hit


print("event mix: %d soft, %d palm" % (sum(r[0] == "c" for r in rows), sum(r[0] == "e" for r in rows)))
print("%-28s %-26s %-26s" % ("rule", "child hits the mole", "child hits the tile NEXT to it"))
for name, k in (("target wins (any echo)", 0.0), ("target >= 0.3 x other", 0.3),
                ("target >= 0.5 x other", 0.5), ("highest peak wins", 1.0)):
    h = rule(k)
    for kind, label in (("c", "soft"), ("e", "palm")):
        sub = [r for r in rows if r[0] == kind]
        hits = sum(h(p_own, p_echo) for _, p_own, p_echo in sub)
        free = sum(h(p_echo, p_own) for _, p_own, p_echo in sub)
        print("%-28s %-26s %-26s" % (name + " " + label, "scored %d/%d" % (hits, len(sub)), "FREE hit %d/%d" % (free, len(sub))))
