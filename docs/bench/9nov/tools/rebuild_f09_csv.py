"""Rebuild the lost F-09 CSV (overwritten on 9 Nov by the F-10 run, which used
prompter gyro mode, so f10_taps.csv has the same fixed schedule).

Slides that schedule over the F-09 spikes (>= 150 mg) and keeps the start time
matching the most events. Usage:
python rebuild_f09_csv.py <f10_taps.csv> <f09_gyro_4.log> <f09_gyro_5.log> <out.csv>
"""
import sys
from common import ms, load

tmpl = [l.strip().split(",") for l in open(sys.argv[1]) if l[0] not in "#w"]
t0 = ms(tmpl[0][0])
offs = [(ms(w) - t0, tile, kind) for w, tile, kind in tmpl]
tiles = {"4": load(sys.argv[2])[0], "5": load(sys.argv[3])[0]}
spikes = {k: [t for t, pk in v if pk >= 150] for k, v in tiles.items()}


def score(start):
    return sum(any(start + o <= s < start + o + 1200 for s in spikes[tile]) for o, tile, _ in offs)


lo = min(t for t, _ in tiles["4"]); hi = max(t for t, _ in tiles["4"]) - offs[-1][0]
best = max((score(s), s) for s in range(int(lo), int(hi), 100))
fine = max(((score(s), s) for s in range(best[1] - 300, best[1] + 300, 10)), key=lambda x: (x[0], -abs(x[1] - best[1])))
start = fine[1]
print("matched %d of %d events" % (fine[0], len(offs)))
with open(sys.argv[4], "w") as f:
    f.write("# F-09: c = soft tap, e = palm slap, b = a 90 deg turn (REBUILT 9 Nov: the original was overwritten; schedule of prompter gyro mode aligned to the F-09 spikes)\nwall,tile,kind\n")
    for o, tile, kind in offs:
        t = start + o
        f.write("%02d:%02d:%06.3f,%s,%s\n" % (int(t // 3600000), int(t % 3600000 // 60000), t % 60000 / 1000, tile, kind))
