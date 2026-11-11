"""pytest: pc/dashboard.py model and pc/pico_link.py (T-B1-05).

Owner: Jolyn (Buddy 1). Implemented by Ahsanul (B4) for Jolyn.
"""

from __future__ import annotations

import os
import socket
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import asset_builder as ab  # noqa: E402
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
    wire_map = [1, 2, 3, 4, 0, 0, 0, 0, 0]
    crcs = [11, 22, 33, 44, 0, 0, 0, 0, 0]
    link.send_session_config(7, 2, 2, 1, wire_map, 600, crcs)
    config = pl.decode(pico.recvfrom(128)[0])
    assert config["type"] == "SESSION_CONFIG"
    assert config["frame_count"] == 600
    assert (config["rows"], config["cols"]) == (2, 2)
    assert config["node_map"] == wire_map
    for send, name in ((link.start, "START_AT"), (link.pause, "PAUSE"),
                       (link.resume, "RESUME_AT"), (link.stop, "STOP")):
        send()
        msg = pl.decode(pico.recvfrom(128)[0])
        assert msg["type"] == name and msg["session_id"] == 7
    status = pl.encode_status(session_id=7, seq=1, readiness=3, online=3,
                              state=7, score=-3, round_state=0, level=0,
                              missed=0, wrong=0, shown=0, time_left_s=0,
                              reaction_ms=0)
    pico.sendto(status, pc.getsockname())
    import time
    time.sleep(0.05)
    got = link.poll_status()
    assert got is not None and got.readiness == 3
    assert got.state_name() == "READY"
    assert got.score == -3
    pico.close()
    pc.close()


def test_required_mask() -> None:
    assert plink.required_mask([1, 2, 0, 3, 4, 0, 0, 0, 0]) == 0b1111
    assert plink.nodes_in(0b1010) == [2, 4]


def test_next_tap_returns_the_board_number_of_a_tap() -> None:
    pico = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    pico.bind(("127.0.0.1", 0))
    pc = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    pc.bind(("127.0.0.1", 0))
    link = plink.PicoLink("127.0.0.1", sock=pc)
    link.pico = ("127.0.0.1", pico.getsockname()[1])
    tap = pl.encode("TAP", {"session_id": 7, "seq": 3, "node_id": 4,
                            "peak": 1000, "t_tap": 123})
    pico.sendto(tap, pc.getsockname())
    assert link.next_tap(1.0) == 4
    assert link.next_tap(0.05) is None
    pico.close()
    pc.close()


def test_send_mode_is_a_game_control_to_the_pico() -> None:
    pico = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    pico.bind(("127.0.0.1", 0))
    pico.settimeout(1.0)
    pc = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    pc.bind(("127.0.0.1", 0))
    link = plink.PicoLink("127.0.0.1", sock=pc)
    link.pico = ("127.0.0.1", pico.getsockname()[1])
    link.session_id = 7
    link.send_mode(plink.MODE_GAME)
    msg = pl.decode(pico.recvfrom(128)[0])
    assert msg["type"] == "GAME_CONTROL"
    assert (msg["node_id"], msg["command"], msg["session_id"]) == (0, 1, 7)
    pico.close()
    pc.close()


def _ready_status(state: int, score: int = 0,
                  round_state: int = 0, **counts: int) -> plink.Status:
    """A status with both tiles ready, in the given coordinator state."""
    return plink.Status(0b11, 0b11, state, 0.0, score,
                        round_state=round_state, **counts)


class _FakeLink:
    """Records the GAME_CONTROL commands; answers ACK or not as set."""

    def __init__(self, ack: bool) -> None:
        self.ack = ack
        self.sent = []  # type: list
        self.args = []  # type: list
        self.taps = []  # type: list

    def poll_status(self):  # type: ignore[no-untyped-def]
        return None

    def send_game_control(self, command: int, level: int = 0,
                          threshold: int = 0) -> bool:
        self.sent.append(command)
        self.args.append((level, threshold))
        return self.ack


