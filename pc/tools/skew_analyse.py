#!/usr/bin/env python3
"""Two-tile skew analyser for the T-B4-03 GPIO-pulse rig.

Reads a logic analyser CSV export (Saleae Logic 2 digital "Export Raw Data"
CSV, or sigrok-cli / PulseView CSV), extracts edges on two named channels,
pairs them by tick, writes a CSV of the pairs and prints the T-B4-03 verdict.

Plan constants (SE15_Implementation_Plan.md, row T-B4-03, acceptance test):
  "maximum absolute skew over all tick pairs at most 5 ms (SE15 target,
   W6D-1c) and no pair at or above 16.67 ms (NFR2 hard limit); median, p99
   and max reported. Pass = both."
Tick period: 1,000,000 / 60 us (T-B5-01 and T-B4-03 rows).

Pairing: each edge on channel A is paired with the nearest edge on channel B
when the two edges are each other's nearest neighbour and lie strictly less
than half a tick period apart. An edge with no partner is reported as
UNPAIRED with its channel and estimated tick, and it never shifts the pairing
of later edges, because every pair is decided by time, not by list position.

Limit of edge timing: with a half-tick pairing window no pair can show an
absolute skew of half a tick or more, so a whole-tick slip between the tiles
looks aligned. The 16.67 ms condition is still applied to every pair as the
plan states; the tick identity check is the slow-motion video of the T-B5-01
moving pixel (see docs/week10/skew_2tile/RIG.md).

Edge modes: 'rising' (default, the T-B4-03 wording) uses rising edges only.
'both' treats every edge as a tick and is meant only for a pin that toggles
each tick (T-B5-01 wording); there, a pair of opposite polarity means the two
pins started at different levels and is reported as a NOTE, not a failure.
Do not use 'both' on a short-pulse pin: a falling edge on one tile can then
pair with a rising edge on the other.

Statistics on absolute skew:
  median: statistics.median (mean of the two middle values for an even count);
  p99: nearest-rank method, the value at rank ceil(0.99 x N) of the sorted
       list (1-based), no interpolation;
  max: largest absolute skew.

Exit codes: 0 PASS, 1 FAIL, 2 bad input.

Standard library only.
"""

import argparse
import bisect
import csv
import math
import re
import statistics
import sys

# Plan constants (T-B4-03 row). Units: microseconds.
TICK_PERIOD_US = 1_000_000 / 60          # 1,000,000 / 60 us per tick
SKEW_TARGET_US = 5_000.0                 # "at most 5 ms" (W6D-1c)
SKEW_HARD_LIMIT_US = 16_670.0            # "no pair at or above 16.67 ms" (NFR2)
PAIR_WINDOW_US = TICK_PERIOD_US / 2      # half a tick period

EXIT_PASS = 0
EXIT_FAIL = 1
EXIT_BAD_INPUT = 2

# sigrok CSV time column labels (libsigrok src/output/csv.c, xlabels[]):
# index i means the time column is in units of 1 / 1000**i seconds.
SIGROK_XLABELS = ["samples", "milliseconds", "microseconds", "nanoseconds",
                  "picoseconds", "femtoseconds", "attoseconds"]
SI_PREFIX = {"": 1, "k": 10**3, "M": 10**6, "G": 10**9, "T": 10**12,
             "P": 10**15, "E": 10**18}
SAMPLERATE_RE = re.compile(r"Samplerate:\s*([0-9]+(?:\.[0-9]+)?)\s*([kMGTPE]?)Hz")
CHANNELS_RE = re.compile(r"Channels\s*\(\d+/\d+\):\s*(.*)$")


class BadInput(Exception):
    """The capture cannot be read or does not contain what was asked for."""


def parse_samplerate(text):
    """Parse '1 MHz', '24MHz', '500 kHz' or a plain number of Hz."""
    text = text.strip()
    m = re.fullmatch(r"([0-9]+(?:\.[0-9]+)?)\s*([kMGTPE]?)(?:Hz)?", text)
    if not m:
        raise BadInput(f"cannot parse sample rate '{text}'")
    return float(m.group(1)) * SI_PREFIX[m.group(2)]


