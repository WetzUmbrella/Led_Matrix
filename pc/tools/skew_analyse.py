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

Limit of edge timing (default mode): pairing within half a tick measures
skew modulo one tick. A true skew of s reads as s minus the nearest whole
multiple of 1,000,000 / 60 us, so any absolute skew within 5 ms of k x 16.67 ms
(for example a true 13 ms reads as 3.667 ms) reads as a pass, and the
"no pair at or above 16.67 ms" test can never fire. Without a tick-identity
reference every verdict therefore carries "TICK IDENTITY NOT CHECKED" and a
PASS is only conditional (exit code 3).

Marker mode (--marker): tick identity from the test pin itself. Under
DE_W10_TEST_PATTERN the pin gives a short pulse at every tick and a wider
marker pulse at every tick n with n mod N == 0 (N and the widths are plan
choices to be agreed with Jia Jun against T-B5-01; see RIG.md). The tool
finds the markers on each channel (high time at or above --marker-min-us),
numbers every rising edge as cycle x N + ticks since the preceding marker,
matches the first marker of tile B to the nearest marker of tile A (so the
two tiles must be within N/2 ticks of each other), and pairs edges by equal
tick number. The skew is then the true skew of any size, so a whole-tick
slip and any skew at or above 16.67 ms fail.

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

Exit codes: 0 PASS (marker mode, tick identity checked), 1 FAIL, 2 bad input,
3 conditional PASS (skew modulo one tick passes; tick identity not checked).

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
EXIT_PASS_CONDITIONAL = 3

# Marker mode defaults (plan choice, to be agreed with Jia Jun against
# T-B5-01): marker every N = 64 ticks, matching the n mod 64 moving pixel of
# DE_W10_TEST_PATTERN; a pulse whose high time is at least 2.5 ms is a marker
# (halfway between the proposed 1 ms tick pulse and 4 ms marker pulse).
MARKER_PERIOD_TICKS = 64
MARKER_MIN_US = 2_500.0
NO_TICK_ID = ("TICK IDENTITY NOT CHECKED: skew measured modulo one tick "
              "(1,000,000 / 60 us); a skew within 5 ms of a whole number of "
              "ticks reads as a pass; run with --marker for tick identity")

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


def summarise(rows, result):
    abs_skews = sorted(abs(r["skew_us"]) for r in rows)
    result["pairs"] = rows
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


def extract_pulses(times, values):
    """Return [(rise_us, high_us or None)] for every rising edge; high_us is
    None when the pulse has not ended before the capture ends or goes unknown."""
    out = []
    prev = None
    for t, v in zip(times, values):
        if v is None:
            prev = None  # an open pulse keeps width None
            continue
        if prev is not None and v != prev:
            if v == 1:
                out.append([t, None])
            elif out and out[-1][1] is None:
                out[-1][1] = t - out[-1][0]
        prev = v
    return [tuple(p) for p in out]


def number_ticks(pulses, period_ticks, marker_min_us, name):
    """Give every rising edge a tick number from the markers on its channel.
    Returns (labels, marker_times, marker_cycles)."""
    markers = [t for t, w in pulses if w is not None and w >= marker_min_us]
    if not markers:
        raise BadInput(f"marker mode: no marker pulse (high >= {marker_min_us:.0f} us) "
                       f"on channel {name}")
    cycle_us = period_ticks * TICK_PERIOD_US
    cycles = [0]
    for prev, cur in zip(markers, markers[1:]):
        steps = round((cur - prev) / cycle_us)
        if steps < 1 or abs((cur - prev) - steps * cycle_us) > TICK_PERIOD_US / 2:
            raise BadInput(f"marker mode: markers on channel {name} at "
                           f"{prev / 1e6:.6f} s and {cur / 1e6:.6f} s are not a whole "
                           f"number of {period_ticks}-tick cycles apart")
        cycles.append(cycles[-1] + steps)
    labels = []
    for t, _ in pulses:
        k = bisect.bisect_right(markers, t) - 1
        k = max(k, 0)
        labels.append(cycles[k] * period_ticks
                      + int(round((t - markers[k]) / TICK_PERIOD_US)))
    return labels, markers, cycles