class _FakeTelemetry:
    def poll(self) -> int:
        return 0


def _shell(ack: bool = True, state: int = 7,
           mode: str = "game") -> db.DashboardShell:
    """A shell with no sockets: the model, a fake link and telemetry."""
    shell = db.DashboardShell.__new__(db.DashboardShell)
    shell.model = db.DashboardModel()
    shell.model.status = _ready_status(state)
    shell.model.mode = mode
    shell.link = _FakeLink(ack)  # type: ignore[assignment]
    shell.telemetry = _FakeTelemetry()  # type: ignore[assignment]
    return shell


def test_round_start_needs_game_mode_and_ready() -> None:
    shell = _shell(mode="video", state=7)
    with pytest.raises(db.Refused):
        shell.start_game()
    assert shell.link.sent == []
    shell.model.mode = "game"
    shell.model.status = _ready_status(5)  # SYNCHRONISING: not READY.
    with pytest.raises(db.Refused):
        shell.start_game()
    shell.model.status = _ready_status(7)
    shell.start_game()
    assert shell.link.sent == [plink.GAME_START]


def test_round_start_not_acknowledged_is_an_error() -> None:
    shell = _shell(ack=False, state=7, mode="game")
    with pytest.raises(OSError):
        shell.start_game()


def test_abort_only_while_a_round_runs() -> None:
    shell = _shell(state=7)
    with pytest.raises(db.Refused):
        shell.abort_game()
    assert shell.link.sent == []
    shell.model.status = _ready_status(12, score=1)
    snap = shell.abort_game()
    assert shell.link.sent == [plink.GAME_END]
    assert snap["game_running"] is True and snap["score"] == 1


def test_mode_switch_keeps_the_old_mode_when_not_acknowledged() -> None:
    shell = _shell(ack=False, state=7, mode="video")
    with pytest.raises(OSError):
        shell.set_mode("game")
    assert shell.model.mode == "video"
    shell.link.ack = True
    shell.set_mode("game")
    assert shell.model.mode == "game"
    # Refused once (not acknowledged), then sent again and acknowledged.
    assert shell.link.sent == [plink.MODE_GAME, plink.MODE_GAME]


def test_snapshot_says_when_a_round_can_start_or_runs() -> None:
    shell = _shell(state=7, mode="game")
    snap = shell.model.snapshot()
    assert snap["game_start_enabled"] is True and snap["game_running"] is False
    shell.model.status = _ready_status(12)
    snap = shell.model.snapshot()
    assert snap["game_start_enabled"] is False and snap["game_running"] is True


def _udp_pair():  # type: ignore[no-untyped-def]
    pico = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    pico.bind(("127.0.0.1", 0))
    pico.settimeout(2.0)
    pc = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    pc.bind(("127.0.0.1", 0))
    link = plink.PicoLink("127.0.0.1", sock=pc)
    link.pico = ("127.0.0.1", pico.getsockname()[1])
    link.session_id = 7
    return pico, pc, link


def test_send_game_control_is_resent_with_its_seq_until_acked(
        monkeypatch) -> None:  # type: ignore[no-untyped-def]
    import threading
    monkeypatch.setattr(plink, "GAME_ACK_WAIT_S", 0.3)
    pico, pc, link = _udp_pair()
    seen = []  # type: list

    def pico_side() -> None:
        for index in range(2):
            msg = pl.decode(pico.recvfrom(128)[0])
            seen.append(msg)
            if index == 1:  # The first datagram is lost: ACK the second.
                pico.sendto(pl.encode("ACK", {
                    "session_id": 7, "seq": 9, "node_id": 0,
                    "acked_seq": msg["seq"]}), pc.getsockname())

    thread = threading.Thread(target=pico_side)
    thread.start()
    assert link.send_game_control(plink.GAME_START) is True
    thread.join(3.0)
    assert seen[0]["seq"] == seen[1]["seq"]
    assert (seen[0]["node_id"], seen[0]["command"]) == (0, plink.GAME_START)
    pico.close()
    pc.close()