def detect_format(lines):
    """Return 'saleae' or 'sigrok' from the first meaningful lines."""
    for line in lines:
        s = line.strip()
        if not s:
            continue
        if s.startswith(";"):
            return "sigrok"
        first = s.split(",")[0].strip()
        if first.replace(" ", "") == "Time[s]":
            return "saleae"
        return "sigrok"
    raise BadInput("empty capture file")


def _value(cell):
    cell = cell.strip()
    if cell == "1":
        return 1
    if cell == "0":
        return 0
    return None  # 'X' (Saleae: no data) or anything unexpected: unknown


def _column_index(header, wanted, fallback_names=None):
    """Find a channel column by its exact header name or by a fallback list."""
    names = [h.strip() for h in header]
    if wanted in names:
        return names.index(wanted)
    if fallback_names and wanted in fallback_names:
        return fallback_names.index(wanted)
    raise BadInput(f"channel '{wanted}' not found; columns are {names}"
                   + (f", sigrok channels {fallback_names}" if fallback_names else ""))


def read_saleae(lines, ch_a, ch_b):
    """Saleae Logic 2 digital CSV: 'Time [s],<ch>,<ch>...', one row per time
    point where at least one channel changes; the first row is the initial
    state. Values 0, 1, or X for no data."""
    rows = csv.reader(l for l in lines if l.strip())
    header = next(rows, None)
    if not header:
        raise BadInput("Saleae CSV has no header")
    ia = _column_index(header, ch_a)
    ib = _column_index(header, ch_b)
    times, va, vb = [], [], []
    for n, row in enumerate(rows, start=2):
        if len(row) <= max(ia, ib):
            raise BadInput(f"Saleae CSV row {n} has too few columns")
        try:
            t_us = float(row[0]) * 1e6
        except ValueError:
            raise BadInput(f"Saleae CSV row {n}: bad time '{row[0]}'")
        times.append(t_us)
        va.append(_value(row[ia]))
        vb.append(_value(row[ib]))
    return times, va, vb


def read_sigrok(lines, ch_a, ch_b, samplerate_hz=None):
    """sigrok CSV output module: ';' comment lines (with '; Samplerate: ...'
    and '; Channels (n/m): ...' when header=true), one label line, then one
    row per sample (or per change with dedup). The time column exists only
    with time=true; its label is 'Time' (label=channel) or a unit name
    (label=units, the default, which also labels every logic column 'logic')."""
    comment_rate = None
    sigrok_channels = None
    body = []
    for line in lines:
        s = line.strip()
        if not s:
            continue  # blank frame separators
        if s.startswith(";"):
            m = SAMPLERATE_RE.search(s)
            if m:
                comment_rate = float(m.group(1)) * SI_PREFIX[m.group(2)]
            m = CHANNELS_RE.search(s)
            if m:
                sigrok_channels = [c.strip() for c in m.group(1).split(",") if c.strip()]
            continue
        body.append(s)
    if not body:
        raise BadInput("sigrok CSV has no label line or data")
    rate = samplerate_hz if samplerate_hz else comment_rate
    rows = csv.reader(body)
    header = [h.strip() for h in next(rows)]

    first = header[0]
    if first == "Time":
        has_time, scale_exp = True, None
    elif first in SIGROK_XLABELS:
        has_time, scale_exp = True, SIGROK_XLABELS.index(first)
    else:
        has_time, scale_exp = False, None

    chan_header = header[1:] if has_time else header
    fallback = None
    if sigrok_channels and all(h == "logic" for h in chan_header):
        fallback = sigrok_channels[:len(chan_header)]
    ia = _column_index(chan_header, ch_a, fallback) + (1 if has_time else 0)
    ib = _column_index(chan_header, ch_b, fallback) + (1 if has_time else 0)

    if has_time and scale_exp is None:
        # 'Time' label: libsigrok picks the smallest 1000**i that is >= rate.
        if not rate:
            raise BadInput("sigrok CSV has a 'Time' column but no sample rate; "
                           "pass --samplerate")
        scale_exp, scale = 0, 1
        while scale < rate:
            scale_exp += 1
            scale *= 1000
    if not has_time and not rate:
        raise BadInput("sigrok CSV has no time column and no sample rate; "
                       "pass --samplerate")

    unit_us = 1e6 / (1000 ** scale_exp) if has_time else None
    times, va, vb = [], [], []
    for n, row in enumerate(rows):
        if len(row) <= max(ia, ib):
            raise BadInput(f"sigrok CSV data row {n + 1} has too few columns")
        if has_time:
            try:
                t_us = int(row[0]) * unit_us
            except ValueError:
                raise BadInput(f"sigrok CSV data row {n + 1}: bad time '{row[0]}'")
        else:
            t_us = n * 1e6 / rate
        times.append(t_us)
        va.append(_value(row[ia]))
        vb.append(_value(row[ib]))
    return times, va, vb


