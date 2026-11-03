#!/usr/bin/env python3
"""Generate the Week 13 data-representation and message-size documents and
fill the "After" column of ``docs/week13/before_after.md`` (T-X-06, T-X-04).

Owner: Benjamin (Buddy 2), tasks T-X-06 and T-X-04 (size table). Implemented
by Ahsanul (B4) for Benjamin, written 30 Oct 2026 in Jia Jun's (B5) session.

Inputs, all in the repository:
  * ``protocol/layout_table.md`` section 7 (the data representation table);
  * ``pc/proto_layout.py`` (encoded length of every message, "after");
  * ``docs/week13/before_after.md`` sections 1 and 3.2 ("before" sizes);
  * ``docs/week13/after/idf_size.txt`` and ``pico_size.txt`` (measured
    after sizes of the firmware builds).
Outputs: ``docs/week13/data_representation.md``,
``docs/week13/message_sizes.md``, and ``before_after.md`` with its empty
After cells filled. Every cell carries a label: measured, derived, or
``[NEEDS MEASUREMENT]`` with the bench step that will measure it. Re-running
the script after a bench run overwrites only cells that are still empty or
``[NEEDS MEASUREMENT]``.

Usage:
    python3 pc/tools/gen_week13_docs.py
"""

from __future__ import annotations

import os
import re
import sys
from typing import Dict, List, Optional, Tuple

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "pc"))
import proto_layout as pl  # noqa: E402

LAYOUT = os.path.join(ROOT, "protocol", "layout_table.md")
BEFORE_AFTER = os.path.join(ROOT, "docs", "week13", "before_after.md")
IDF_AFTER = os.path.join(ROOT, "docs", "week13", "after", "idf_size.txt")
PICO_AFTER = os.path.join(ROOT, "docs", "week13", "after", "pico_size.txt")
DATA_REPR = os.path.join(ROOT, "docs", "week13", "data_representation.md")
MSG_SIZES = os.path.join(ROOT, "docs", "week13", "message_sizes.md")

NEEDS = "[NEEDS MEASUREMENT]"

#: Message types in type-code order (layout_table.md section 5.1).
MESSAGES = ["REGISTER", "HEARTBEAT", "SESSION_CONFIG", "MANIFEST",
            "ASSET_CHUNK", "CHUNK_ACK", "NACK", "BUSY", "ASSET_READY",
            "SYNC_READY", "BEACON", "START_AT", "RESUME_AT", "PAUSE", "STOP",
            "STATUS", "TELEMETRY", "TELEMETRY_ACK"]

#: Before sizes not in before_after.md section 3.2: (bytes, label, note).
BEFORE_EXTRA = {
    "BEACON": ("50 B", "measured (`sizeof(beacon_t)`, Xtensa)",
               "before_after.md section 1; on-air capture T-B4-01 B-2"),
    "HEARTBEAT": ("13 B", "derived (T-B4-01 typing rule)",
                  "9 B header + Node ID `uint32_t` 4; the prototype's "
                  "HELLO (9 B, measured) did both jobs"),
}


def read(path: str) -> str:
    """Whole file as text."""
    with open(path, encoding="utf-8") as handle:
        return handle.read()


def section(text: str, heading: str) -> str:
    """The section that starts with ``heading`` up to the next ``## ``."""
    start = text.index(heading)
    end = text.find("\n## ", start + len(heading))
    return text[start:end if end >= 0 else len(text)]


def after_bytes(name: str) -> Tuple[int, str]:
    """Encoded length of a message and the arithmetic."""
    if name == "ASSET_CHUNK":
        full = pl.encoded_length(name, pl.CHUNK_PAYLOAD_MAX)
        return full, "5 B head + 1,024 B (last of 57: 5 + 256 = 261 B)"
    bits = pl.body_bits(name)  # Header included.
    return pl.encoded_length(name), "%d bits, ceil(%d / 8)" % (bits, bits)