def test_send_game_control_gives_up_and_ignores_other_nodes(
        monkeypatch) -> None:  # type: ignore[no-untyped-def]
    monkeypatch.setattr(plink, "GAME_TRIES", 2)
    monkeypatch.setattr(plink, "GAME_ACK_WAIT_S", 0.05)
    pico, pc, link = _udp_pair()
    assert link.send_game_control(plink.MODE_GAME) is False
    assert pl.decode(pico.recvfrom(128)[0])["command"] == plink.MODE_GAME
    assert pl.decode(pico.recvfrom(128)[0])["command"] == plink.MODE_GAME
    # An ACK from a tile (node 3) is not the Pico's ACK of the PC command.
    pico.sendto(pl.encode("ACK", {"session_id": 7, "seq": 9, "node_id": 3,
                                  "acked_seq": link.seq}), pc.getsockname())
    assert link.poll_status() is None and link.acks == []
    pico.close()
    pc.close()


def test_last_score_is_none_before_any_round() -> None:
    model = db.DashboardModel()
    model.on_status(_ready_status(7))
    assert model.snapshot()["last_score"] is None


PLAYING, RESULTS, ARMED, IDLE = 3, 4, 1, 0


def test_last_score_keeps_the_final_score_after_the_round_ends() -> None:
    model = db.DashboardModel()
    model.on_status(_ready_status(7))
    model.on_status(_ready_status(12, score=3, round_state=PLAYING))
    model.on_status(_ready_status(12, score=5, round_state=PLAYING))
    assert model.snapshot()["last_score"] is None  # Still in the round.
    # RESULTS: READY, and the Pico keeps the final score on STATUS.
    model.on_status(_ready_status(7, score=8, round_state=RESULTS,
                                  shown=10, missed=2, wrong=1,
                                  reaction_ms=420))
    snap = model.snapshot()
    assert snap["last_score"] == 8
    assert snap["game_running"] is False
    assert snap["round_state"] == "RESULTS"
    assert snap["last_result"] == {
        "score": 8, "hits": 8, "missed": 2, "wrong": 1, "shown": 10,
        "reaction_ms": 420, "stars": 3}


def test_last_score_is_replaced_by_the_next_round() -> None:
    model = db.DashboardModel()
    model.on_status(_ready_status(12, score=2, round_state=PLAYING))
    model.on_status(_ready_status(7, score=2, round_state=RESULTS))
    assert model.snapshot()["last_score"] == 2
    model.on_status(_ready_status(12, round_state=ARMED))
    model.on_status(_ready_status(12, score=4, round_state=PLAYING))
    model.on_status(_ready_status(7, score=4, round_state=RESULTS))
    assert model.snapshot()["last_score"] == 4


def test_old_results_at_start_are_not_a_new_round() -> None:
    model = db.DashboardModel()
    model.on_status(_ready_status(7, score=38, round_state=RESULTS))
    model.on_status(_ready_status(7, score=38, round_state=RESULTS))
    assert model.snapshot()["last_score"] is None
    assert not any("round over" in line for line in model.messages)


def test_an_aborted_arm_is_not_a_round() -> None:
    model = db.DashboardModel()
    model.on_status(_ready_status(12, round_state=ARMED))
    model.on_status(_ready_status(7, round_state=IDLE))
    assert model.snapshot()["last_score"] is None
    assert any("not started" in line for line in model.messages)


def test_last_score_is_in_the_json_state() -> None:
    import json

    model = db.DashboardModel()
    model.on_status(_ready_status(12, score=1, round_state=PLAYING))
    model.on_status(_ready_status(7, score=1, round_state=RESULTS))
    body = json.loads(json.dumps(model.snapshot()))
    assert body["last_score"] == 1 and body["score"] == 1


def test_hits_do_not_count_a_mole_still_up() -> None:
    # Mid-round: 3 moles shown, 2 hit, 1 still up and not yet judged.
    assert _ready_status(12, score=2, shown=3, missed=0).hits() == 2


