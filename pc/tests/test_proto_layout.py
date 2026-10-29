"""pytest for pc/proto_layout.py (T-B1-01).

Owner: Jolyn (Buddy 1). Implemented by Ahsanul (B4) for Jolyn, task T-B1-01.
Every behaviour in the T-B1-01 and T-B2-01 rows that the Python side can
show on its own: the boundary set round trip for every row, the OD-05
worked vector, the 115,200 B payload against a 600-frame manifest, the
lengths of layout_table.md section 4 and the valid-range rejections.
"""

from __future__ import annotations

import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import gen_vectors as gv  # noqa: E402
import proto_layout as pl  # noqa: E402

#: Encoded lengths from protocol/layout_table.md section 4 (typed in, so a
#: width change in LAYOUT that the table does not follow fails here).
LENGTHS = {"REGISTER": 10, "HEARTBEAT": 5, "SESSION_CONFIG": 9,
           "MANIFEST": 10, "CHUNK_ACK": 5, "NACK": 7, "BUSY": 7,
           "ASSET_READY": 9, "SYNC_READY": 5, "BEACON": 12, "START_AT": 12,
           "RESUME_AT": 13, "PAUSE": 4, "STOP": 4, "STATUS": 7,
           "TELEMETRY": 9, "TELEMETRY_ACK": 6}


def _expected(full: dict) -> dict:
    return {k: v for k, v in full.items()}


@pytest.mark.parametrize("name", list(pl.LAYOUT))
@pytest.mark.parametrize("high", [False, True])
def test_boundary_round_trip(name: str, high: bool) -> None:
    """T-B2-01 boundary set: each field at its smallest and largest value."""
    fields = gv._corner(name, high)
    fields.update({"session_id": 0xFF if high else 0,
                   "seq": 0xFFFF if high else 0})
    decoded = pl.decode(pl.encode(name, fields))
    assert decoded.pop("type") == name
    assert decoded == fields


def test_boundary_values_named_in_the_plan() -> None:
    """frame index 0 and 1023; t_p 0 and 2^64 - 1; fault word 0 and
    0xFFFFFFFF; bitmap 0 and 0x1FF."""
    for frame_index in (0, 1023):
        data = pl.encode_resume_at(session_id=1, seq=1, t_resume=5,
                                   frame_index=frame_index)
        assert pl.decode_resume_at(data)["frame_index"] == frame_index
    for t_p in (0, (1 << 64) - 1):
        data = pl.encode_beacon(session_id=1, seq=1, t_p=t_p)
        assert pl.decode_beacon(data)["t_p"] == t_p
    for word in (0, 0xFFFFFFFF):
        data = pl.encode_telemetry(session_id=1, seq=1, node_id=1,
                                   fault_word=word, orientation=0,
                                   display_state=0)
        assert pl.decode_telemetry(data)["fault_word"] == word
    for bitmap in (0, 0x1FF):
        data = pl.encode_status(session_id=1, seq=1, readiness=bitmap,
                                online=bitmap, state=0)
        decoded = pl.decode_status(data)
        assert decoded["readiness"] == bitmap == decoded["online"]


@pytest.mark.parametrize("name,length", sorted(LENGTHS.items()))
def test_lengths_match_layout_table(name: str, length: int) -> None:
    assert pl.encoded_length(name) == length
    fields = gv._corner(name, False)
    fields.update({"session_id": 0, "seq": 0})
    assert len(pl.encode(name, fields)) == length


def test_asset_chunk_length_is_header_plus_payload() -> None:
    data = pl.encode_asset_chunk(session_id=1, seq=1, chunk_index=56,
                                 payload=bytes(256))
    assert len(data) == 5 + 256
    assert pl.decode_asset_chunk(data)["payload"] == bytes(256)


def test_beacon_reference_bytes() -> None:
    """Same reference as common/proto/test/test_proto.c g_ref_mid."""
    data = pl.encode_beacon(session_id=0x5A, seq=0x1234,
                            t_p=0x0123456789ABCDEF)
    assert data.hex() == "4a8b46e2bd7935f1ac682400"


def test_od05_worked_vector() -> None:
    """Pixel 0 = (1, 2, 3), pixel 1 = (4, 5, 6) packs to 21 43 65."""
    values = [1, 2, 3, 4, 5, 6] + [0] * 186
    frame = pl.pack_frame(values)
    assert len(frame) == 96
    assert frame[:3] == bytes((0x21, 0x43, 0x65))
    assert pl.unpack_frame(frame) == values


def test_packed_frame_rejects_values_above_the_cap() -> None:
    with pytest.raises(pl.LayoutError):
        pl.pack_frame([9] + [0] * 191)


def test_payload_115200_does_not_match_a_600_frame_manifest() -> None:
    """VC-5: payload length must be frame_count x 96 B."""
    vector = [v for v in gv.build_vectors() if v["name"] == "payload_115200"]
    assert len(vector[0]["_data"]) == 115_200
    assert vector[0]["expected_length"] == 57_600


@pytest.mark.parametrize("name,fields", [
    ("HEARTBEAT", {"node_id": 0}),
    ("HEARTBEAT", {"node_id": 10}),
    ("MANIFEST", {"format_version": 2, "frame_count": 0, "crc32": 0}),
    ("MANIFEST", {"format_version": 2, "frame_count": 1024, "crc32": 0}),
    ("STATUS", {"readiness": 0x200, "online": 0, "state": 0}),
    ("STATUS", {"readiness": 0, "online": 0, "state": 12}),
    ("TELEMETRY", {"node_id": 1, "fault_word": 0, "orientation": 0,
                   "display_state": 7}),
    ("ASSET_CHUNK", {"chunk_index": 113, "payload": b"\x00"}),
    ("ASSET_CHUNK", {"chunk_index": 0, "payload": b""}),
    ("ASSET_CHUNK", {"chunk_index": 0, "payload": bytes(1025)}),
    ("SESSION_CONFIG", {"layout": 3, "policy": 0, "node_map": [0] * 9}),
    ("SESSION_CONFIG", {"layout": 0, "policy": 0,
                        "node_map": [10] + [0] * 8}),
])
def test_encoder_refuses_out_of_range(name: str, fields: dict) -> None:
    fields = dict(fields, session_id=0, seq=0)
    with pytest.raises(pl.LayoutError):
        pl.encode(name, fields)


def test_decoder_rejects_wrong_length_type_and_range() -> None:
    beat = pl.encode_heartbeat(session_id=1, seq=2, node_id=3)
    with pytest.raises(pl.LayoutError):
        pl.decode(beat + b"\x00")
    with pytest.raises(pl.LayoutError):
        pl.decode(beat[:3])
    with pytest.raises(pl.LayoutError):
        pl.decode_sync_ready(beat)
    # Node ID 0 written straight into bits 29 to 32.
    value = int.from_bytes(beat, "little") & ~(0xF << 29)
    with pytest.raises(pl.LayoutError):
        pl.decode(value.to_bytes(len(beat), "little"))
    # Type code 20 is not a message.
    with pytest.raises(pl.LayoutError):
        pl.decode(bytes((20, 0, 0, 0, 0)))
