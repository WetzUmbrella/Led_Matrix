"""pytest: pc/web.py, the website over the dashboard (whack-a-mole re-plan,
build step 5). Every endpoint is called over HTTP on loopback with a fake
shell, so no Pico, tile or GIF is needed.
"""

from __future__ import annotations

import json
import os
import sys
import threading
import urllib.error
import urllib.request

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import asset_builder as ab  # noqa: E402
import dashboard as db  # noqa: E402
import web  # noqa: E402


class FakeShell:
    """Records every call; ``playing`` makes changes refused like the real
    shell does while the Pico is playing."""

    def __init__(self) -> None:
        self.calls = []  # type: list
        self.playing = False
        self.no_ack = False
        self.snap = {"rows": 1, "cols": 2, "node_map": [1, 2, 0, 0, 0, 0,
                                                       0, 0, 0],
                     "pico_state": "READY", "score": 0, "mode": "video",
                     "stopped": True}

    def _change(self, name, *args):  # type: ignore[no-untyped-def]
        self.calls.append((name,) + args)
        if self.playing:
            raise db.Refused("stop playback first")
        return dict(self.snap)

    def snapshot(self):  # type: ignore[no-untyped-def]
        self.calls.append(("snapshot",))
        return dict(self.snap)

    def set_grid(self, rows, cols):  # type: ignore[no-untyped-def]
        if rows * cols > 9:
            raise ab.AssetError("rows x cols must be at most 9 tiles")
        return self._change("set_grid", rows, cols)

    def arm_place(self, slot):  # type: ignore[no-untyped-def]
        return self._change("arm_place", slot)

    def set_slot(self, slot, on):  # type: ignore[no-untyped-def]
        return self._change("set_slot", slot, on)

    def set_mode(self, mode):  # type: ignore[no-untyped-def]
        if mode not in ("video", "game"):
            raise ab.AssetError("mode must be video or game")
        return self._change("set_mode", mode)

    def start_game(self):  # type: ignore[no-untyped-def]
        self.calls.append(("start_game",))
        if self.playing:
            raise db.Refused("the wall must be READY to start a round")
        if self.no_ack:
            raise OSError("the Pico did not acknowledge the round start")
        return dict(self.snap)

    def abort_game(self):  # type: ignore[no-untyped-def]
        self.calls.append(("abort_game",))
        return dict(self.snap)

    def set_level(self, level):  # type: ignore[no-untyped-def]
        if level not in ("easy", "normal", "hard"):
            raise ab.AssetError("level must be easy, normal or hard")
        self.calls.append(("set_level", level))
        return dict(self.snap)

    def arm_game(self):  # type: ignore[no-untyped-def]
        self.calls.append(("arm_game",))
        return dict(self.snap)

    def start_now(self):  # type: ignore[no-untyped-def]
        self.calls.append(("start_now",))
        if self.playing:
            raise db.Refused("no round is armed")
        return dict(self.snap)

    def set_tap_test(self, on):  # type: ignore[no-untyped-def]
        self.calls.append(("set_tap_test", on))
        return dict(self.snap)

    def set_threshold(self, mg):  # type: ignore[no-untyped-def]
        if mg < 0:
            raise ab.AssetError("threshold must be 0 or 60 to 5000 mg")
        self.calls.append(("set_threshold", mg))
        return dict(self.snap)


@pytest.fixture
def served():  # type: ignore[no-untyped-def]
    shell = FakeShell()
    server = web.make_server(shell, "127.0.0.1", 0)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    yield shell, "http://127.0.0.1:%d" % server.server_address[1]
    server.shutdown()
    server.server_close()


