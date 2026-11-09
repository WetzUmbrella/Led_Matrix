"""F-05: per-event peaks from tap logger logs, aligned on PC receive times."""
import sys, statistics as st
def ms(hms):
    h, m, s = hms.split(":"); return (int(h) * 3600 + int(m) * 60 + float(s)) * 1000.0
def load(path):
    rows = []
    for line in open(path, errors="replace"):
        p = line.split()
        if len(p) >= 2 and p[1].startswith("T,"):
            _, t, pk = p[1].split(","); rows.append((ms(p[0]), int(t), int(pk)))
    off = min(pc - t for pc, t, _ in rows)          # least-latency line sets the offset
    return [(t + off, pk) for _, t, pk in rows]
csv_path, logs = sys.argv[1], dict(a.split("=") for a in sys.argv[2:])
tiles = {k: load(v) for k, v in logs.items()}
events = []
for line in open(csv_path):
    if line.startswith("#") or line.startswith("wall"): continue
    w, tile, kind = line.strip().split(","); events.append((ms(w), tile, kind))
def peak(tile, t0, t1):
    return max([pk for t, pk in tiles[tile] if t0 <= t < t1] or [0])
WIN = (0.0, 1200.0)   # reaction time after the prompt
res = {}
for t, tile, kind in events:
    for tt in tiles:
        role = "self" if tt == tile else ("knock" if tile == "board" else "nbr")
        res.setdefault((tt, kind, role), []).append(peak(tt, t + WIN[0], t + WIN[1]))
last = max(t for t, _, _ in events)
for tt in sorted(tiles):
    q = sorted(pk for t, pk in tiles[tt] if last + 3000 <= t < last + 30000)
    print("tile %s" % tt)
    for kind, name in (("c", "soft taps"), ("e", "palm slaps")):
        v = sorted(res[(tt, kind, "self")])
        print("  own %-10s n=%2d min %5d  p10 %5d  median %5d  max %5d mg" % (name, len(v), v[0], v[int(len(v) * 0.1)], st.median(v), v[-1]))
    for kind, name in (("c", "soft"), ("e", "palm")):
        v = sorted(res[(tt, kind, "nbr")])
        print("  when the OTHER tile got %-4s: median %5d  max %5d mg" % (name, st.median(v), v[-1]))
    v = sorted(res[(tt, "k", "knock")])
    print("  knocks on the surface      : median %5d  max %5d mg" % (st.median(v), v[-1]))
    print("  quiet 27 s                 : p99 %5d  max %5d mg (n=%d windows)" % (q[int(len(q) * 0.99)], q[-1], len(q)))
    print("  soft taps per threshold    : " + "  ".join("%d mg:%d/20" % (th, sum(x >= th for x in res[(tt, "c", "self")])) for th in (100, 150, 200, 300, 500)))

def first_over(tile, t0, t1, th):
    for t, pk in tiles[tile]:
        if t0 <= t < t1 and pk >= th: return t
    return None
print("\nper tap: does the rule pick the tapped tile? (other tile counted only if it also crossed 300 mg)")
for kind, name in (("c", "soft"), ("e", "palm")):
    peak_ok = peak_bad = first_ok = first_bad = tie = alone = 0; leads = []
    for t, tile, k in events:
        if k != kind: continue
        other = [x for x in tiles if x != tile][0]
        a, b = peak(tile, t, t + 1200), peak(other, t, t + 1200)
        if b < 300: alone += 1; continue
        peak_ok += a > b; peak_bad += a <= b
        fa, fb = first_over(tile, t, t + 1200, 300), first_over(other, t, t + 1200, 300)
        if fa is None: first_bad += 1; continue
        d = fb - fa; leads.append(d)
        if d > 0: first_ok += 1
        elif d < 0: first_bad += 1
        else: tie += 1
    print("  %s: other tile quiet %d | both fired %d -> highest peak right %d wrong %d | first-over-300mg right %d wrong %d same-10ms-window %d | lead ms %s"
          % (name, alone, peak_ok + peak_bad, peak_ok, peak_bad, first_ok, first_bad, tie, sorted(int(x) for x in leads)))