def extract_edges(times, values, edges="rising"):
    """Return [(time_us, polarity)] for edges between consecutive known states.
    The first state is the starting level, never an edge, so a capture that
    starts with the pin already high gives no rising edge at its start. A
    transition into or out of an unknown state ('X') is not an edge."""
    out = []
    prev = None
    for t, v in zip(times, values):
        if v is None:
            prev = None
            continue
        if prev is not None and v != prev:
            if v == 1:
                out.append((t, 1))
            elif edges == "both":
                out.append((t, 0))
        prev = v
    return out


def _nearest(sorted_times, t):
    """Index of the element of sorted_times nearest to t, or None."""
    if not sorted_times:
        return None
    k = bisect.bisect_left(sorted_times, t)
    best = None
    for j in (k - 1, k):
        if 0 <= j < len(sorted_times):
            if best is None or abs(sorted_times[j] - t) < abs(sorted_times[best] - t):
                best = j
    return best


def pair_edges(edges_a, edges_b, window_us=PAIR_WINDOW_US):
    """Mutual-nearest-neighbour pairing within +/- window_us (strict).
    Returns (pairs, unpaired_a, unpaired_b); pairs are (t_a, t_b, pol_a, pol_b)."""
    ta = [e[0] for e in edges_a]
    tb = [e[0] for e in edges_b]
    pairs, used_b = [], set()
    paired_a = set()
    for i, t in enumerate(ta):
        j = _nearest(tb, t)
        if j is None or abs(tb[j] - t) >= window_us:
            continue
        if _nearest(ta, tb[j]) != i:
            continue
        pairs.append((t, tb[j], edges_a[i][1], edges_b[j][1]))
        paired_a.add(i)
        used_b.add(j)
    unpaired_a = [edges_a[i] for i in range(len(ta)) if i not in paired_a]
    unpaired_b = [edges_b[j] for j in range(len(tb)) if j not in used_b]
    return pairs, unpaired_a, unpaired_b


def p99_nearest_rank(sorted_values):
    rank = math.ceil(0.99 * len(sorted_values))
    return sorted_values[max(rank, 1) - 1]


def analyse(times, va, vb, edges="rising", window_us=PAIR_WINDOW_US):
    ea = extract_edges(times, va, edges)
    eb = extract_edges(times, vb, edges)
    pairs, ua, ub = pair_edges(ea, eb, window_us)
    all_t = [e[0] for e in ea] + [e[0] for e in eb]
    t0 = min(all_t) if all_t else 0.0

    def tick(t):
        return int(round((t - t0) / TICK_PERIOD_US))

    rows = []
    for t_a, t_b, pa, pb in pairs:
        rows.append({"tick": tick((t_a + t_b) / 2), "t_a_us": t_a, "t_b_us": t_b,
                     "skew_us": t_b - t_a, "pol_a": pa, "pol_b": pb})
    abs_skews = sorted(abs(r["skew_us"]) for r in rows)
    result = {
        "edges_a": len(ea), "edges_b": len(eb), "pairs": rows,
        "unpaired": [("A", t, tick(t)) for t, _ in ua] + [("B", t, tick(t)) for t, _ in ub],
        "polarity_mismatch": sum(1 for r in rows if r["pol_a"] != r["pol_b"]),
    }
    if abs_skews:
        result["median_us"] = statistics.median(abs_skews)
        result["p99_us"] = p99_nearest_rank(abs_skews)
        result["max_us"] = abs_skews[-1]
        result["over_hard_limit"] = sum(1 for s in abs_skews if s >= SKEW_HARD_LIMIT_US)
        result["passed"] = (result["max_us"] <= SKEW_TARGET_US
                            and result["over_hard_limit"] == 0)
    else:
        result["passed"] = False
    return result


