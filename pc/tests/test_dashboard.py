"""pytest: pc/dashboard.py model and pc/pico_link.py (T-B1-05).

Owner: Jolyn (Buddy 1). Implemented by Ahsanul (B4) for Jolyn.
"""

from __future__ import annotations

import os
import socket
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import dashboard as db  # noqa: E402
import pico_link as plink  # noqa: E402
import proto_layout as pl  # noqa: E402

MAP_2 = [1, 2, 0, 0, 0, 0, 0, 0, 0]


def _status(readiness: int, online: int) -> plink.Status:
    return plink.Status(readiness, online, 5, 0.0)


@pytest.mark.parametrize("value", [9, 40, 255])
def test_brightness_9_40_255_refused(value: int) -> None:
    model = db.DashboardModel()
    assert not model.set_brightness(value)
    assert not model.configure("x.gif", "2x2", MAP_2, 1, value)
    assert model.brightness == 8


def test_start_disabled_until_bitmap_complete() -> None:
    model = db.DashboardModel()
    assert model.configure("x.gif", "2x2", MAP_2, 1, 8)
    model.state = "VERIFYING UPLOADS"
    model.on_status(_status(0b001, 0b011))
    assert not model.start_enabled()
    assert model.missing() == [2]
    model.on_status(_status(0b011, 0b011))
    assert model.start_enabled()
    assert model.state == "READY"


def test_loss_names_the_node() -> None:
    model = db.DashboardModel(clock=lambda: 12.5)
    model.configure("x.gif", "2x2", MAP_2, 1, 8)
    model.on_status(_status(0b011, 0b011))
    model.messages.clear()
    model.on_status(_status(0b001, 0b001))
    assert model.messages == ["12.500 tile 2 lost (Pico STATUS online bit "
                              "cleared)"]


def test_new_gif_refused_while_playing() -> None:
    model = db.DashboardModel()
    model.configure("x.gif", "1x1", [1] + [0] * 8, 1, 8)
    model.state = "MONITORING PLAYBACK"
    assert not model.configure("y.gif", "1x1", [1] + [0] * 8, 1, 8)


def test_brightness_change_drops_assets() -> None:
    model = db.DashboardModel()
    model.configure("x.gif", "1x1", [1] + [0] * 8, 1, 8)
    model.assets = {1: object()}  # type: ignore[dict-item]
    model.state = "READY"
    assert model.set_brightness(4)
    assert model.assets == {}
    assert any("rebuilt and re-uploaded" in m for m in model.messages)


def test_pico_link_sends_config_and_commands() -> None:
    pico = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    pico.bind(("127.0.0.1", 0))
    pico.settimeout(1.0)
    pc = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    pc.bind(("127.0.0.1", 0))
    link = plink.PicoLink("127.0.0.1", sock=pc)
    link.pico = ("127.0.0.1", pico.getsockname()[1])
    link.send_session_config(7, 1, 1, [1, 2, 0, 3, 4, 0, 0, 0, 0], 600,
                             [11, 22, 0, 33, 44, 0, 0, 0, 0])
    config = pl.decode(pico.recvfrom(128)[0])
    assert config["type"] == "SESSION_CONFIG"
    assert config["frame_count"] == 600
    for send, name in ((link.start, "START_AT"), (link.pause, "PAUSE"),
                       (link.resume, "RESUME_AT"), (link.stop, "STOP")):
        send()
        msg = pl.decode(pico.recvfrom(128)[0])
        assert msg["type"] == name and msg["session_id"] == 7
    status = pl.encode_status(session_id=7, seq=1, readiness=3, online=3,
                              state=7)
    pico.sendto(status, pc.getsockname())
    import time
    time.sleep(0.05)
    got = link.poll_status()
    assert got is not None and got.readiness == 3
    assert got.state_name() == "READY"
    pico.close()
    pc.close()


def test_required_mask() -> None:
    assert plink.required_mask([1, 2, 0, 3, 4, 0, 0, 0, 0]) == 0b1111
    assert plink.nodes_in(0b1010) == [2, 4]
