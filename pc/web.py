#!/usr/bin/env python3
"""Website over the dashboard (whack-a-mole re-plan, build step 5).

Owner: Jolyn (Buddy 1). Serves two static pages (pc/web/index.html, the
control page, and pc/web/score.html, the scoreboard for a TV, A16; plain JS,
no CDN) and a small JSON API over ``dashboard.DashboardShell``, using only the
Python standard library (http.server). The pages poll /api/state every
250 ms; there are no WebSockets. Binds 0.0.0.0, port 8080 by default.

Step 4e (18 Nov 2026): a pump thread calls ``shell.pump()`` every 20 ms, so
STATUS, TAPs and TELEMETRY are read and ACKed with no browser open, and the
shell publishes in the background, so an upload never holds up a request.

JSON API (every answer is JSON, except the preview; errors are
{"error": text}):
  GET  /api/state                 grid, node_map, Pico state, mode, score,
                                  tiles, uploads, taps, best per level
  GET  /api/gifs                  the GIF library and the GIF chosen
  GET  /api/preview.png           the chosen GIF's first frame cut for the
                                  grid (PNG); 400 with no GIF chosen
  POST /api/gifs?name=NAME        the body is a GIF file (image/gif, up to
                                  8 MB): kept in pc/gifs as NAME
  POST /api/gifs/delete {name}    remove a GIF; 409 for the one chosen
  POST /api/gif    {name}         choose the GIF (null: no video); rebuilds
                                  and uploads in the background
  POST /api/video/play            play; 409 unless every tile is ready
  POST /api/video/pause, resume, stop
  POST /api/grid   {rows, cols}   wall shape; rebuilds and re-uploads
  POST /api/place  {slot}         arm tap-to-place: the next TAP fills slot
  POST /api/slot   {slot, on}     off writes 0, on writes the board back;
                                  rebuilds and re-uploads
  POST /api/remove {slot}         take the tile out of the grid: the cell
                                  is freed (not off); rebuilds and uploads
  POST /api/mode   {mode}         video or game; 409 unless stopped; the
                                  Pico must ACK it, or 500 and no change
  POST /api/game/start            a round: game mode and the wall READY;
                                  409 when refused, 500 without the Pico's ACK
  POST /api/game/abort            end the round now; 409 unless one runs,
                                  500 without the Pico's ACK
  POST /api/game/arm  {level?}    arm a round (start sprite, DS-08) at the
                                  level (easy, normal, hard); as start
  POST /api/game/now              skip the start sprite; 409 unless armed
  POST /api/game/tap_test {on}    the tap test (A11); 409 unless READY in
                                  game mode
  POST /api/game/threshold {mg}   tap threshold for the next arm (A12); 0
                                  is the tiles' default; 400 when invalid

Status codes: 400 bad input, 404 unknown path, 405 wrong method, 409 refused
because the wall is playing or not ready, 501 a feature not built, 500
unexpected failure or no ACK from the Pico.

Every request runs under the shell's lock (``shell.lock``), so the shell is
only used by one thread at a time; a GIF upload and the preview take it only
briefly. Usage:
    python3 pc/web.py --pico 192.168.1.57 [--session 7] [--port 8080]
        [--layout 3x3 [--map 1,2,3,4,5,6,7,8,9]] [--gif NAME.gif]
        [--policy pause|continue]
--layout, --map and --gif set up the wall at start (demo day: no
tap-placing after a restart); the map defaults to reading order. --policy
is the failure policy every SESSION_CONFIG carries (default pause; bench
25 Nov 2026: under pause a tile away over 5 s puts the Pico in FAULT).
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Callable, Dict, Optional, Tuple
from urllib.parse import parse_qs, urlsplit

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import asset_builder as ab  # noqa: E402
import dashboard as db  # noqa: E402
import gif_library as gl  # noqa: E402
import proto_layout as pl  # noqa: E402
import pico_link  # noqa: E402
import tap_log as tl  # noqa: E402

#: Port and bind address (the page is for the laptop and the wall's PC LAN).
DEFAULT_PORT = 8080
BIND_ADDRESS = "0.0.0.0"

#: The static pages: the control page and the scoreboard (A16).
PAGE_PATH = os.path.join(HERE, "web", "index.html")
SCORE_PATH = os.path.join(HERE, "web", "score.html")
PAGES = {"/": PAGE_PATH, "/index.html": PAGE_PATH, "/score": SCORE_PATH,
         "/score.html": SCORE_PATH}

#: Largest request body accepted (the API bodies are a few bytes).
MAX_BODY = 4096

#: Largest GIF accepted by POST /api/gifs.
MAX_GIF_BODY = gl.MAX_BYTES

#: How often the pump reads the network, seconds (50 times a second).
PUMP_PERIOD_S = 0.02


class ApiError(ValueError):
    """A request body that is missing a field or has the wrong type."""


def _field(body: Dict[str, object], name: str, kind: type) -> object:
    value = body.get(name)
    if kind is int and (isinstance(value, bool) or not isinstance(value, int)):
        raise ApiError("%s must be an integer" % name)
    if kind is bool and not isinstance(value, bool):
        raise ApiError("%s must be true or false" % name)
    if kind is str and not isinstance(value, str):
        raise ApiError("%s must be a string" % name)
    return value


Route = Callable[[Dict[str, object]], object]


def post_routes(shell) -> Dict[str, Route]:  # type: ignore[no-untyped-def]
    """POST paths and what each calls on the shell."""
    return {
        "/api/grid": lambda b: shell.set_grid(
            _field(b, "rows", int), _field(b, "cols", int)),
        "/api/place": lambda b: shell.arm_place(_field(b, "slot", int)),
        "/api/slot": lambda b: shell.set_slot(
            _field(b, "slot", int), _field(b, "on", bool)),
        "/api/remove": lambda b: shell.remove_slot(_field(b, "slot", int)),
        "/api/mode": lambda b: shell.set_mode(_field(b, "mode", str)),
        "/api/game/start": lambda _b: shell.start_game(),
        "/api/game/abort": lambda _b: shell.abort_game(),
        "/api/game/arm": lambda b: _arm(shell, b),
        "/api/game/now": lambda _b: shell.start_now(),
        "/api/game/tap_test": lambda b: shell.set_tap_test(
            _field(b, "on", bool)),
        "/api/game/threshold": lambda b: shell.set_threshold(
            _field(b, "mg", int)),
        "/api/gif": lambda b: shell.choose_gif(_gif_name(b)),
        "/api/gifs/delete": lambda b: shell.delete_gif(
            _field(b, "name", str)),
        "/api/video/play": lambda _b: shell.play(),
        "/api/video/pause": lambda _b: shell.video("pause"),
        "/api/video/resume": lambda _b: shell.video("resume"),
        "/api/video/stop": lambda _b: shell.video("stop"),
    }


def get_routes(shell) -> Dict[str, Callable[[], object]]:  # type: ignore
    """GET paths that answer JSON."""
    return {"/api/state": lambda: shell.snapshot(),
            "/api/gifs": lambda: shell.list_gifs()}


def _gif_name(body: Dict[str, object]) -> Optional[str]:
    """The GIF to choose: a name, or null for no video."""
    if body.get("name") is None and "name" in body:
        return None
    return _field(body, "name", str)  # type: ignore[return-value]


def _arm(shell, body: Dict[str, object]):  # type: ignore[no-untyped-def]
    """Arm a round, at the body's level when it gives one."""
    if "level" in body:
        shell.set_level(_field(body, "level", str))
    return shell.arm_game()


