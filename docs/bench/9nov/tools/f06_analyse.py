"""F-06: peaks while a tile is turned, on itself and on the other tile."""
import sys, statistics as st
exec(open(sys.argv[1]).read().split("csv_path, logs")[0])          # reuse ms() and load()
csv_path, logs = sys.argv[2], dict(a.split("=") for a in sys.argv[3:])
tiles = {k: load(v) for k, v in logs.items()}
ev = []
for line in open(csv_path):
    if line[0] in "#w": continue
    w, tile, kind = line.strip().split(","); ev.append((ms(w), tile, kind))
def peak(tile, t0, t1): return max([pk for t, pk in tiles[tile] if t0 <= t < t1] or [0])
def windows_over(tile, t0, t1, th): return sum(1 for t, pk in tiles[tile] if t0 <= t < t1 and pk >= th)
for tile in sorted(tiles):
    other = [x for x in tiles if x != tile][0]
    turns = [t for t, tt, k in ev if tt == tile and k == "b"]
    taps = [t for t, tt, k in ev if tt == tile and k == "c"]
    own = sorted(peak(tile, t, t + 2500) for t in turns)
    nbr = sorted(peak(other, t, t + 2500) for t in turns)
    print("tile %s turned (%d turns): own peak min %d median %d max %d mg | other tile %s: median %d max %d mg"
          % (tile, len(turns), own[0], st.median(own), own[-1], other, st.median(nbr), nbr[-1]))
    print("   own soft alignment taps: %s mg" % [peak(tile, t, t + 1200) for t in taps])
    for th in (300, 1000, 2000, 5000):
        print("   threshold %5d mg: turns that would fire a tap on the turned tile %2d/%d, on the other tile %2d/%d"
              % (th, sum(x >= th for x in own), len(own), sum(x >= th for x in nbr), len(nbr)))
