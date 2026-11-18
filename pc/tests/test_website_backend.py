"""pytest: the website's back end (step 4e, 18 Nov 2026): parallel uploads
with progress, background publishing, the network pump, tap peaks, best
score per level, video controls, the GIF library and the preview. No Pico,
tile or network is needed.
"""

from __future__ import annotations

import os
import sys
import threading
import time

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import asset_builder as ab  # noqa: E402
import dashboard as db  # noqa: E402
import gif_library as gl  # noqa: E402
import pico_link as plink  # noqa: E402
import telemetry  # noqa: E402
import uploader  # noqa: E402
import web  # noqa: E402

READY, PLAYBACK, PAUSED, GAME = 7, 8, 9, 12
PLAYING_ROUND, RESULTS = 3, 4


def _status(state: int = READY, readiness: int = 0, online: int = 0,
            **fields: int) -> plink.Status:
    return plink.Status(readiness, online, state, time.monotonic(), **fields)


def _gif(path: str, frames: int = 2, size=(16, 8)) -> str:
    """A small GIF: frame i is a left half red and a right half of grey
    level 40 x i."""
    from PIL import Image
    images = []
    for index in range(frames):
        image = Image.new("RGB", size, (0, 0, 0))
        for x in range(size[0]):
            for y in range(size[1]):
                image.putpixel((x, y), (255, 0, 0) if x < size[0] // 2
                               else (40 * index,) * 3)
        images.append(image)
    images[0].save(path, save_all=True, append_images=images[1:],
                   duration=100, loop=0)
    return path


class FakeLink:
    """PicoLink without a socket."""

    def __init__(self) -> None:
        self.status = None  # type: object
        self.taps = []  # type: list
        self.tap_events = []  # type: list
        self.commands = []  # type: list
        self.configs = []  # type: list
        self.polls = 0

    def poll_status(self):  # type: ignore[no-untyped-def]
        self.polls += 1
        return self.status

    def next_tap(self, _timeout):  # type: ignore[no-untyped-def]
        return self.taps.pop(0) if self.taps else None

    def send_session_config(self, *args):  # type: ignore[no-untyped-def]
        self.configs.append(args)

    def send_game_control(self, command, level=0,  # type: ignore
                          threshold=0):
        self.commands.append(("game", command))
        return True

    def start(self) -> None:
        self.commands.append("start")

    def pause(self) -> None:
        self.commands.append("pause")

    def resume(self) -> None:
        self.commands.append("resume")

    def stop(self) -> None:
        self.commands.append("stop")


class FakeTelemetry:
    def __init__(self, session: int = 7) -> None:
        self.tiles = {}  # type: dict
        self.polls = 0
        self.session = session

    def poll(self) -> int:
        self.polls += 1
        return 0

    def address_of(self, node):  # type: ignore[no-untyped-def]
        return "10.0.0.%d" % node

    def session_of(self, _node):  # type: ignore[no-untyped-def]
        return self.session


class FakeUploader:
    """One per upload; every chunk ACKed at once unless ``fail`` names the
    address."""

    def __init__(self, log: list, fail=(), gate=None) -> None:  # type: ignore
        self.log, self.fail, self.gate = log, fail, gate
        self.closed = False

    def upload_asset(self, address, session_id, payload,  # type: ignore
                     stats=None):
        self.log.append(address)
        stats.chunks = uploader.chunk_count(len(payload))
        if self.gate is not None:
            self.gate.wait(timeout=5)
        if address in self.fail:
            raise uploader.UploadError("tile refused the asset, NACK reason 1")
        stats.acked = set(range(stats.chunks))
        return stats

    def close(self) -> None:
        self.closed = True


def _shell(tmp_path=None, background: bool = False):  # type: ignore
    shell = db.DashboardShell.__new__(db.DashboardShell)
    shell.model = db.DashboardModel(clock=lambda: 0.0)
    shell.link = FakeLink()
    shell.telemetry = FakeTelemetry()
    shell.session_id = 7
    shell.lock = threading.RLock()
    shell.background = background
    shell.live = {}
    shell.uploads_log = []
    shell.new_uploader = lambda: FakeUploader(shell.uploads_log)
    # Tiles follow the Pico's beacon into whatever session the PC uses.
    shell.telemetry.session_of = lambda _node: shell.session_id
    shell.model.on_status(_status())
    return shell


def _three_tile_shell(tmp_path):  # type: ignore[no-untyped-def]
    shell = _shell()
    shell.model.layout = "1x3"
    shell.model.node_map = [1, 2, 3]
    shell.model.gif = _gif(str(tmp_path / "wall.gif"), size=(24, 8))
    return shell


