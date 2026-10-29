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
    "SESSION_CONFIG": {"layout": 1, "policy": 1,
                       "node_map": [1, 2, 0, 3, 4, 0, 0, 0, 0]},
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
    "STATUS": {"readiness": 0x0A5, "online": 0x15A, "state": 8},
    "TELEMETRY": {"node_id": 8, "fault_word": 0x80000041,
                  "orientation": 2, "display_state": 4},
    "TELEMETRY_ACK": {"acked_seq": 0xBEEF},
}


def _corner(name: str, high: bool) -> Dict[str, object]:
    """Every field at its smallest (high False) or largest valid value."""
    fields: Dict[str, object] = {}
    node_map: List[int] = []
    for field, _, low, top in pl.LAYOUT[name][1]:
        value = top if high else low
        if field.startswith("node_map["):
            node_map.append(value)
        else:
            fields[field] = value
    if node_map:
        fields["node_map"] = node_map
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


if __name__ == "__main__":
    target = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_DIR
    written = write_vectors(target)
    print("%d vectors written to %s" % (len(written), target))