def call(base, method, path, body=None, raw=None):  # type: ignore
    """(status, parsed JSON or text, headers) of one request."""
    if raw is None and body is not None:
        raw = json.dumps(body).encode("utf-8")
    request = urllib.request.Request(
        base + path, data=raw, method=method,
        headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(request, timeout=5) as res:
            data = res.read()
            status, headers = res.status, res.headers
    except urllib.error.HTTPError as err:
        data, status, headers = err.read(), err.code, err.headers
    text = data.decode("utf-8")
    kind = headers.get("Content-Type", "")
    return status, (json.loads(text) if "json" in kind else text), headers


def test_state_reports_grid_map_pico_state_mode_and_score(served) -> None:  # type: ignore
    shell, base = served
    status, body, headers = call(base, "GET", "/api/state")
    assert status == 200 and "json" in headers["Content-Type"]
    assert (body["rows"], body["cols"]) == (1, 2)
    assert body["node_map"][:2] == [1, 2]
    assert body["pico_state"] == "READY" and body["mode"] == "video"
    assert body["score"] == 0
    assert shell.calls == [("snapshot",)]


def test_grid_passes_rows_and_cols(served) -> None:  # type: ignore
    shell, base = served
    status, _body, _h = call(base, "POST", "/api/grid", {"rows": 2,
                                                         "cols": 2})
    assert status == 200
    assert shell.calls == [("set_grid", 2, 2)]


def test_grid_bad_fields_are_400(served) -> None:  # type: ignore
    shell, base = served
    for body in ({"rows": "2", "cols": 2}, {"rows": True, "cols": 2},
                 {"cols": 2}, {"rows": 4, "cols": 4}):
        status, reply, _h = call(base, "POST", "/api/grid", body)
        assert status == 400 and "error" in reply
    assert shell.calls == []  # Refused before the shell changed anything.


def test_grid_refused_while_playing_is_409(served) -> None:  # type: ignore
    shell, base = served
    shell.playing = True
    status, reply, _h = call(base, "POST", "/api/grid", {"rows": 1,
                                                         "cols": 3})
    assert status == 409 and "stop playback" in reply["error"]


def test_place_arms_the_slot(served) -> None:  # type: ignore
    shell, base = served
    status, _body, _h = call(base, "POST", "/api/place", {"slot": 3})
    assert status == 200 and shell.calls == [("arm_place", 3)]


def test_place_needs_an_integer_slot(served) -> None:  # type: ignore
    shell, base = served
    status, _reply, _h = call(base, "POST", "/api/place", {"slot": None})
    assert status == 400 and shell.calls == []


def test_slot_on_and_off(served) -> None:  # type: ignore
    shell, base = served
    assert call(base, "POST", "/api/slot", {"slot": 1, "on": False})[0] == 200
    assert call(base, "POST", "/api/slot", {"slot": 1, "on": True})[0] == 200
    assert shell.calls == [("set_slot", 1, False), ("set_slot", 1, True)]


def test_slot_needs_true_or_false(served) -> None:  # type: ignore
    shell, base = served
    status, _reply, _h = call(base, "POST", "/api/slot",
                              {"slot": 1, "on": 1})
    assert status == 400 and shell.calls == []


def test_mode_video_and_game(served) -> None:  # type: ignore
    shell, base = served
    assert call(base, "POST", "/api/mode", {"mode": "game"})[0] == 200
    assert call(base, "POST", "/api/mode", {"mode": "video"})[0] == 200
    assert shell.calls == [("set_mode", "game"), ("set_mode", "video")]


def test_mode_refused_while_playing_and_bad_names(served) -> None:  # type: ignore
    shell, base = served
    assert call(base, "POST", "/api/mode", {"mode": "chess"})[0] == 400
    shell.playing = True
    status, _reply, _h = call(base, "POST", "/api/mode", {"mode": "game"})
    assert status == 409


def test_game_start_and_abort_reach_the_shell(served) -> None:  # type: ignore
    shell, base = served
    assert call(base, "POST", "/api/game/start")[0] == 200
    assert call(base, "POST", "/api/game/abort")[0] == 200
    assert [c[0] for c in shell.calls] == ["start_game", "abort_game"]


def test_game_start_refused_is_409_and_no_ack_is_500(served) -> None:  # type: ignore
    shell, base = served
    shell.playing = True
    status, reply, _h = call(base, "POST", "/api/game/start")
    assert status == 409 and "READY" in reply["error"]
    shell.playing = False
    shell.no_ack = True
    status, reply, _h = call(base, "POST", "/api/game/start")
    assert status == 500 and "acknowledge" in reply["error"]


def test_get_on_a_post_endpoint_is_405(served) -> None:  # type: ignore
    shell, base = served
    status, _reply, _h = call(base, "GET", "/api/grid")
    assert status == 405 and shell.calls == []


def test_unknown_endpoint_is_404(served) -> None:  # type: ignore
    _shell, base = served
    assert call(base, "GET", "/api/nope")[0] == 404
    assert call(base, "POST", "/api/nope", {})[0] == 404


def test_bad_bodies_are_400(served) -> None:  # type: ignore
    shell, base = served
    assert call(base, "POST", "/api/grid", raw=b"{not json")[0] == 400
    assert call(base, "POST", "/api/grid", raw=b"[1, 2]")[0] == 400
    assert call(base, "POST", "/api/grid", raw=b"x" * 5000)[0] == 400
    assert shell.calls == []


def test_empty_post_body_is_an_empty_object(served) -> None:  # type: ignore
    _shell, base = served
    status, reply, _h = call(base, "POST", "/api/grid")
    assert status == 400 and "rows" in reply["error"]


def test_page_is_served_from_the_pc_with_no_cdn(served) -> None:  # type: ignore
    _shell, base = served
    status, page, headers = call(base, "GET", "/")
    assert status == 200 and headers["Content-Type"].startswith("text/html")
    assert "/api/state" in page and "setTimeout" in page
    assert "http://" not in page and "https://" not in page
    assert call(base, "GET", "/index.html")[0] == 200


def test_dispatch_maps_os_errors_to_500() -> None:
    class Broken(FakeShell):
        def set_slot(self, slot, on):  # type: ignore
            raise OSError("disk gone")

    status, reply = web.dispatch(Broken(), "POST", "/api/slot",
                                 {"slot": 1, "on": True})
    assert status == 500 and reply == {"error": "disk gone"}


def test_round_commands_reach_the_shell(served) -> None:  # type: ignore
    shell, base = served
    assert call(base, "POST", "/api/game/arm", {"level": "hard"})[0] == 200
    assert call(base, "POST", "/api/game/arm")[0] == 200
    assert call(base, "POST", "/api/game/now")[0] == 200
    assert call(base, "POST", "/api/game/tap_test", {"on": True})[0] == 200
    assert call(base, "POST", "/api/game/threshold", {"mg": 300})[0] == 200
    assert shell.calls == [("set_level", "hard"), ("arm_game",),
                           ("arm_game",), ("start_now",),
                           ("set_tap_test", True), ("set_threshold", 300)]


def test_round_command_errors(served) -> None:  # type: ignore
    shell, base = served
    assert call(base, "POST", "/api/game/arm", {"level": "expert"})[0] == 400
    assert call(base, "POST", "/api/game/threshold", {"mg": -1})[0] == 400
    assert call(base, "POST", "/api/game/tap_test", {})[0] == 400
    shell.playing = True
    assert call(base, "POST", "/api/game/now")[0] == 409