# Parallel uploads (DS-14).

def _uploadable_model(nodes=(1, 2, 3)):  # type: ignore[no-untyped-def]
    model = db.DashboardModel(clock=lambda: 0.0)
    model.layout = "1x%d" % len(nodes)
    model.node_map = list(nodes)
    model.assets = {n: ab.TileAsset(n, 1, bytes([n]) * 96, n)
                    for n in nodes}
    return model


def test_parallel_uploads_send_every_tile_at_the_same_time() -> None:
    model = _uploadable_model()
    barrier = threading.Barrier(3, timeout=5)  # Breaks unless all 3 overlap.
    lock = threading.RLock()

    def send(address, session_id, payload):  # type: ignore[no-untyped-def]
        barrier.wait()

    failed = db.run_uploads(model, 7, send, "10.0.0.{}".format, workers=9,
                            lock=lock)
    assert failed == {}
    assert model.state == "VERIFYING UPLOADS"
    assert [u["state"] for u in model.snapshot()["uploads"]] == ["done"] * 3
    assert model.pending_uploads(7) == []


def test_a_failed_tile_does_not_stop_the_parallel_others() -> None:
    model = _uploadable_model()

    def send(address, session_id, payload):  # type: ignore[no-untyped-def]
        if address.endswith(".2"):
            raise uploader.UploadError("tile is playing (BUSY)")

    failed = db.run_uploads(model, 7, send, "10.0.0.{}".format, workers=9,
                            lock=threading.RLock())
    assert failed == {2: "tile is playing (BUSY)"}
    rows = {u["node"]: u for u in model.snapshot()["uploads"]}
    assert rows[2]["state"] == "failed" and "BUSY" in rows[2]["error"]
    assert rows[1]["state"] == rows[3]["state"] == "done"
    assert model.state == "ERROR" and model.pending_uploads(7) == [2]


def test_each_tile_gets_its_own_uploader_and_progress(tmp_path) -> None:  # type: ignore
    shell = _three_tile_shell(tmp_path)
    shell.publish()
    assert sorted(shell.uploads_log) == ["10.0.0.1", "10.0.0.2", "10.0.0.3"]
    snap = shell.snapshot()
    assert [(u["node"], u["state"], u["acked"] == u["chunks"] > 0)
            for u in snap["uploads"]] == [(1, "done", True), (2, "done", True),
                                          (3, "done", True)]


def test_sync_publish_with_parallel_uploads_does_not_deadlock(tmp_path) -> None:  # type: ignore
    # The shell (not the website) publishes in the caller's thread; its
    # upload threads take the lock, so it must not be held across them.
    shell = _three_tile_shell(tmp_path)
    done = threading.Event()

    def change() -> None:
        shell.set_grid(1, 3)
        done.set()

    threading.Thread(target=change, daemon=True).start()
    assert done.wait(10), "set_grid hung: publish held the lock"
    assert shell.model.pending_uploads(7) == []


def test_every_layout_after_the_first_goes_out_in_a_fresh_session(tmp_path) -> None:  # type: ignore
    # Wall test 18 Nov 2026: the Pico ignores a SESSION_CONFIG repeating its
    # active session, so a GIF chosen after the layout was never seen and
    # every tile's ASSET_READY failed the CRC check.
    shell = _three_tile_shell(tmp_path)
    gif, shell.model.gif = shell.model.gif, None
    shell.set_grid(1, 3)  # Layout only, in the session given.
    shell.model.set_gif(gif)
    shell.publish()  # The GIF: a new session, and every tile uploaded.
    shell.model.set_gif(gif)  # Re-choosing the same GIF publishes again.
    shell.publish()
    sessions = [config[0] for config in shell.link.configs]
    assert sessions[0] == 7
    assert len(set(sessions)) == 3 and all(1 <= s <= 255 for s in sessions)
    assert shell.snapshot()["session"] == sessions[-1]
    assert sorted(shell.uploads_log) == sorted(
        ["10.0.0.1", "10.0.0.2", "10.0.0.3"] * 2)
    assert shell.model.pending_uploads(sessions[-1]) == []


# Background publishing (the website).

def test_website_publish_runs_in_the_background(tmp_path) -> None:  # type: ignore
    shell = _three_tile_shell(tmp_path)
    shell.background = True
    gate = threading.Event()
    shell.new_uploader = lambda: FakeUploader(shell.uploads_log, gate=gate)
    snap = shell.set_grid(1, 3)  # Returns before any upload finishes.
    assert snap["job"] == "upload"
    with pytest.raises(db.Refused, match="wait for the upload"):
        shell.set_slot(0, False)
    gate.set()
    deadline = time.monotonic() + 5
    while shell.snapshot()["job"] is not None:
        assert time.monotonic() < deadline, "the job never finished"
        time.sleep(0.01)
    assert shell.model.pending_uploads(7) == []


