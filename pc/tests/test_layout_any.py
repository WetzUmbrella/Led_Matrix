"""pytest: any rows x cols layout (whack-a-mole re-plan, build step 4).

Covers asset splitting for 1x9, 1x5, 2x2 and 3x3 (with blanks), the centre
crop, the dashboard's tap-to-place with a fake tap feed, resumable uploads,
the coordinator state taken from the Pico, and the session id rules.
"""

from __future__ import annotations

import os
import random
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, os.path.dirname(__file__))
import asset_builder as ab  # noqa: E402
import dashboard as db  # noqa: E402
import numbered_pattern as npat  # noqa: E402
import pico_link as plink  # noqa: E402
import uploader  # noqa: E402

SHAPES = [
    ("1x9", [1, 2, 3, 4, 5, 6, 7, 8, 9]),
    ("1x5", [1, 2, 3, 4, 5, 0, 0, 0, 0]),
    ("2x2", [1, 2, 3, 4, 0, 0, 0, 0, 0]),
    ("3x3", [1, 2, 3, 4, 5, 6, 7, 8, 9]),
    ("3x3", [1, 0, 2, 0, 3, 0, 0, 0, 4]),
]


def _status(readiness: int, online: int, state: int = 7) -> plink.Status:
    return plink.Status(readiness, online, state, 0.0)


@pytest.mark.parametrize("layout,node_map", SHAPES)
def test_each_node_gets_its_own_region(layout, node_map) -> None:
    frames, _delays = npat.make_frames(layout, node_map)
    tiles = ab.partition_tiles(frames[:2], layout, node_map)
    assert sorted(tiles) == sorted(n for n in node_map if n)
    for slot, node in enumerate(node_map):
        if node:
            for frame in range(2):
                assert tiles[node][frame] == npat.region_pixels(node, frame)


@pytest.mark.parametrize("layout,node_map", SHAPES)
def test_assets_build_for_every_shape(layout, node_map) -> None:
    frames, delays = npat.make_frames(layout, node_map)
    assets = ab.build_assets(frames, delays, layout, node_map)
    assert sorted(assets) == sorted(n for n in node_map if n)
    assert all(len(a.payload) == a.frame_count * 96 for a in assets.values())


def test_centre_crop_drops_the_top_and_bottom_of_a_strip() -> None:
    from PIL import Image
    frame = Image.new("RGB", (90, 30), (0, 0, 255))  # Too tall for 1x9.
    for y in range(10, 20):
        for x in range(90):
            frame.putpixel((x, y), (0, 255, 0))
    tiles = ab.partition_tiles([frame], "1x9", list(range(1, 10)))
    assert all(pixel == (0, 255, 0) for pixel in tiles[1][0])


def test_centre_crop_drops_the_sides_of_a_wide_frame() -> None:
    from PIL import Image
    frame = Image.new("RGB", (40, 10), (255, 0, 0))  # Too wide for 2x2.
    for y in range(10):
        for x in range(15, 25):
            frame.putpixel((x, y), (0, 0, 255))
    tiles = ab.partition_tiles([frame], "2x2", [1, 2, 3, 4, 0, 0, 0, 0, 0])
    assert all(pixel == (0, 0, 255) for pixel in tiles[1][0])


@pytest.mark.parametrize("text", ["2X", "0x3", "10x1", "3x4", "x2", "2x2x2"])
def test_bad_shapes_refused(text: str) -> None:
    with pytest.raises(ab.AssetError):
        ab.parse_shape(text)


class FakeTaps:
    """Stands in for PicoLink.next_tap: a queue of board numbers."""

    def __init__(self, *taps):  # type: ignore[no-untyped-def]
        self.taps = list(taps)

    def __call__(self):  # type: ignore[no-untyped-def]
        return self.taps.pop(0) if self.taps else None


def _model(layout: str = "1x5", node_map=(1, 2, 3, 0, 0, 0, 0, 0, 0),
           clock=lambda: 0.0) -> db.DashboardModel:
    model = db.DashboardModel(clock=clock)
    assert model.configure("x.gif", layout, list(node_map), 1, 8)
    return model


