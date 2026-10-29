"""pytest: pc/proto_layout.py follows protocol/layout_table.md (T-B1-01).

Owner: Jolyn (Buddy 1). Implemented by Ahsanul (B4) for Jolyn, task T-B1-01.
The plan row says "Python and C read the same layout_table.md field list".
This test reads the header table (section 2), every message table
(section 3) and the length table (section 4) and compares them with
``LAYOUT``. The C side is compared with Python by the vectors
(test_cross_lang.py), so all three agree.
"""

from __future__ import annotations

import os
import re
import sys
from typing import Dict, List, Tuple

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import proto_layout as pl  # noqa: E402

TABLE = os.path.join(os.path.dirname(__file__), "..", "..", "protocol",
                     "layout_table.md")


def _rows(lines: List[str]) -> List[List[str]]:
    rows = []
    for line in lines:
        if line.startswith("|") and not line.startswith("|---"):
            rows.append([cell.strip() for cell in line.strip("|\n")
                         .split("|")])
    return rows[1:]  # Drop the column titles.


def _parse() -> Tuple[List[List[str]], Dict[str, Tuple[int, List[List[str]]]],
                      Dict[str, str]]:
    with open(TABLE, encoding="utf-8") as handle:
        text = handle.read()
    header = text.split("## 2. ", 1)[1].split("\n## ", 1)[0]
    messages_text = text.split("## 3. ", 1)[1].split("\n## 4. ", 1)[0]
    lengths_text = text.split("## 4. ", 1)[1].split("\n## 5. ", 1)[0]
    messages: Dict[str, Tuple[int, List[List[str]]]] = {}
    for block in messages_text.split("\n### ")[1:]:
        title, body = block.split("\n", 1)
        match = re.match(r"(\w+) \(type (\d+),", title)
        assert match, title
        messages[match.group(1)] = (int(match.group(2)),
                                    _rows(body.split("\n")))
    lengths = {row[0]: row[2] for row in _rows(lengths_text.split("\n"))}
    return _rows(header.split("\n")), messages, lengths


def _max_of(text: str) -> int:
    """Upper end of a 'low to high' range written in the table."""
    high = text.split(" to ")[1].replace(",", "").replace(" ", "")
    if high.startswith("2^"):
        base, minus = high[2:].split("-")
        return (1 << int(base)) - int(minus)
    return int(high, 16) if high.startswith("0x") else int(high)


def test_header_matches() -> None:
    header, _, _ = _parse()
    assert [(row[0], int(row[1]), int(row[2])) for row in header] == [
        ("type", 0, pl.TYPE_BITS), ("session_id", pl.TYPE_BITS,
                                    pl.SESSION_BITS),
        ("seq", pl.TYPE_BITS + pl.SESSION_BITS, pl.SEQ_BITS)]


def test_every_message_row_matches() -> None:
    _, messages, _ = _parse()
    assert set(messages) == set(pl.LAYOUT)
    for name, (code, rows) in messages.items():
        assert code == pl.LAYOUT[name][0], name
        table = [(row[0], int(row[1]), int(row[2])) for row in rows]
        assert table == pl.offsets(name), name
        for row, (_, _, low, high) in zip(rows, pl.LAYOUT[name][1]):
            assert int(row[3].split(" to ")[0], 0) == low, (name, row[0])
            assert _max_of(row[3]) == high, (name, row[0])


def test_lengths_match() -> None:
    _, _, lengths = _parse()
    for name in pl.LAYOUT:
        cell = lengths[name]
        if name == "ASSET_CHUNK":
            assert cell.startswith("5 + 1 to 1,024")
        else:
            assert int(cell) == pl.encoded_length(name), name