def before_sizes(text: str) -> Dict[str, Tuple[str, str]]:
    """Message -> (before, label) from before_after.md section 3.2."""
    sizes: Dict[str, Tuple[str, str]] = {}
    for line in section(text, "### 3.2").splitlines():
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) >= 5 and cells[0] in MESSAGES:
            sizes[cells[0]] = (cells[3], cells[4])
    for name, (size, label, _note) in BEFORE_EXTRA.items():
        sizes.setdefault(name, (size, label))
    return sizes


def idf_after() -> Dict[str, int]:
    """DIRAM .data, .bss, .text and IRAM .text from the after size report."""
    values: Dict[str, int] = {}
    region = ""
    for line in read(IDF_AFTER).splitlines():
        cells = [c.strip() for c in line.strip("│ ").split("│")]
        if not cells or not cells[0]:
            continue
        if cells[0] in ("DIRAM", "IRAM"):
            region = cells[0]
            values[region] = int(cells[1])
        elif cells[0] in (".data", ".bss", ".text") and region:
            values.setdefault(region + cells[0], int(cells[1]))
    return values


def pico_after() -> Tuple[int, int, int]:
    """text, data, bss of the after Pico build."""
    for line in read(PICO_AFTER).splitlines():
        parts = line.split()
        if parts and parts[-1].endswith(".elf"):
            return int(parts[0]), int(parts[1]), int(parts[2])
    raise ValueError("no size line in %s" % PICO_AFTER)


def write_data_representation() -> None:
    """data_representation.md from layout_table.md section 7 plus the
    encoded length of every message."""
    table = section(read(LAYOUT), "## 7. Data representation table")
    lines = ["# Data representation (T-X-06)", "",
             "Generated by `pc/tools/gen_week13_docs.py` from "
             "`protocol/layout_table.md` section 7 and `pc/proto_layout.py`."
             " Do not edit by hand; edit the layout table and re-run.", "",
             "## 1. Fields and shared packed words", "",
             table.split("\n", 1)[1].strip(), "",
             "## 2. Encoded length of every message (after)", "",
             "| Message | Type code | Bits | Bytes | Label |",
             "|---|---|---|---|---|"]
    for code, name in enumerate(MESSAGES):
        size, how = after_bytes(name)
        lines.append("| %s | %d | %s | %d | derived (encoder) |" % (
            name, code, how, size))
    lines.append("")
    with open(DATA_REPR, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines))


def write_message_sizes(text: str) -> None:
    """message_sizes.md: before, after encoded, after on-air (T-X-04)."""
    before = before_sizes(text)
    lines = ["# Message size before and after (T-X-04)", "",
             "Generated by `pc/tools/gen_week13_docs.py`. Before: "
             "`before_after.md` (measured for beacon and HELLO, derived for "
             "the rest). After: the encoded length from the frozen layout "
             "table. On-air after: the UDP payload length captured on the PC "
             "in the T-X-04 run; until then it is %s." % NEEDS, "",
             "| Message | Before | Before label | After (encoded) | After "
             "label | After on-air |", "|---|---|---|---|---|---|"]
    missing = 0
    for name in MESSAGES:
        size, how = after_bytes(name)
        cell, label = before.get(name, ("", ""))
        missing += 0 if cell else 1
        lines.append("| %s | %s | %s | %d B (%s) | derived | %s |" % (
            name, cell or NEEDS, label or NEEDS, size, how, NEEDS))
    lines += ["", "Message types with a before cell: %d of %d." % (
        len(MESSAGES) - missing, len(MESSAGES)), ""]
    for name, (_size, _label, note) in BEFORE_EXTRA.items():
        lines.append("- %s before: %s." % (name, note))
    lines.append("")
    with open(MSG_SIZES, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines))


