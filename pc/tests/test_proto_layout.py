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
LENGTHS = {"REGISTER": 10, "HEARTBEAT": 5, "SESSION_CONFIG": 53,
           "MANIFEST": 10, "CHUNK_ACK": 5, "NACK": 7, "BUSY": 7,
           "ASSET_READY": 9, "SYNC_READY": 5, "BEACON": 12, "START_AT": 12,
           "RESUME_AT": 13, "PAUSE": 4, "STOP": 4, "STATUS": 13,
           "TELEMETRY": 9, "TELEMETRY_ACK": 6, "TAP": 15, "MOLE_ON": 14,
           "FEEDBACK": 5, "GAME_CONTROL": 6, "WALL_PICTURE": 23, "ACK": 7}


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
                                online=bitmap, state=0, score=0,
                                **_STATUS_GAME)
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


#: STATUS game fields at their idle values (build step 3).
_STATUS_GAME = {"round_state": 0, "level": 0, "missed": 0, "wrong": 0,
                "shown": 0, "time_left_s": 0, "reaction_ms": 0}
#: WALL_PICTURE fields other than node_id, at valid values.
#: A valid 1 x 1 SESSION_CONFIG with no tile placed (step 4d fields).
_CFG = {"rows": 1, "cols": 1, "policy": 0, "placed": [0] * 9, "row": [0] * 9,
        "col": [0] * 9, "frame_count": 1, "crc32": [0] * 9}
_PIC = {"slot": 0, "effect": 0, "colour": 0, "repeat": 0, "t_show": 0,
        "dur_ms": 0, "mask": 0}


