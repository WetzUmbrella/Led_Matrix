"""Shared helpers for the 9 Nov bench analyses: PC time parsing and log loading.

Each tap logger log line is "<PC HH:MM:SS.mmm> <payload>". Tile windows are
mapped to PC time with the least-latency offset (min of PC time - tile ms),
good to about 100 ms between tiles: enough for peaks, not for ordering.
"""


def ms(hms):
    h, m, s = hms.split(":")
    return (int(h) * 3600 + int(m) * 60 + float(s)) * 1000.0


def load(path):
    """T windows as (pc_ms, peak_mg) and G windows as (pc_ms, gz, gxy, angle_deg)."""
    T, G = [], []
    for line in open(path, errors="replace"):
        p = line.split()
        if len(p) < 2:
            continue
        f = p[1].split(",")
        if f[0] == "T" and len(f) == 3:
            T.append((ms(p[0]), int(f[1]), int(f[2])))
        elif f[0] == "G" and len(f) == 5:
            G.append((int(f[1]), int(f[2]), int(f[3]), int(f[4]) / 100.0))
    off = min(pc - t for pc, t, _ in T)
    return [(t + off, pk) for _, t, pk in T], [(t + off, gz, gxy, a) for t, gz, gxy, a in G]


def events(csv_path):
    out = []
    for line in open(csv_path):
        if line[0] in "#w":
            continue
        w, tile, kind = line.strip().split(",")
        out.append((ms(w), tile, kind))
    return out


def peak(T, t0, t1):
    return max([pk for t, pk in T if t0 <= t < t1] or [0])