def dispatch(shell, method: str, path: str,  # type: ignore[no-untyped-def]
             body: Optional[Dict[str, object]]) -> Tuple[int, Dict[str, object]]:
    """Answer one API request: (HTTP status, JSON-able body)."""
    routes = post_routes(shell)
    gets = get_routes(shell)
    if path not in routes and path not in gets:
        return 404, {"error": "no such endpoint: %s" % path}
    if method == "GET" and path in gets:
        return guarded(gets[path])
    if method != "POST" or path not in routes:
        return 405, {"error": "%s is not allowed on %s" % (method, path)}
    return guarded(lambda: routes[path](body or {}))


def guarded(call: Callable[[], object]) -> Tuple[int, Dict[str, object]]:
    """Run one API call: (200, its answer) or the error's status."""
    try:
        return 200, call()  # type: ignore[return-value]
    except db.Refused as err:
        return 409, {"error": str(err)}
    except NotImplementedError as err:
        return 501, {"error": str(err)}
    except (ApiError, ab.AssetError) as err:
        return 400, {"error": str(err)}
    except OSError as err:
        return 500, {"error": str(err)}


class _Handler(BaseHTTPRequestHandler):
    """HTTP front end; ``shell`` and ``lock`` are set by make_server. The
    preview and a GIF upload take the lock only inside the shell, so a slow
    picture or file never holds up the state polls."""

    server_version = "SE15Web/1"
    shell = None
    lock = None

    def log_message(self, format: str, *args: object) -> None:
        """Keep the console quiet; the dashboard prints what matters."""

    def _send(self, status: int, content_type: str, data: bytes) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def _send_json(self, status: int, payload: Dict[str, object]) -> None:
        self._send(status, "application/json",
                   json.dumps(payload).encode("utf-8"))

    def _api(self, method: str, body: Optional[Dict[str, object]]) -> None:
        path = self.path.split("?", 1)[0]
        try:
            with self.lock:  # type: ignore[union-attr]
                status, payload = dispatch(self.shell, method, path, body)
        except Exception as err:  # Never leave the page without an answer.
            status, payload = 500, {"error": "%s: %s" % (
                type(err).__name__, err)}
        self._send_json(status, payload)

    def _page(self, path: str) -> None:
        try:
            with open(PAGES[path], "rb") as handle:
                data = handle.read()
        except OSError:
            self._send_json(500, {"error": "web/%s is missing" % (
                os.path.basename(PAGES[path]))})
            return
        self._send(200, "text/html; charset=utf-8", data)

    def _preview(self) -> None:
        status, payload = guarded(lambda: {"png": self.shell.preview_png()})
        if status != 200:
            self._send_json(status, payload)
            return
        self._send(200, "image/png", payload["png"])  # type: ignore[arg-type]

    def do_GET(self) -> None:  # noqa: N802 (http.server's name)
        path = self.path.split("?", 1)[0]
        if path in PAGES:
            self._page(path)
        elif path == "/api/preview.png":
            self._preview()
        elif path.startswith("/api/"):
            self._api("GET", None)
        else:
            self._send_json(404, {"error": "no such page: %s" % path})

    def _length(self, limit: int) -> Optional[int]:
        """The body's length, or None (answered 400) when over ``limit``."""
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            length = -1
        if not 0 <= length <= limit:
            self._send_json(400, {"error": "body must be 0 to %d bytes"
                                  % limit})
            return None
        return length

    def _add_gif(self) -> None:
        """POST /api/gifs?name=NAME with the GIF as the body."""
        names = parse_qs(urlsplit(self.path).query).get("name", [])
        length = self._length(MAX_GIF_BODY)
        if length is None:
            return
        data = self.rfile.read(length) if length else b""
        if len(names) != 1:
            self._send_json(400, {"error": "give the GIF one ?name="})
            return
        self._send_json(*guarded(lambda: self.shell.add_gif(names[0], data)))

    def do_POST(self) -> None:  # noqa: N802 (http.server's name)
        if self.path.split("?", 1)[0] == "/api/gifs":
            self._add_gif()
            return
        length = self._length(MAX_BODY)
        if length is None:
            return
        raw = self.rfile.read(length) if length else b""
        try:
            body = json.loads(raw.decode("utf-8") or "{}")
        except (UnicodeDecodeError, ValueError):
            self._send_json(400, {"error": "body is not JSON"})
            return
        if not isinstance(body, dict):
            self._send_json(400, {"error": "body must be a JSON object"})
            return
        self._api("POST", body)