def write_pairs_csv(path, rows):
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["tick", "t_a_s", "t_b_s", "skew_ms"])
        for r in rows:
            w.writerow([r["tick"], f"{r['t_a_us'] / 1e6:.9f}",
                        f"{r['t_b_us'] / 1e6:.9f}", f"{r['skew_us'] / 1e3:.6f}"])


def write_plot(path, rows):
    """Skew against tick with the 5 ms target lines. Optional: matplotlib is
    the only non standard library import and is loaded only here."""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    fig, ax = plt.subplots(figsize=(10, 4))
    ax.plot([r["tick"] for r in rows], [r["skew_us"] / 1e3 for r in rows], ".", ms=2)
    for y in (SKEW_TARGET_US / 1e3, -SKEW_TARGET_US / 1e3):
        ax.axhline(y, linestyle="--", linewidth=1)
    ax.set_xlabel("tick")
    ax.set_ylabel("skew t_b - t_a (ms)")
    ax.set_title("T-B4-03 two-tile skew (dashed: 5 ms target)")
    fig.tight_layout()
    fig.savefig(path, dpi=120)


def main(argv=None):
    ap = argparse.ArgumentParser(description="T-B4-03 two-tile skew analyser")
    ap.add_argument("capture", help="analyser CSV export")
    ap.add_argument("--ch-a", required=True, help="channel name of tile A test pin")
    ap.add_argument("--ch-b", required=True, help="channel name of tile B test pin")
    ap.add_argument("--format", choices=["auto", "saleae", "sigrok"], default="auto")
    ap.add_argument("--samplerate", help="sigrok sample rate (e.g. '1 MHz' or 1000000) "
                    "when the CSV header comment lacks it; overrides the comment")
    ap.add_argument("--edges", choices=["rising", "both"], default="rising",
                    help="rising (T-B4-03 wording) or both (each edge of a toggling pin is a tick)")
    ap.add_argument("--out", default="skew_pairs.csv", help="pairs CSV to write")
    ap.add_argument("--plot", help="optional PNG of skew against tick (needs matplotlib)")
    args = ap.parse_args(argv)

    try:
        with open(args.capture, newline="") as f:
            lines = f.read().splitlines()
        fmt = detect_format(lines) if args.format == "auto" else args.format
        rate = parse_samplerate(args.samplerate) if args.samplerate else None
        if fmt == "saleae":
            times, va, vb = read_saleae(lines, args.ch_a, args.ch_b)
        else:
            times, va, vb = read_sigrok(lines, args.ch_a, args.ch_b, rate)
        if not times:
            raise BadInput("capture holds no data rows")
        res = analyse(times, va, vb, args.edges)
        if not res["pairs"]:
            raise BadInput(f"no edge pairs found (edges A={res['edges_a']}, "
                           f"B={res['edges_b']}); check channel names and --edges")
    except (OSError, BadInput) as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return EXIT_BAD_INPUT

    write_pairs_csv(args.out, res["pairs"])
    if args.plot:
        write_plot(args.plot, res["pairs"])
    for ch, t, k in sorted(res["unpaired"], key=lambda u: u[1]):
        name = args.ch_a if ch == "A" else args.ch_b
        print(f"UNPAIRED channel={name} tick={k} t={t / 1e6:.9f} s")
    print(f"format={fmt} edges={args.edges} edges_a={res['edges_a']} "
          f"edges_b={res['edges_b']} pairs={len(res['pairs'])} "
          f"unpaired={len(res['unpaired'])}")
    if args.edges == "both" and res["polarity_mismatch"]:
        print(f"NOTE: {res['polarity_mismatch']} pairs join a rising edge to a falling edge")
    print(f"median_abs_skew_ms={res['median_us'] / 1e3:.3f} "
          f"p99_abs_skew_ms={res['p99_us'] / 1e3:.3f} (nearest rank) "
          f"max_abs_skew_ms={res['max_us'] / 1e3:.3f}")
    print(f"pairs_at_or_above_16.67ms={res['over_hard_limit']}")
    print(f"pairs_csv={args.out}")
    if res["passed"]:
        print("VERDICT: PASS (max absolute skew <= 5 ms and no pair >= 16.67 ms)")
        return EXIT_PASS
    print("VERDICT: FAIL (needs max absolute skew <= 5 ms and no pair >= 16.67 ms)")
    return EXIT_FAIL


if __name__ == "__main__":
    sys.exit(main())