def test_a_failing_job_becomes_a_message_and_frees_the_shell() -> None:
    shell = _shell(background=True)
    finished = threading.Event()

    def broken() -> None:
        try:
            raise RuntimeError("disk gone")
        finally:
            finished.set()

    shell.run_job("upload", broken)
    assert finished.wait(5)
    deadline = time.monotonic() + 5
    while shell.model.job is not None:
        assert time.monotonic() < deadline
        time.sleep(0.01)
    assert any("upload failed: disk gone" in m for m in shell.model.messages)


# The pump, tile health and the tap test (A8, A11).

def test_pump_reads_status_telemetry_and_tap_peaks() -> None:
    shell = _shell()
    shell.link.status = _status(GAME, online=0b101)
    shell.link.tap_events.extend([(1, 420, time.monotonic()),
                                  (3, 1300, time.monotonic())])
    shell.pump()
    assert shell.telemetry.polls == 1 and shell.link.tap_events == []
    snap = shell.snapshot()
    assert [(t["node"], t["peak_mg"]) for t in snap["taps"]] == [
        (3, 1300), (1, 420)]  # Newest first.
    assert snap["online"] == 0b101


def test_tiles_show_online_cell_on_off_and_orientation() -> None:
    shell = _shell()
    shell.model.layout, shell.model.node_map = "1x3", [1, 0, 3]
    shell.model.parked = {1: 2}
    shell.telemetry.tiles[3] = telemetry.TileRow(
        node_id=3, address="10.0.0.3", last_seen=time.monotonic(), seq=1,
        fault_word=0, orientation=2, display_state=1)
    shell.link.status = _status(online=0b100)
    tiles = {t["node"]: t for t in shell.snapshot()["tiles"]}
    assert sorted(tiles) == [1, 2, 3]
    assert (tiles[1]["online"], tiles[1]["cell"], tiles[1]["on"]) == \
        (False, 0, True)
    assert (tiles[2]["cell"], tiles[2]["on"]) == (1, False)  # Switched off.
    assert (tiles[3]["online"], tiles[3]["orientation"],
            tiles[3]["faults"]) == (True, 2, "none")


def test_start_pump_keeps_pumping_after_an_error() -> None:
    calls = []

    class Flaky:
        def pump(self) -> None:
            calls.append(1)
            if len(calls) == 1:
                raise OSError("network down")

    stop = threading.Event()
    thread = web.start_pump(Flaky(), stop, period_s=0.001)
    deadline = time.monotonic() + 5
    while len(calls) < 5:
        assert time.monotonic() < deadline
        time.sleep(0.005)
    stop.set()
    thread.join(5)
    assert not thread.is_alive()


def test_placing_a_switched_off_tile_elsewhere_clears_its_old_cell() -> None:
    # Seen on the page 18 Nov 2026: tile 5 switched off in cell 4, then
    # tap-placed in cell 3, still showed as switched off in cell 4.
    model = db.DashboardModel(clock=lambda: 0.0)
    model.layout, model.node_map = "2x3", [1, 2, 3, 0, 5, 0]
    model.set_slot(4, False)
    assert model.parked == {4: 5}
    model.place(3, lambda: 5)
    assert model.node_map == [1, 2, 3, 5, 0, 0]
    assert model.parked == {}
    with pytest.raises(ab.AssetError, match="no tile to put back"):
        model.set_slot(4, True)


# Best score per level (A10).

def _round(model, level: int, score: int, shown: int = 10) -> dict:  # type: ignore
    model.on_status(_status(GAME, round_state=PLAYING_ROUND, level=level))
    model.on_status(_status(READY, round_state=RESULTS, level=level,
                            score=score, shown=shown))
    return model.last_result


def test_best_score_per_level_and_new_best() -> None:
    model = db.DashboardModel(clock=lambda: 0.0)
    assert _round(model, 0, 5)["new_best"] is True
    assert _round(model, 0, 3)["new_best"] is False
    assert _round(model, 0, 5)["new_best"] is False  # Equal is not higher.
    assert _round(model, 0, 7)["new_best"] is True
    assert _round(model, 2, 1)["new_best"] is True  # Hard has its own best.
    assert model.snapshot()["best"] == {"easy": 7, "hard": 1}
    result = _round(model, 1, 0, shown=4)
    assert result["new_best"] is False and result["accuracy_pct"] == 0
    assert model.best["normal"] == 0


