#!/usr/bin/env python3
"""Summarise an imu_tap_log log file.

Usage: python summarise.py <log file>

Reads the T (tap peak) and MARK (typed key) lines, ignores everything else.
Splits the log into segments: the "before first MARK" segment (if it has T
lines) followed by one segment per MARK line, each labelled by that MARK's
character. For each segment prints: label, number of T lines, max peak and
the 99th-percentile peak in mg (nearest-rank).

Exits 1 when the file has no T lines.
"""

import math
import re
import sys

LINE_T = re.compile(r"^T,([0-9]+),([0-9]+)\r?\n?$")
LINE_MARK = re.compile(r"^MARK,[0-9]+,(.)\r?\n?$")

LABEL_BEFORE = "before first MARK"
PCT = 99  # percentile reported, in percent


def percentile_nearest_rank(sorted_vals, pct):
    """Nearest-rank percentile: 1-based rank = ceil(pct/100 * n)."""
    n = len(sorted_vals)
    rank = int(math.ceil((pct / 100.0) * n))
    if rank < 1:
        rank = 1
    if rank > n:
        rank = n
    return sorted_vals[rank - 1]


def summarise(path):
    segments = []  # (label, [peaks])
    cur_label = LABEL_BEFORE
    cur_peaks = []
    total_t = 0

    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m_mark = LINE_MARK.match(line)
            if m_mark:
                if cur_peaks:
                    segments.append((cur_label, cur_peaks))
                cur_label = m_mark.group(1)
                cur_peaks = []
                continue

            m_t = LINE_T.match(line)
            if m_t:
                cur_peaks.append(int(m_t.group(2)))
                total_t += 1

    if cur_peaks:
        segments.append((cur_label, cur_peaks))

    if total_t == 0:
        return 1

    for label, peaks in segments:
        peaks_sorted = sorted(peaks)
        mx = peaks_sorted[-1]
        p99 = percentile_nearest_rank(peaks_sorted, PCT)
        print("{}: count={} max={} p99={}".format(
            label, len(peaks), mx, p99))

    return 0


def main(argv):
    if len(argv) != 2:
        sys.stderr.write("usage: python summarise.py <log file>\n")
        return 2
    return summarise(argv[1])


if __name__ == "__main__":
    sys.exit(main(sys.argv))