def make_server(shell, host: str = BIND_ADDRESS,  # type: ignore[no-untyped-def]
                port: int = DEFAULT_PORT) -> ThreadingHTTPServer:
    """A server bound to host:port over ``shell`` (port 0 picks a free one).
    The caller runs serve_forever() and server_close()."""
    lock = getattr(shell, "lock", None) or threading.RLock()
    handler = type("Handler", (_Handler,), {"shell": shell, "lock": lock})
    server = ThreadingHTTPServer((host, port), handler)
    server.daemon_threads = True
    return server


def start_pump(shell, stop: threading.Event,  # type: ignore[no-untyped-def]
               period_s: float = PUMP_PERIOD_S) -> threading.Thread:
    """A thread that calls ``shell.pump()`` every ``period_s`` until
    ``stop`` is set. An error is printed once per kind and the pump goes
    on: the page must keep its STATUS."""
    seen = set()

    def run() -> None:
        while not stop.wait(period_s):
            try:
                shell.pump()
            except Exception as err:  # Keep pumping; say each kind once.
                kind = type(err).__name__
                if kind not in seen:
                    seen.add(kind)
                    print("web: pump: %s: %s" % (kind, err), file=sys.stderr)

    thread = threading.Thread(target=run, name="se15-pump", daemon=True)
    thread.start()
    return thread