# Video controls (A13).

def test_play_needs_video_mode_a_gif_and_every_tile_ready(tmp_path) -> None:  # type: ignore
    shell = _three_tile_shell(tmp_path)
    shell.publish()
    assert shell.model.state == "VERIFYING UPLOADS"
    with pytest.raises(db.Refused, match="not every tile is ready"):
        shell.play()
    shell.link.status = _status(readiness=0b111)
    snap = shell.play()
    assert shell.link.commands == ["start"]
    assert snap["play_enabled"] is True
    shell.model.mode = "game"
    with pytest.raises(db.Refused, match="video mode"):
        shell.play()


def test_pause_resume_and_stop_follow_the_pico_state() -> None:
    shell = _shell()
    with pytest.raises(db.Refused, match="cannot pause"):
        shell.video("pause")
    shell.link.status = _status(PLAYBACK)
    shell.video("pause")
    with pytest.raises(db.Refused, match="cannot resume"):
        shell.video("resume")
    shell.link.status = _status(PAUSED)
    snap = shell.video("resume")
    assert snap["stop_enabled"] is True and snap["pause_enabled"] is False
    shell.video("stop")
    assert shell.link.commands == ["pause", "resume", "stop"]


# GIF library.

def test_library_saves_lists_and_deletes(tmp_path) -> None:  # type: ignore
    folder = str(tmp_path / "gifs")
    data = open(_gif(str(tmp_path / "src.gif"), frames=3), "rb").read()
    info = gl.save_gif("Blip robot_1.gif", data, folder)
    assert (info["frames"], info["width"], info["height"]) == (3, 16, 8)
    assert [g["name"] for g in gl.list_gifs(folder)] == ["Blip robot_1.gif"]
    assert gl.path_of("Blip robot_1.gif", folder).startswith(folder)
    gl.delete_gif("Blip robot_1.gif", folder)
    assert gl.list_gifs(folder) == []


@pytest.mark.parametrize("name", ["../up.gif", "a/b.gif", "x.png", ".gif",
                                  "", "a" * 70 + ".gif", "nul\x00.gif"])
def test_library_refuses_unsafe_names(tmp_path, name) -> None:  # type: ignore
    with pytest.raises(gl.LibraryError):
        gl.save_gif(name, b"GIF89a....", str(tmp_path))


def test_library_refuses_files_that_are_not_gifs(tmp_path) -> None:  # type: ignore
    folder = str(tmp_path)
    for data in (b"", b"\x89PNG....", b"GIF89a" + b"\x00" * 10):
        with pytest.raises(gl.LibraryError):
            gl.save_gif("x.gif", data, folder)
    assert os.listdir(folder) == []  # No half-written file is left.
    with pytest.raises(gl.LibraryError, match="at most"):
        gl.save_gif("x.gif", b"GIF89a" + b"\x00" * gl.MAX_BYTES, folder)


def test_chosen_gif_cannot_be_deleted(tmp_path, monkeypatch) -> None:  # type: ignore
    monkeypatch.setattr(gl, "GIF_DIR", str(tmp_path))
    data = open(_gif(str(tmp_path / "src.gif")), "rb").read()
    gl.save_gif("demo.gif", data, str(tmp_path))
    shell = _shell()
    shell.model.layout, shell.model.node_map = "1x2", [1, 2]
    snap = shell.choose_gif("demo.gif")
    assert snap["gif"] == "demo.gif"
    with pytest.raises(db.Refused, match="choose another"):
        shell.delete_gif("demo.gif")


# Preview (DS-16): the same cut as the tiles' sections.

