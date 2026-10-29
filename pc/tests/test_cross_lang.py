"""Cross-language test: C codec against pc/proto_layout.py (T-B1-01).

Owner: Jolyn (Buddy 1). Implemented by Ahsanul (B4) for Jolyn, task T-B1-01.

Acceptance (T-B1-01 row): "Python to C and C to Python: byte-for-byte equal
for 100% of vectors. Pass = 0 mismatches."

For every message vector in protocol/vectors/vectors.json:
  1. Python to C: the C tool decodes the Python-written .bin and prints the
     same fields;
  2. C to Python: the C tool encodes the same fields and its bytes equal the
     .bin, and Python decodes the C bytes back to the same fields.
For each frame vector the C tool reads the 192 nibbles with proto_get_bits
and they equal Python's values. The committed vectors must also equal a
fresh run of gen_vectors.py.
"""

from __future__ import annotations

import filecmp
import json
import os
import shutil
import subprocess
import sys
from typing import Dict, List

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "pc"))
import gen_vectors as gv  # noqa: E402
import proto_layout as pl  # noqa: E402

VECTOR_DIR = os.path.join(ROOT, "protocol", "vectors")
TEST_DIR = os.path.join(ROOT, "common", "proto", "test")
TOOL = os.path.join(TEST_DIR, "proto_vector")


@pytest.fixture(scope="module")
def tool() -> str:
    if shutil.which("make") is None or shutil.which("gcc") is None:
        pytest.skip("make and gcc are needed to build the C side")
    subprocess.run(["make", "-s", "-C", TEST_DIR, "vector_tool"], check=True)
    return TOOL


def _vectors() -> List[Dict[str, object]]:
    with open(os.path.join(VECTOR_DIR, "vectors.json"),
              encoding="utf-8") as handle:
        return json.load(handle)


def _canonical(vector: Dict[str, object]) -> List[str]:
    """The tool's output for a message vector, built from its fields."""
    fields = dict(vector["fields"])  # type: ignore[arg-type]
    lines = ["type=%s" % vector["message"],
             "session_id=%d" % fields.pop("session_id"),
             "seq=%d" % fields.pop("seq")]
    for field, _, _, _ in pl.LAYOUT[str(vector["message"])][1]:
        if field.startswith("node_map["):
            if field == "node_map[0]":
                lines.append("node_map=" + ",".join(
                    str(n) for n in fields["node_map"]))  # type: ignore
        else:
            lines.append("%s=%d" % (field, fields[field]))
    if "payload" in fields:
        lines.append("payload=%s" % fields["payload"])
    return lines


def _args(vector: Dict[str, object]) -> List[str]:
    args = []
    for key, value in dict(vector["fields"]).items():  # type: ignore
        if key == "node_map":
            value = ",".join(str(n) for n in value)
        args.append("%s=%s" % (key, value))
    return args


def _run(cmd: List[str]) -> str:
    return subprocess.run(cmd, check=True, capture_output=True,
                          text=True).stdout


def test_committed_vectors_are_current(tmp_path) -> None:  # type: ignore
    gv.write_vectors(str(tmp_path))
    names = sorted(os.listdir(str(tmp_path)))
    assert names == sorted(os.listdir(VECTOR_DIR))
    match, mismatch, errors = filecmp.cmpfiles(str(tmp_path), VECTOR_DIR,
                                               names, shallow=False)
    assert mismatch == [] and errors == []


def test_vector_set_covers_the_row() -> None:
    vectors = _vectors()
    messages = {v["message"] for v in vectors if v["kind"] == "message"}
    assert messages == set(pl.LAYOUT)
    names = {v["name"] for v in vectors}
    assert {"manifest_unknown_format", "frame_od05_worked",
            "payload_115200"} <= names


def test_c_and_python_agree_on_every_vector(tool: str) -> None:
    mismatches = []
    count = 0
    for vector in _vectors():
        path = os.path.join(VECTOR_DIR, str(vector["file"]))
        if vector["kind"] == "message":
            count += 1
            # Python to C.
            if _run([tool, "decode", path]).splitlines() != \
                    _canonical(vector):
                mismatches.append(("py->c", vector["name"]))
            # C to Python, bytes and fields.
            c_hex = _run([tool, "encode", str(vector["message"])]
                         + _args(vector)).strip()
            if c_hex != vector["hex"]:
                mismatches.append(("c->py bytes", vector["name"]))
            decoded = pl.decode(bytes.fromhex(c_hex))
            expected = dict(vector["fields"])  # type: ignore[arg-type]
            if "payload" in decoded:
                raw = decoded["payload"]
                decoded["payload"] = bytes(raw).hex()  # type: ignore
            if decoded.pop("type") != vector["message"] or \
                    decoded != expected:
                mismatches.append(("c->py fields", vector["name"]))
        elif vector["kind"] == "frame":
            count += 1
            nibbles = [int(n) for n in
                       _run([tool, "nibbles", path]).strip().split(",")]
            if nibbles != vector["values"]:
                mismatches.append(("frame", vector["name"]))
    print("%d vectors, %d mismatches" % (count, len(mismatches)))
    assert count >= len(pl.LAYOUT)
    assert mismatches == []


def test_c_rejects_the_115200_payload_as_a_chunk(tool: str) -> None:
    """A whole 115,200 B payload is far over one ASSET_CHUNK; the C decoder
    refuses it, and the tile must NACK it by length (VC-5)."""
    path = os.path.join(VECTOR_DIR, "payload_115200.bin")
    result = subprocess.run([tool, "decode", path], capture_output=True)
    assert result.returncode == 1