def after_cells() -> List[Tuple[str, str]]:
    """(row prefix, after cell) for before_after.md."""
    idf = idf_after()
    text_b, data_b, bss_b = pico_after()
    dram = idf["DIRAM.data"] + idf["DIRAM.bss"]
    iram = idf["IRAM"] + idf["DIRAM.text"]
    cells = [
        ("| `sizeof(beacon_t)` on the target", "none: no wire struct "
         "(packed by shifts and masks, VB-2a); encoded BEACON %d B, derived"
         % after_bytes("BEACON")[0]),
        ("| `sizeof(hello_t)` on the target", "none: HELLO replaced by "
         "REGISTER %d B and HEARTBEAT %d B, derived" % (
             after_bytes("REGISTER")[0], after_bytes("HEARTBEAT")[0])),
        ("| `sizeof(beacon_t)`, host", "none on either compiler: no wire "
         "struct; encoded BEACON %d B, derived" % after_bytes("BEACON")[0]),
        ("| `sizeof(hello_t)`, host", "none on either compiler: REGISTER "
         "%d B and HEARTBEAT %d B, derived" % (
             after_bytes("REGISTER")[0], after_bytes("HEARTBEAT")[0])),
        ("| `beacon_t` field sum", "%d B, derived (encoder)"
         % after_bytes("BEACON")[0]),
        ("| `hello_t` field sum", "REGISTER %d B, HEARTBEAT %d B, derived" % (
            after_bytes("REGISTER")[0], after_bytes("HEARTBEAT")[0])),
        ("| Beacon, on-air", "%s (T-X-04 capture)" % NEEDS),
        ("| HELLO, on-air", "%s (T-X-04 capture of REGISTER and HEARTBEAT)"
         % NEEDS),
        ("| `panel_sync` static DRAM", "%s B (%s + %s), measured (build, "
         "`docs/week13/after/idf_size.txt`)" % (
             format(dram, ","), format(idf["DIRAM.data"], ","),
             format(idf["DIRAM.bss"], ","))),
        ("| `panel_sync` static IRAM", "%s B (%s + %s), measured (build, "
         "`docs/week13/after/idf_size.txt`)" % (
             format(iram, ","), format(idf["IRAM"], ","),
             format(idf["DIRAM.text"], ","))),
        ("| `ledwall_sync.elf` text / data / bss", "%s / %d / %s B, measured "
         "(build, `docs/week13/after/pico_size.txt`)" % (
             format(text_b, ","), data_b, format(bss_b, ","))),
        ("| Free internal heap after WiFi", "%s (bench Part D, D5)" % NEEDS),
        ("| Largest internal block after WiFi", "%s (bench Part D, D5)"
         % NEEDS),
        ("| Asset per tile", "57,600 B, derived (600 x 96 B, packed)"),
        ("| Upload chunks of 1,024 B", "57 chunks, last 256 B, derived"),
        ("| RAM copy plus margin", "90,368 B, derived (57,600 + 32,768)"),
    ]
    for name in MESSAGES:
        size, how = after_bytes(name)
        cells.append(("| %s |" % name, "%d B, derived (%s)" % (size, how)))
    return cells


def fill_after(text: str) -> Tuple[str, int]:
    """Fill the last cell of matching rows when it is empty or NEEDS."""
    filled = 0
    out = []
    rules = after_cells()
    for line in text.splitlines():
        rule: Optional[str] = next(
            (cell for prefix, cell in rules if line.startswith(prefix)), None)
        if rule is not None and line.rstrip().endswith("|"):
            head, last = line.rstrip()[:-1].rsplit("|", 1)
            if last.strip() in ("", NEEDS) or last.strip().startswith(NEEDS):
                line = "%s| %s |" % (head, rule)
                filled += 1
        out.append(line)
    return "\n".join(out) + ("\n" if text.endswith("\n") else ""), filled


def main() -> int:
    """Write both documents and fill before_after.md."""
    text = read(BEFORE_AFTER)
    write_data_representation()
    write_message_sizes(text)
    new_text, filled = fill_after(text)
    with open(BEFORE_AFTER, "w", encoding="utf-8") as handle:
        handle.write(new_text)
    print("wrote %s and %s; filled %d After cells in %s" % (
        os.path.relpath(DATA_REPR, ROOT), os.path.relpath(MSG_SIZES, ROOT),
        filled, os.path.relpath(BEFORE_AFTER, ROOT)))
    left = [line.split("|")[1].strip() for line in new_text.splitlines()
            if re.match(r"^\| .* \|\s*$", line) and line.rstrip().endswith(
                "| |")]
    if left:
        print("rows still without an After cell: %s" % "; ".join(left))
    return 0


if __name__ == "__main__":
    sys.exit(main())