@pytest.mark.parametrize("name,fields", [
    ("HEARTBEAT", {"node_id": 0}),
    ("HEARTBEAT", {"node_id": 10}),
    ("MANIFEST", {"format_version": 2, "frame_count": 0, "crc32": 0}),
    ("MANIFEST", {"format_version": 2, "frame_count": 1024, "crc32": 0}),
    ("STATUS", {"readiness": 0x200, "online": 0, "state": 0, "score": 0,
                **_STATUS_GAME}),
    ("STATUS", {"readiness": 0, "online": 0, "state": 13, "score": 0,
                **_STATUS_GAME}),
    ("STATUS", {"readiness": 0, "online": 0, "state": 0, "score": 100,
                **_STATUS_GAME}),
    ("STATUS", {"readiness": 0, "online": 0, "state": 0, "score": -100,
                **_STATUS_GAME}),
    ("STATUS", {"readiness": 0, "online": 0, "state": 0, "score": 0,
                **dict(_STATUS_GAME, round_state=5)}),
    ("STATUS", {"readiness": 0, "online": 0, "state": 0, "score": 0,
                **dict(_STATUS_GAME, level=3)}),
    ("TAP", {"node_id": 0, "peak": 0, "t_tap": 0}),
    ("MOLE_ON", {"node_id": 1, "t_show": 0, "up_ms": 0, "kind": 5}),
    ("MOLE_ON", {"node_id": 1, "t_show": 0, "up_ms": 4001, "kind": 0}),
    ("FEEDBACK", {"node_id": 1, "result": 3}),
    ("GAME_CONTROL", {"node_id": 10, "command": 0, "level": 0,
                      "threshold": 0}),
    ("GAME_CONTROL", {"node_id": 0, "command": 8, "level": 0,
                      "threshold": 0}),
    ("GAME_CONTROL", {"node_id": 0, "command": 4, "level": 3,
                      "threshold": 0}),
    ("GAME_CONTROL", {"node_id": 0, "command": 4, "level": 0,
                      "threshold": 251}),
    ("WALL_PICTURE", {"node_id": 0, **_PIC}),
    ("WALL_PICTURE", {"node_id": 1, **dict(_PIC, effect=3)}),
    ("ACK", {"node_id": 10, "acked_seq": 0}),
    ("TELEMETRY", {"node_id": 1, "fault_word": 0, "orientation": 0,
                   "display_state": 7}),
    ("ASSET_CHUNK", {"chunk_index": 113, "payload": b"\x00"}),
    ("ASSET_CHUNK", {"chunk_index": 0, "payload": b""}),
    ("ASSET_CHUNK", {"chunk_index": 0, "payload": bytes(1025)}),
    ("SESSION_CONFIG", dict(_CFG, rows=0)),
    ("SESSION_CONFIG", dict(_CFG, cols=10)),
    # Step 4d cells: outside the grid, shared, on a tile not placed,
    # placed 2, row 9.
    ("SESSION_CONFIG", dict(_CFG, rows=2, cols=2, placed=[1] + [0] * 8,
                            row=[2] + [0] * 8)),
    ("SESSION_CONFIG", dict(_CFG, rows=2, cols=2, placed=[1, 1] + [0] * 7,
                            row=[1, 1] + [0] * 7, col=[1, 1] + [0] * 7)),
    ("SESSION_CONFIG", dict(_CFG, row=[0, 1] + [0] * 7)),
    ("SESSION_CONFIG", dict(_CFG, placed=[2] + [0] * 8)),
    ("SESSION_CONFIG", dict(_CFG, rows=9, placed=[1] + [0] * 8,
                            row=[9] + [0] * 8)),
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
    # Type code 24 is not a message.
    with pytest.raises(pl.LayoutError):
        pl.decode(bytes((24, 0, 0, 0, 0)))


def test_score_is_twos_complement() -> None:
    """STATUS score -99 to 99: -1 is all ones in its 8 bits (bits 51-58)."""
    for score in (-99, -1, 0, 99):
        data = pl.encode_status(session_id=1, seq=1, readiness=0, online=0,
                                state=12, score=score, **_STATUS_GAME)
        assert pl.decode_status(data)["score"] == score
    data = pl.encode_status(session_id=0, seq=0, readiness=0, online=0,
                            state=0, score=-1, **_STATUS_GAME)
    assert (int.from_bytes(data, "little") >> 51) & 0xFF == 0xFF


def test_sparse_grid_round_trips() -> None:
    """Step 4d (DS-15): a 9 x 9 grid with tiles on the diagonal."""
    diagonal = list(range(9))
    data = pl.encode_session_config(session_id=1, seq=1, rows=9, cols=9,
                                    policy=0, placed=[1] * 9, row=diagonal,
                                    col=diagonal, frame_count=1,
                                    crc32=[0] * 9)
    msg = pl.decode(data)
    assert (msg["rows"], msg["cols"]) == (9, 9)
    assert msg["placed"] == [1] * 9
    assert msg["row"] == diagonal and msg["col"] == diagonal


def test_decoder_rejects_a_shared_cell() -> None:
    """Node 1 at (0, 0) of 1 x 1; node 2's placed bit (bit 47) set on the
    wire puts it on the same cell."""
    good = pl.encode_session_config(session_id=1, seq=1, rows=1, cols=1,
                                    policy=0, placed=[1] + [0] * 8,
                                    row=[0] * 9, col=[0] * 9, frame_count=1,
                                    crc32=[0] * 9)
    assert pl.decode(good)["placed"][0] == 1  # type: ignore[index]
    with pytest.raises(pl.LayoutError, match="shares"):
        pl.decode(_with_bits(good, 47, 1, 1))


def test_places_from_a_row_major_map() -> None:
    """The bench form "1x2 4,5,0,..." gives nodes 4 and 5 their cells."""
    cells = pl.places_from_map(3, [1, 0, 2, 0, 3, 0, 0, 0, 0])
    assert cells["placed"] == [1, 1, 1, 0, 0, 0, 0, 0, 0]
    assert cells["row"][:3] == [0, 0, 1] and cells["col"][:3] == [0, 2, 1]


#: The six whack-a-mole rows (re-plan build step 3).
GAME_ROWS = ["TAP", "MOLE_ON", "FEEDBACK", "GAME_CONTROL", "WALL_PICTURE",
             "ACK"]
#: Rows whose node_id names a tile (1 to 9); the others allow 0 (PC-Pico).
TILE_ONLY = ["TAP", "MOLE_ON", "FEEDBACK", "WALL_PICTURE"]
#: node_id sits at bit 29 in every game row, 4 bits wide.
NODE_BIT = 29


def _typical(name: str) -> bytes:
    return pl.encode(name, dict(gv.TYPICAL[name], session_id=1, seq=1))


def _with_bits(data: bytes, bit: int, width: int, value: int) -> bytes:
    """data with the field at bit, width bits wide, set straight to value."""
    word = int.from_bytes(data, "little") & ~(((1 << width) - 1) << bit)
    return (word | (value << bit)).to_bytes(len(data), "little")


@pytest.mark.parametrize("name", GAME_ROWS)
def test_decoder_rejects_truncated_game_rows(name: str) -> None:
    data = _typical(name)
    assert pl.decode(data)["type"] == name
    with pytest.raises(pl.LayoutError):
        pl.decode(data[:-1])


@pytest.mark.parametrize("name", GAME_ROWS)
@pytest.mark.parametrize("node_id", [0] + list(range(10, 16)))
def test_decoder_node_id_range_in_game_rows(name: str, node_id: int) -> None:
    """node_id 10 to 15 is refused everywhere; 0 only where a tile is
    required (GAME_CONTROL and ACK use 0 for the PC-Pico link)."""
    data = _with_bits(_typical(name), NODE_BIT, 4, node_id)
    if node_id == 0 and name not in TILE_ONLY:
        assert pl.decode(data)["node_id"] == 0
    else:
        with pytest.raises(pl.LayoutError):
            pl.decode(data)


@pytest.mark.parametrize("bit", [29, 33])
def test_decoder_rejects_rows_or_cols_of_zero(bit: int) -> None:
    """rows at bit 29, cols at bit 33, 4 bits each; 0 is refused."""
    data = _with_bits(_typical("SESSION_CONFIG"), bit, 4, 0)
    with pytest.raises(pl.LayoutError):
        pl.decode(data)


def test_decoder_takes_up_ms_of_zero_and_rejects_kind_5() -> None:
    """MOLE_ON up_ms at bit 97, 12 bits: 0 is valid since 11 Nov 2026 (the
    start sprite, up until tapped). kind at bit 109, 3 bits: 5 is refused."""
    assert pl.decode(_with_bits(_typical("MOLE_ON"), 97, 12, 0))["up_ms"] == 0
    with pytest.raises(pl.LayoutError):
        pl.decode(_with_bits(_typical("MOLE_ON"), 109, 3, 5))