def test_tap_to_place_writes_the_board_number() -> None:
    model = _model()
    assert model.place(4, FakeTaps(5)) == 5
    assert model.node_map == [1, 2, 3, 0, 5, 0, 0, 0, 0]


def test_tap_to_place_moves_a_tile_already_in_the_map() -> None:
    model = _model()
    model.place(0, FakeTaps(2))
    assert model.node_map == [2, 0, 3, 0, 0, 0, 0, 0, 0]


def test_no_tap_leaves_the_map_alone() -> None:
    model = _model()
    before = list(model.node_map)
    assert model.place(4, FakeTaps()) is None
    assert model.node_map == before


def test_place_refused_outside_the_layout_and_while_playing() -> None:
    model = _model()
    with pytest.raises(ab.AssetError):
        model.place(5, FakeTaps(4))  # 1x5 has slots 0 to 4.
    model.on_status(_status(0, 0, state=8))  # COORDINATING PLAYBACK
    with pytest.raises(db.Refused):
        model.place(0, FakeTaps(4))


def test_armed_slot_waits_for_a_tap_then_expires() -> None:
    now = [0.0]
    model = _model(clock=lambda: now[0])
    model.arm_place(3)
    assert model.poll_place(FakeTaps()) is None
    assert model.armed == (3, db.PLACE_WAIT_S)
    assert model.poll_place(FakeTaps(4)) == 4
    assert model.node_map[3] == 4 and model.armed is None
    model.arm_place(0)
    now[0] = db.PLACE_WAIT_S + 1
    assert model.poll_place(FakeTaps(9)) is None
    assert model.armed is None and model.node_map[0] == 1


def test_grid_keeps_tiles_still_inside_the_new_shape() -> None:
    model = _model(node_map=(1, 2, 3, 4, 5, 0, 0, 0, 0))
    model.set_grid(2, 2)
    assert model.layout == "2x2"
    assert model.node_map == [1, 2, 0, 0, 0, 0, 0, 0, 0]


def test_grid_refused_when_it_leaves_no_tile() -> None:
    model = _model(node_map=(0, 0, 0, 0, 5, 0, 0, 0, 0))
    with pytest.raises(ab.AssetError):
        model.set_grid(2, 2)
    assert model.layout == "1x5"


def test_slot_off_remembers_the_board_and_on_writes_it_back() -> None:
    model = _model()
    assert model.set_slot(1, False) == 2
    assert model.node_map == [1, 0, 3, 0, 0, 0, 0, 0, 0]
    assert model.parked == {1: 2}
    assert model.set_slot(1, True) == 2
    assert model.node_map == [1, 2, 3, 0, 0, 0, 0, 0, 0]
    assert model.parked == {}


def test_last_tile_cannot_be_switched_off() -> None:
    model = _model(node_map=(4, 0, 0, 0, 0, 0, 0, 0, 0))
    with pytest.raises(ab.AssetError):
        model.set_slot(0, False)


def test_mode_changes_only_while_stopped() -> None:
    model = _model()
    model.set_mode("game")
    assert model.mode == "game"
    model.on_status(_status(0, 0, state=9))  # PAUSED
    with pytest.raises(db.Refused):
        model.set_mode("video")
    with pytest.raises(ab.AssetError):
        model.set_mode("chess")


def test_coordinator_state_is_the_one_the_pico_reported() -> None:
    model = _model()
    assert model.pico_state() == "NO STATUS YET"
    model.on_status(_status(0, 0, state=7))
    assert model.state == "IDLE" and model.pico_state() == "READY"
    model.on_status(_status(0, 0, state=8))
    assert model.pico_state() == "COORDINATING PLAYBACK"
    assert not model.stopped()
    assert model.snapshot()["pico_state"] == "COORDINATING PLAYBACK"


class FakeUploadError(uploader.UploadError):
    pass


def _uploadable(model: db.DashboardModel) -> None:
    model.assets = {node: ab.TileAsset(node, bytes([node]), 1, node + 100)
                    for node in (1, 2, 3)}