def start_wall(shell, layout: Optional[str],  # type: ignore[no-untyped-def]
               map_text: Optional[str], gif: Optional[str]) -> None:
    """The wall at start: the layout (the map in reading order when not
    given), a library GIF, then one publish in the background. Raises
    ab.AssetError on a bad layout, map or GIF name."""
    if layout is None and map_text is None and gif is None:
        return
    model = shell.model
    if layout is not None:
        rows, cols = ab.parse_shape(layout)
        if map_text is None:
            node_map = [n if n <= pl.NODE_MAX else 0
                        for n in range(1, rows * cols + 1)]
        else:
            try:
                node_map = [int(n) for n in map_text.split(",")]
            except ValueError:
                raise ab.AssetError("the map is Node IDs separated by commas")
        ab.parse_layout(layout, node_map)
        model.layout = layout
        model.node_map = ab.normalise_map(layout, node_map)
    elif map_text is not None:
        raise ab.AssetError("--map needs --layout")
    if gif is not None:
        model.gif = gl.path_of(gif)
    shell.run_job("upload", shell.publish)


#: --policy names to the SESSION_CONFIG failure policy (T-B2-04).
POLICIES = {"pause": pico_link.POLICY_PAUSE,
            "continue": pico_link.POLICY_CONTINUE}


def main(argv: Optional[list] = None) -> int:  # type: ignore[type-arg]
    """Command line entry point."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--pico", required=True, help="Pico IP address")
    parser.add_argument("--session", type=int, default=None,
                        help="session id 1 to 255; default: a fresh one")
    parser.add_argument("--host", default=BIND_ADDRESS)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--layout", default=None,
                        help="grid at start, e.g. 3x3")
    parser.add_argument("--map", default=None,
                        help="Node IDs in reading order, 0 = empty cell")
    parser.add_argument("--gif", default=None,
                        help="a GIF of the library (pc/gifs) to play")
    parser.add_argument("--tap-log", default="auto",
                        help="CSV of every tap: auto (pc/logs/taps_<time>"
                             ".csv), none, or a path")
    parser.add_argument("--policy", choices=sorted(POLICIES),
                        default="pause",
                        help="failure policy when a tile is lost during "
                             "playback (default pause)")
    args = parser.parse_args(argv)
    try:
        session = db.resolve_session(args.session)
    except ValueError as err:
        print("web: %s" % err, file=sys.stderr)
        return 1
    shell = db.DashboardShell(args.pico, session)
    shell.background = True
    shell.model.policy = POLICIES[args.policy]
    print("failure policy: %s" % args.policy)
    if args.tap_log != "none":
        shell.tap_log = tl.TapLog(tl.default_path() if args.tap_log == "auto"
                                  else args.tap_log)
        print("taps logged to %s" % shell.tap_log.path)
    try:
        start_wall(shell, args.layout, args.map, args.gif)
    except ab.AssetError as err:
        print("web: %s" % err, file=sys.stderr)
        shell.link.close()
        shell.telemetry.close()
        return 1
    server = make_server(shell, args.host, args.port)
    stop = threading.Event()
    start_pump(shell, stop)
    print("website on http://%s:%d/ (session %d), scoreboard on /score" % (
        args.host, server.server_address[1], session))
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
        server.server_close()
        if shell.tap_log is not None:
            shell.tap_log.close()
        shell.link.close()
        shell.telemetry.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