def analyse_marker(times, va, vb, period_ticks=MARKER_PERIOD_TICKS,
                   marker_min_us=MARKER_MIN_US):
    pa = extract_pulses(times, va)
    pb = extract_pulses(times, vb)
    la, ma, ca = number_ticks(pa, period_ticks, marker_min_us, "A")
    lb, mb, cb = number_ticks(pb, period_ticks, marker_min_us, "B")
    j = _nearest(ma, mb[0])
    if abs(ma[j] - mb[0]) >= period_ticks * TICK_PERIOD_US / 2:
        raise BadInput("marker mode: first marker on B is not within half a marker "
                       "cycle of any marker on A")
    shift = (ca[j] - cb[0]) * period_ticks
    lb = [l + shift for l in lb]
    by_a, by_b = {}, {}
    dup = []
    for (t, _), l in zip(pa, la):
        if l in by_a:
            dup.append(("A", t, l))
        else:
            by_a[l] = t
    for (t, _), l in zip(pb, lb):
        if l in by_b:
            dup.append(("B", t, l))
        else:
            by_b[l] = t
    rows = [{"tick": l, "t_a_us": by_a[l], "t_b_us": by_b[l],
             "skew_us": by_b[l] - by_a[l], "pol_a": 1, "pol_b": 1}
            for l in sorted(set(by_a) & set(by_b))]
    unpaired = ([("A", by_a[l], l) for l in sorted(set(by_a) - set(by_b))]
                + [("B", by_b[l], l) for l in sorted(set(by_b) - set(by_a))] + dup)
    result = {"edges_a": len(pa), "edges_b": len(pb), "unpaired": unpaired,
              "polarity_mismatch": 0, "markers_a": len(ma), "markers_b": len(mb)}
    return summarise(rows, result)


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
    result = {
        "edges_a": len(ea), "edges_b": len(eb),
        "unpaired": [("A", t, tick(t)) for t, _ in ua] + [("B", t, tick(t)) for t, _ in ub],
        "polarity_mismatch": sum(1 for r in rows if r["pol_a"] != r["pol_b"]),
    }
    return summarise(rows, result)


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
    ap.add_argument("--marker", action="store_true",
                    help="tick identity from marker pulses (pulse pins only, rising edges)")
    ap.add_argument("--marker-period", type=int, default=MARKER_PERIOD_TICKS,
                    help="ticks between markers (default 64, plan choice)")
    ap.add_argument("--marker-min-us", type=float, default=MARKER_MIN_US,
                    help="minimum high time of a marker pulse in us (default 2500, plan choice)")
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
        if args.marker:
            if args.edges != "rising":
                raise BadInput("--marker needs --edges rising (pulse pins)")
            res = analyse_marker(times, va, vb, args.marker_period, args.marker_min_us)
        else:
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
    mode = "marker" if args.marker else "edge-timing"
    print(f"format={fmt} mode={mode} edges={args.edges} edges_a={res['edges_a']} "
          f"edges_b={res['edges_b']} pairs={len(res['pairs'])} "
          f"unpaired={len(res['unpaired'])}")
    if args.edges == "both" and res["polarity_mismatch"]:
        print(f"NOTE: {res['polarity_mismatch']} pairs join a rising edge to a falling edge")
    print(f"median_abs_skew_ms={res['median_us'] / 1e3:.3f} "
          f"p99_abs_skew_ms={res['p99_us'] / 1e3:.3f} (nearest rank) "
          f"max_abs_skew_ms={res['max_us'] / 1e3:.3f}")
    print(f"pairs_at_or_above_16.67ms={res['over_hard_limit']}")
    print(f"pairs_csv={args.out}")
    if args.marker:
        print(f"TICK IDENTITY CHECKED: markers_a={res['markers_a']} "
              f"markers_b={res['markers_b']} every {args.marker_period} ticks")
        if res["passed"]:
            print("VERDICT: PASS (max absolute skew <= 5 ms and no pair >= 16.67 ms)")
            return EXIT_PASS
        print("VERDICT: FAIL (needs max absolute skew <= 5 ms and no pair >= 16.67 ms)")
        return EXIT_FAIL
    if res["passed"]:
        print("VERDICT: CONDITIONAL PASS (skew modulo one tick: max absolute skew <= 5 ms); "
              + NO_TICK_ID)
        return EXIT_PASS_CONDITIONAL
    print("VERDICT: FAIL (needs max absolute skew <= 5 ms and no pair >= 16.67 ms); "
          + NO_TICK_ID)
    return EXIT_FAIL


if __name__ == "__main__":
    sys.exit(main())