def test_upload_skips_acked_tiles_and_continues_past_a_failure() -> None:
    model = _model()
    _uploadable(model)
    sent = []
    refuse = {2}

    def send(address, session_id, payload):  # type: ignore[no-untyped-def]
        sent.append(payload[0])
        if payload[0] in refuse:
            raise FakeUploadError("NACK reason 1")
        return None

    addresses = {1: "10.0.0.1", 2: "10.0.0.2", 3: "10.0.0.3"}
    failed = db.run_uploads(model, 7, send, addresses.get)
    assert sent == [1, 2, 3]  # Node 3 still went out after node 2 failed.
    assert failed == {2: "NACK reason 1"}
    assert model.state == "ERROR"
    assert any("node 2 not uploaded" in m for m in model.messages)

    refuse.clear()
    sent.clear()
    failed = db.run_uploads(model, 7, send, addresses.get)
    assert sent == [2]  # Only the failed tile is sent again.
    assert failed == {} and model.state == "VERIFYING UPLOADS"


def test_upload_reports_a_tile_with_no_address() -> None:
    model = _model()
    _uploadable(model)
    failed = db.run_uploads(model, 7, lambda *a: None,
                            {1: "10.0.0.1", 3: "10.0.0.3"}.get)
    assert failed == {2: "no TELEMETRY yet, address unknown"}


def test_upload_waits_for_the_session_before_each_tile() -> None:
    # DS-12 A1: the wait comes before the send; a tile whose session never
    # shows is uploaded all the same, with a message.
    model = _model()
    _uploadable(model)
    order = []

    def wait(node):  # type: ignore[no-untyped-def]
        order.append(("wait", node))
        return node != 2

    def send(address, session_id, payload):  # type: ignore[no-untyped-def]
        order.append(("send", payload[0]))

    failed = db.run_uploads(model, 7, send, "10.0.0.{}".format, wait)
    assert failed == {}
    assert order == [("wait", 1), ("send", 1), ("wait", 2), ("send", 2),
                     ("wait", 3), ("send", 3)]
    assert any("node 2: no TELEMETRY in session 7" in m
               for m in model.messages)


def test_incomplete_upload_counts_when_the_pico_says_ready() -> None:
    # DS-12 C: the last CHUNK_ACKs were lost; the Pico's readiness decides.
    model = _model()
    _uploadable(model)
    asked = []

    def send(address, session_id, payload):  # type: ignore[no-untyped-def]
        if payload[0] in (1, 2):
            raise uploader.UploadIncomplete("55 of 57 chunks ACKed after "
                                            "20 rounds")
        raise uploader.UploadError("tile is playing (BUSY)")

    def ready(node):  # type: ignore[no-untyped-def]
        asked.append(node)
        return node == 1

    failed = db.run_uploads(model, 7, send, "10.0.0.{}".format,
                            ready_of=ready)
    # Node 1 is ready on the Pico; node 2 is not; a refusal is never asked.
    assert asked == [1, 2]
    assert failed == {2: "55 of 57 chunks ACKed after 20 rounds",
                      3: "tile is playing (BUSY)"}
    assert model.pending_uploads(7) == [2, 3]
    assert any("node 1" in m and "Pico counts it ready" in m
               for m in model.messages)


def test_asset_already_acked_for_another_session_is_sent_again() -> None:
    model = _model()
    _uploadable(model)
    model.mark_uploaded(1, 7)
    assert model.pending_uploads(7) == [2, 3]
    assert model.pending_uploads(8) == [1, 2, 3]


def test_session_pick_is_fresh_and_in_range() -> None:
    for seed in range(200):
        value = db.pick_session_id(5, random.Random(seed))
        assert value != 5 and 1 <= value <= 255


def test_session_resolve_saves_and_differs_next_time(tmp_path) -> None:  # type: ignore[no-untyped-def]
    path = str(tmp_path / "last")
    first = db.resolve_session(None, path)
    second = db.resolve_session(None, path)
    assert first != second and db.load_last_session(path) == second
    assert db.resolve_session(9, path) == 9
    with pytest.raises(ValueError):
        db.resolve_session(256, path)
    with pytest.raises(ValueError):
        db.resolve_session(0, path)