def test_stars_follow_the_pico_rule() -> None:
    # 80% or more 3, 50% or more 2, otherwise 1; no moles: 1.
    assert _ready_status(7, score=8, shown=10, missed=2).stars() == 3
    assert _ready_status(7, score=7, shown=10, missed=3).stars() == 2
    assert _ready_status(7, score=5, shown=10, missed=5).stars() == 2
    assert _ready_status(7, score=4, shown=10, missed=6).stars() == 1
    assert _ready_status(7).stars() == 1


def test_arm_sends_the_level_and_threshold() -> None:
    shell = _shell(state=7, mode="game")
    shell.set_level("normal")
    shell.set_threshold(300)
    shell.arm_game()
    assert shell.link.sent == [plink.GAME_ARM]
    assert shell.link.args == [(1, 15)]
    with pytest.raises(ab.AssetError):
        shell.set_level("expert")
    for bad in (-20, 40, 310, 5020):
        with pytest.raises(ab.AssetError):
            shell.set_threshold(bad)
    shell.set_threshold(0)
    assert shell.model.threshold_code() == 0


def test_arm_needs_game_mode_and_ready() -> None:
    shell = _shell(state=7, mode="video")
    with pytest.raises(db.Refused):
        shell.arm_game()
    shell.model.mode = "game"
    shell.model.status = _ready_status(12, round_state=PLAYING)
    with pytest.raises(db.Refused):
        shell.arm_game()
    # RESULTS is READY: the next round can be armed.
    shell.model.status = _ready_status(7, round_state=RESULTS)
    shell.arm_game()
    assert shell.link.sent == [plink.GAME_ARM]


def test_start_now_only_while_armed() -> None:
    shell = _shell(state=12, mode="game")
    shell.model.status = _ready_status(12, round_state=PLAYING)
    with pytest.raises(db.Refused):
        shell.start_now()
    shell.model.status = _ready_status(12, round_state=ARMED)
    snap = shell.start_now()
    assert shell.link.sent == [plink.GAME_START_NOW]
    assert snap["start_now_enabled"] is True


def test_tap_test_needs_ready_game_mode() -> None:
    shell = _shell(state=7, mode="video")
    with pytest.raises(db.Refused):
        shell.set_tap_test(True)
    shell.model.mode = "game"
    shell.model.status = _ready_status(12, round_state=ARMED)
    with pytest.raises(db.Refused):
        shell.set_tap_test(True)
    shell.model.status = _ready_status(7)
    assert shell.set_tap_test(True)["tap_test"] is True
    assert shell.set_tap_test(False)["tap_test"] is False
    assert shell.link.sent == [plink.GAME_TAP_TEST_ON,
                               plink.GAME_TAP_TEST_OFF]


def test_tap_test_not_acknowledged_keeps_the_old_state() -> None:
    shell = _shell(ack=False, state=7, mode="game")
    with pytest.raises(OSError):
        shell.set_tap_test(True)
    assert shell.model.tap_test is False


class _LayoutLink(_FakeLink):
    """Records SESSION_CONFIG as well."""

    def __init__(self) -> None:
        super().__init__(True)
        self.configs = []  # type: list

    def send_session_config(self, session_id, rows, cols, policy,  # type: ignore
                            node_map, frame_count, crcs):
        self.configs.append((session_id, rows, cols, list(node_map),
                             frame_count, list(crcs)))
        return b""


def test_layout_without_a_gif_is_sent_for_a_game() -> None:
    # A14: no GIF chosen, a grid change sends the layout with 1 frame and
    # no CRCs, and nothing is uploaded.
    shell = _shell(state=7, mode="game")
    shell.link = _LayoutLink()  # type: ignore[assignment]
    shell.session_id = 9
    shell.uploader = None  # type: ignore[assignment]
    shell.set_grid(1, 2)
    assert len(shell.link.configs) == 1
    session, rows, cols, _node_map, frames, crcs = shell.link.configs[0]
    assert (session, rows, cols, frames) == (9, 1, 2, 1)
    assert crcs == [0] * len(crcs)
