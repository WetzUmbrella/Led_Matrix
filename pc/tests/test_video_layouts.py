"""pytest: video on any layout (whack-a-mole re-plan, build step 6).

Runs the PC path the dashboard uses (DashboardShell.publish, send_config and
the resumable upload) with a fake link, telemetry and uploader, so no Pico or
tile is needed. Shapes: 1x5, 2x2 and 5 tiles in a 3x3 wall, plus the 1x9
strip GIF.
"""

from __future__ import annotations

import json
import os
import sys
import types
import zlib

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, os.path.dirname(__file__))
import asset_builder as ab  # noqa: E402
import dashboard as db  # noqa: E402
import numbered_pattern as npat  # noqa: E402
import pico_link as plink  # noqa: E402
import proto_layout as pl  # noqa: E402

CONFIGS = [
    ("1x5", [1, 2, 3, 4, 5, 0, 0, 0, 0]),
    ("2x2", [1, 2, 3, 4, 0, 0, 0, 0, 0]),
    ("3x3", [1, 2, 0, 3, 0, 4, 5, 0, 0]),  # 5 tiles in a 3x3 wall.
]


class FakeLink:
    """Records SESSION_CONFIG; no Pico behind it."""

    def __init__(self) -> None:
        self.configs = []  # type: list
        self.taps = []  # type: list

    def send_session_config(self, session_id, rows, cols, policy,  # type: ignore
                            node_map, frame_count, crcs):
        self.configs.append({"session_id": session_id, "rows": rows,
                             "cols": cols, "policy": policy,
                             "node_map": list(node_map),
                             "frame_count": frame_count,
                             "crcs": list(crcs)})
        return b""

    def poll_status(self):  # type: ignore[no-untyped-def]
        return None

    def next_tap(self, timeout_s):  # type: ignore[no-untyped-def]
        return None


class FakeTelemetry:
    tiles = {}  # type: dict

    def poll(self) -> int:
        return 0

    def address_of(self, node: int):  # type: ignore[no-untyped-def]
        return "10.0.0.%d" % node


class FakeUploader:
    def __init__(self) -> None:
        self.sent = []  # type: list

    def upload_asset(self, address, session_id, payload,  # type: ignore
                     already_ready=None):
        self.sent.append((address, session_id, len(payload)))
        return types.SimpleNamespace(acked=set(), chunks=1, resent=0,
                                     seconds=0.0, already_ready=False)


def _shell() -> db.DashboardShell:
    shell = db.DashboardShell.__new__(db.DashboardShell)
    shell.model = db.DashboardModel(clock=lambda: 0.0)
    shell.link = FakeLink()
    shell.telemetry = FakeTelemetry()
    shell.uploader = FakeUploader()
    shell.session_id = 7
    # The Pico reports READY (coordinator state 7): the wall is stopped.
    shell.model.on_status(plink.Status(0, 0, 7, 0.0))
    return shell


def _gif(tmp_path, layout: str, node_map) -> str:  # type: ignore[no-untyped-def]
    frames, delays = npat.make_frames(layout, node_map)
    path = str(tmp_path / ("%s.gif" % layout))
    frames[0].save(path, save_all=True, append_images=frames[1:],
                   duration=delays, loop=0)
    return path


@pytest.mark.parametrize("layout,node_map", CONFIGS)
def test_session_config_round_trips_the_wire_map(layout, node_map,  # type: ignore
                                                 tmp_path) -> None:
    shell = _shell()
    assert shell.model.configure(_gif(tmp_path, layout, node_map), layout,
                                 node_map, 1, 8)
    assert shell.publish() == {}
    sent = shell.link.configs[-1]
    rows, cols = ab.parse_shape(layout)
    assert (sent["rows"], sent["cols"]) == (rows, cols)
    assert sent["node_map"] == node_map
    crc_by_node = {n: a.crc32 for n, a in shell.model.assets.items()}
    assert sent["crcs"] == [crc_by_node.get(n, 0) if n else 0
                            for n in node_map]
    wire = pl.encode_session_config(
        session_id=7, seq=1, rows=sent["rows"], cols=sent["cols"],
        policy=1, node_map=sent["node_map"],
        frame_count=sent["frame_count"], crc32=sent["crcs"])
    decoded = pl.decode(wire)
    assert (decoded["rows"], decoded["cols"]) == (rows, cols)
    assert list(decoded["node_map"]) == node_map  # type: ignore[arg-type]
    assert decoded["frame_count"] == sent["frame_count"]