def test_preview_matches_the_tiles_cut_and_greys_empty_and_off(tmp_path) -> None:  # type: ignore
    import io
    from PIL import Image
    frame = ab.load_first_frame(_gif(str(tmp_path / "p.gif"), size=(24, 8)))
    node_map = [1, 0, 3]
    png = ab.render_preview(frame, "1x3", node_map, off_slots=[1])
    image = Image.open(io.BytesIO(png)).convert("RGB")
    led = ab.preview_led_px(1, 3)
    side, pitch = 8 * led, 8 * led + ab.PREVIEW_GAP_PX
    assert image.size == (3 * pitch - ab.PREVIEW_GAP_PX, side)
    tiles = ab.partition_tiles([frame], "1x3", node_map)
    for slot, node in ((0, 1), (2, 3)):
        for led_y in range(8):
            for led_x in range(8):
                got = image.getpixel((slot * pitch + led_x * led + led // 2,
                                      led_y * led + led // 2))
                assert got == tiles[node][0][led_y * 8 + led_x]
    assert image.getpixel((pitch + side // 2, side // 2)) == ab.PREVIEW_OFF
    png = ab.render_preview(frame, "1x3", node_map)
    image = Image.open(io.BytesIO(png)).convert("RGB")
    assert image.getpixel((pitch + side // 2, side // 2)) == ab.PREVIEW_EMPTY


def test_preview_needs_a_gif() -> None:
    with pytest.raises(ab.AssetError, match="choose a GIF"):
        _shell().preview_png()


# The new routes.

class RouteShell:
    """Records the calls of the step 4e routes."""

    def __init__(self) -> None:
        self.calls = []  # type: list

    def snapshot(self):  # type: ignore[no-untyped-def]
        return {"ok": True}

    def list_gifs(self):  # type: ignore[no-untyped-def]
        return {"gifs": [], "chosen": None}

    def add_gif(self, name, data):  # type: ignore[no-untyped-def]
        self.calls.append(("add_gif", name, data))
        if name.endswith(".png"):
            raise gl.LibraryError("bad name")
        return self.list_gifs()

    def delete_gif(self, name):  # type: ignore[no-untyped-def]
        self.calls.append(("delete_gif", name))
        return self.list_gifs()

    def choose_gif(self, name):  # type: ignore[no-untyped-def]
        self.calls.append(("choose_gif", name))
        return {}

    def preview_png(self):  # type: ignore[no-untyped-def]
        return b"\x89PNG fake"

    def play(self):  # type: ignore[no-untyped-def]
        self.calls.append(("play",))
        raise db.Refused("not every tile is ready")

    def video(self, action):  # type: ignore[no-untyped-def]
        self.calls.append(("video", action))
        return {}


@pytest.fixture
def routes():  # type: ignore[no-untyped-def]
    shell = RouteShell()
    server = web.make_server(shell, "127.0.0.1", 0)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    yield shell, "http://127.0.0.1:%d" % server.server_address[1]
    server.shutdown()
    server.server_close()


def _request(base, method, path, data=None,  # type: ignore[no-untyped-def]
             kind="application/json"):
    import urllib.error
    import urllib.request
    request = urllib.request.Request(base + path, data=data, method=method,
                                     headers={"Content-Type": kind})
    try:
        with urllib.request.urlopen(request, timeout=5) as res:
            return res.status, res.headers.get("Content-Type"), res.read()
    except urllib.error.HTTPError as err:
        return err.code, err.headers.get("Content-Type"), err.read()


def test_gif_upload_takes_the_raw_body_and_a_name(routes) -> None:  # type: ignore
    shell, base = routes
    status, _kind, _body = _request(base, "POST", "/api/gifs?name=a%20b.gif",
                                    b"GIF89a-bytes", "image/gif")
    assert status == 200
    assert shell.calls == [("add_gif", "a b.gif", b"GIF89a-bytes")]
    assert _request(base, "POST", "/api/gifs", b"x", "image/gif")[0] == 400
    assert _request(base, "POST", "/api/gifs?name=x.png", b"x",
                    "image/gif")[0] == 400


def test_preview_is_a_png_and_gif_choice_takes_null(routes) -> None:  # type: ignore
    shell, base = routes
    status, kind, body = _request(base, "GET", "/api/preview.png")
    assert (status, kind, body) == (200, "image/png", b"\x89PNG fake")
    assert _request(base, "POST", "/api/gif", b'{"name": null}')[0] == 200
    assert _request(base, "POST", "/api/gif", b'{"name": "d.gif"}')[0] == 200
    assert _request(base, "POST", "/api/gif", b'{"name": 3}')[0] == 400
    assert _request(base, "POST", "/api/gifs/delete",
                    b'{"name": "d.gif"}')[0] == 200
    assert shell.calls == [("choose_gif", None), ("choose_gif", "d.gif"),
                           ("delete_gif", "d.gif")]


def test_video_routes_and_refusal(routes) -> None:  # type: ignore
    shell, base = routes
    assert _request(base, "POST", "/api/video/play")[0] == 409
    for action in ("pause", "resume", "stop"):
        assert _request(base, "POST", "/api/video/" + action)[0] == 200
    assert shell.calls == [("play",), ("video", "pause"),
                           ("video", "resume"), ("video", "stop")]
    assert _request(base, "GET", "/api/gifs")[0] == 200


def test_scoreboard_page_is_served_with_no_cdn(routes) -> None:  # type: ignore
    _shell_, base = routes
    status, kind, body = _request(base, "GET", "/score")
    page = body.decode("utf-8")
    assert status == 200 and kind.startswith("text/html")
    assert "/api/state" in page
    assert "http://" not in page and "https://" not in page
