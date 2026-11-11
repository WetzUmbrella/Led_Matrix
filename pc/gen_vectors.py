#!/usr/bin/env python3
"""Write the cross-language test vectors (T-B1-01).

Owner: Jolyn (Buddy 1). Implemented by Ahsanul (B4) for Jolyn, task T-B1-01.

Writes ``protocol/vectors/vectors.json`` and one ``<name>.bin`` per vector.
The vectors are fixed values, so running this again gives identical files;
``pc/tests/test_cross_lang.py`` checks that, and checks every message vector
against the C codec in both directions.

Vector set (T-B1-01 row): at least one per message type, the T-B2-01
boundary set (every field at its smallest and largest valid value), one
packed asset frame, a manifest with an unknown format_version, and a
115,200 B payload (the 8-bit size that VC-5 requires the tile to NACK).

Usage: python3 pc/gen_vectors.py [output directory]
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from typing import Dict, List

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import proto_layout as pl  # noqa: E402

DEFAULT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..",
                           "protocol", "vectors")

#: Typical values: every field non-zero and different from its neighbours,
#: so a field written at the wrong offset changes the bytes.
TYPICAL: Dict[str, Dict[str, object]] = {
    "REGISTER": {"node_id": 3, "cached_session_id": 0xA5,
                 "cached_crc32": 0x1234ABCD},
    "HEARTBEAT": {"node_id": 7},
    "SESSION_CONFIG": {"rows": 2, "cols": 3, "policy": 1,
                       "node_map": [1, 2, 0, 3, 4, 5, 0, 0, 0],
                       "frame_count": 600,
                       "crc32": [0x11111111, 0x22222222, 0, 0x33333333,
                                 0x44444444, 0x55555555, 0, 0, 0]},
    "MANIFEST": {"format_version": 2, "frame_count": 600,
                 "crc32": 0xCBF43926},
    "ASSET_CHUNK": {"chunk_index": 56,
                    "payload": bytes((i * 7 + 3) & 0xFF for i in range(256))},
    "CHUNK_ACK": {"node_id": 5, "chunk_index": 42},
    "NACK": {"node_id": 2, "reason": 1, "ref_seq": 0x4321},
    "BUSY": {"node_id": 9, "ref_seq": 0x0FED},
    "ASSET_READY": {"node_id": 4, "crc32": 0xDEADBEEF},
    "SYNC_READY": {"node_id": 6},
    "BEACON": {"t_p": 0x0123456789ABCDEF},
    "START_AT": {"t_start": 1_234_567_890_123},
    "RESUME_AT": {"t_resume": 9_876_543_210_987, "frame_index": 517},
    "PAUSE": {},
    "STOP": {},
    "STATUS": {"readiness": 0x0A5, "online": 0x15A, "state": 12,
               "score": -7, "round_state": 3, "level": 1, "missed": 5,
               "wrong": 2, "shown": 17, "time_left_s": 21,
               "reaction_ms": 431},
    "TELEMETRY": {"node_id": 8, "fault_word": 0x80000041,
                  "orientation": 2, "display_state": 4},
    "TELEMETRY_ACK": {"acked_seq": 0xBEEF},
    "TAP": {"node_id": 5, "peak": 0x2A5C, "t_tap": 0x00000123456789AB},
    "MOLE_ON": {"node_id": 3, "t_show": 1_234_567_890_123, "up_ms": 1200,
                "kind": 1},
    "FEEDBACK": {"node_id": 6, "result": 1},
    "GAME_CONTROL": {"node_id": 2, "command": 4, "level": 1,
                     "threshold": 10},
    "WALL_PICTURE": {"node_id": 7, "slot": 2, "effect": 0, "colour": 1,
                     "repeat": 1, "t_show": 9_876_543_210_987,
                     "dur_ms": 1000, "mask": 0x183C7EFFFF7E3C18},
    "ACK": {"node_id": 4, "acked_seq": 0xA55A},
}

#: Largest-value corners that break a rule across fields: rows and cols
#: may each be 9, but rows x cols is at most 9, so the max corner is 9 x 1.
CORNER_MAX_OVERRIDE: Dict[str, Dict[str, object]] = {
    "SESSION_CONFIG": {"cols": 1},
}


def _corner(name: str, high: bool) -> Dict[str, object]:
    """Every field at its smallest (high False) or largest valid value."""
    fields: Dict[str, object] = {}
    arrays: Dict[str, List[int]] = {}
    for field, _, low, top in pl.LAYOUT[name][1]:
        value = top if high else low
        if "[" in field:
            arrays.setdefault(field.split("[", 1)[0], []).append(value)
        else:
            fields[field] = value
    fields.update(arrays)
    if high:
        fields.update(CORNER_MAX_OVERRIDE.get(name, {}))
    if name == "ASSET_CHUNK":
        size = pl.CHUNK_PAYLOAD_MAX if high else 1
        fields["payload"] = bytes((0xFF if high else 0x00,)) * size
    return fields


def _message(vector: str, name: str, session_id: int, seq: int,
             fields: Dict[str, object]) -> Dict[str, object]:
    full: Dict[str, object] = {"session_id": session_id, "seq": seq}
    full.update(fields)
    data = pl.encode(name, full)
    shown = dict(full)
    if "payload" in shown:
        raw = shown["payload"]
        shown["payload"] = bytes(raw).hex()  # type: ignore[arg-type]
    return {"name": vector, "kind": "message", "message": name,
            "fields": shown, "file": vector + ".bin", "hex": data.hex(),
            "length": len(data), "_data": data}


def build_vectors() -> List[Dict[str, object]]:
    """The full vector list; '_data' holds the bytes for each file."""
    vectors: List[Dict[str, object]] = []
    for name in pl.LAYOUT:
        stem = name.lower()
        vectors.append(_message(stem + "_typical", name, 0x3C, 0x1234,
                                TYPICAL[name]))
        vectors.append(_message(stem + "_min", name, 0, 0,
                                _corner(name, False)))
        vectors.append(_message(stem + "_max", name, 0xFF, 0xFFFF,
                                _corner(name, True)))

    # VC-5: a manifest whose format_version no firmware knows. The codec
    # carries it; the tile must NACK it (T-B3-05, T-X-08 case 4).
    vectors.append(_message("manifest_unknown_format", "MANIFEST", 0x3C, 7,
                            {"format_version": 0, "frame_count": 600,
                             "crc32": 0x11223344}))

    # OD-05 worked vector: pixel 0 = (1, 2, 3), pixel 1 = (4, 5, 6).
    worked = [1, 2, 3, 4, 5, 6] + [0] * (pl.FRAME_NIBBLES - 6)
    pattern = [k % (pl.NIBBLE_MAX + 1) for k in range(pl.FRAME_NIBBLES)]
    for vector, values in (("frame_od05_worked", worked),
                           ("frame_pattern", pattern)):
        data = pl.pack_frame(values)
        vectors.append({"name": vector, "kind": "frame", "values": values,
                        "file": vector + ".bin", "hex": data.hex(),
                        "length": len(data), "_data": data})

    # VC-5 / T-X-08 case 5: a 115,200 B payload (8 bits per channel, 600
    # frames) against a manifest of frame_count 600, which requires
    # 600 x 96 = 57,600 B. The tile must NACK with reason 1.
    payload = bytes((i * 31 + 17) & 0xFF for i in range(115_200))
    vectors.append({"name": "payload_115200", "kind": "payload",
                    "manifest": {"format_version": 2, "frame_count": 600},
                    "expected_length": 600 * pl.FRAME_BYTES,
                    "file": "payload_115200.bin", "length": len(payload),
                    "_data": payload})
    return vectors


def write_vectors(out_dir: str) -> List[Dict[str, object]]:
    """Write vectors.json and every .bin into out_dir."""
    os.makedirs(out_dir, exist_ok=True)
    vectors = build_vectors()
    listing = []
    for vector in vectors:
        data = vector["_data"]
        with open(os.path.join(out_dir, str(vector["file"])), "wb") as handle:
            handle.write(data)  # type: ignore[arg-type]
        listing.append({k: v for k, v in vector.items() if k != "_data"})
    with open(os.path.join(out_dir, "vectors.json"), "w",
              encoding="utf-8") as handle:
        json.dump(listing, handle, indent=1, sort_keys=True)
        handle.write("\n")
    return vectors


def main() -> None:
    """Command line: optional output directory."""
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("out_dir", nargs="?", default=DEFAULT_DIR,
                        help="output directory (default protocol/vectors)")
    args = parser.parse_args()
    written = write_vectors(args.out_dir)
    print("%d vectors written to %s" % (len(written), args.out_dir))


if __name__ == "__main__":
    main()