@pytest.mark.parametrize("layout,node_map", CONFIGS)
def test_each_tile_gets_its_own_region_and_manifest(layout, node_map,  # type: ignore
                                                    tmp_path) -> None:
    frames, delays = npat.make_frames(layout, node_map)
    assets = ab.build_assets(frames, delays, layout, node_map)
    assert sorted(assets) == sorted(n for n in node_map if n)
    for node, asset in assets.items():
        assert asset.crc32 == zlib.crc32(asset.payload)
        manifest = pl.decode_manifest(ab.build_manifest(7, 1, asset))
        assert manifest["frame_count"] == asset.frame_count
        assert manifest["crc32"] == asset.crc32
        first = pl.unpack_frame(asset.payload[:pl.FRAME_BYTES])
        expected = [ab.scale(c) for px in npat.region_pixels(node, 0)
                    for c in px]
        assert first == expected


def test_required_mask_follows_the_map() -> None:
    for _layout, node_map in CONFIGS:
        assert plink.nodes_in(plink.required_mask(node_map)) == \
            sorted(n for n in node_map if n)


def test_a_grid_change_rebuilds_and_uploads_the_new_shape(tmp_path) -> None:  # type: ignore
    shell = _shell()
    node_map = [1, 2, 3, 4, 0, 0, 0, 0, 0]
    shell.model.configure(_gif(tmp_path, "2x2", node_map), "2x2",
                          node_map, 1, 8)
    shell.publish()
    assert sorted(shell.model.assets) == [1, 2, 3, 4]
    shell.set_grid(1, 5)  # Row 0 keeps tiles 1 and 2; row 1 is gone.
    assert shell.model.node_map == [1, 2, 0, 0, 0, 0, 0, 0, 0]
    sent = shell.link.configs[-1]
    assert (sent["rows"], sent["cols"]) == (1, 5)
    assert sorted(shell.model.assets) == [1, 2]


def test_only_changed_tiles_are_sent_again(tmp_path) -> None:  # type: ignore
    shell = _shell()
    node_map = [1, 2, 3, 4, 0, 0, 0, 0, 0]
    shell.model.configure(_gif(tmp_path, "2x2", node_map), "2x2",
                          node_map, 1, 8)
    shell.publish()
    assert len(shell.uploader.sent) == 4
    shell.set_slot(3, False)  # Tile 4 leaves; the other regions do not move.
    assert shell.uploader.sent[4:] == []
    assert shell.model.node_map == [1, 2, 3, 0, 0, 0, 0, 0, 0]


def test_strip_gif_builds_from_the_command_line(tmp_path, capsys) -> None:  # type: ignore
    gif = _gif(tmp_path, "1x9", list(range(1, 10)))
    out = str(tmp_path / "out")
    code = ab.main([gif, "--layout", "1x9", "--map",
                    "1,2,3,4,5,6,7,8,9", "--out", out])
    assert code == 0
    with open(os.path.join(out, "session.json"), encoding="utf-8") as h:
        info = json.load(h)
    assert info["layout"] == "1x9" and len(info["tiles"]) == 9
    assert "node 9:" in capsys.readouterr().out


def test_strip_gif_has_a_region_per_node_across_the_strip(tmp_path) -> None:  # type: ignore
    gif = _gif(tmp_path, "1x9", list(range(1, 10)))
    frames, delays = ab.load_gif(gif)
    tiles = ab.partition_tiles(frames[:1], "1x9", list(range(1, 10)))
    for node in range(1, 10):
        assert tiles[node][0] == npat.region_pixels(node, 0)


def test_upload_asks_the_pico_for_the_right_tile(tmp_path) -> None:  # type: ignore
    """The readiness check given to the uploader is for the tile at that
    address, read from the Pico's STATUS (bench 9 Nov: a tile that already
    holds the asset tells only the Pico)."""
    shell = _shell()
    row = types.SimpleNamespace(address="10.0.0.2")
    shell.telemetry.tiles = {2: row}
    checks = {}

    def upload_asset(address, session_id, payload,  # type: ignore
                     already_ready=None):
        checks[address] = already_ready
        return types.SimpleNamespace(acked=set(), chunks=1, resent=0,
                                     seconds=0.0, already_ready=True)

    shell.uploader.upload_asset = upload_asset
    shell._send_asset("10.0.0.2", 7, b"x")
    shell.model.on_status(plink.Status(0b010, 0b010, 7, 0.0))
    assert checks["10.0.0.2"]() is True  # Bit 1 = node 2.
    shell.model.on_status(plink.Status(0b001, 0b011, 7, 0.0))
    assert checks["10.0.0.2"]() is False
    assert any("already holds" in m for m in shell.model.messages)
    shell._send_asset("10.0.0.9", 7, b"x")  # No TELEMETRY from this address.
    assert checks["10.0.0.9"]() is False
