#!/usr/bin/env python3
"""Website over the dashboard (whack-a-mole re-plan, build step 5).

Owner: Jolyn (Buddy 1). Serves one static page (pc/web/index.html, plain JS,
no CDN) and a small JSON API over ``dashboard.DashboardShell``, using only the
Python standard library (http.server). The page polls /api/state every 250 ms;
there are no WebSockets. Binds 0.0.0.0, port 8080 by default.

JSON API (every answer is JSON; errors are {"error": text}):
  GET  /api/state                 grid, node_map, Pico state, mode, score
  POST /api/grid   {rows, cols}   wall shape; rebuilds and re-uploads
  POST /api/place  {slot}         arm tap-to-place: the next TAP fills slot
  POST /api/slot   {slot, on}     off writes 0, on writes the board back;
                                  rebuilds and re-uploads
  POST /api/mode   {mode}         video or game; 409 unless stopped; the
                                  Pico must ACK it, or 500 and no change
  POST /api/game/start            a round: game mode and the wall READY;
                                  409 when refused, 500 without the Pico's ACK
  POST /api/game/abort            end the round now; 409 unless one runs,
                                  500 without the Pico's ACK

Status codes: 400 bad input, 404 unknown path, 405 wrong method, 409 refused
because the wall is playing or not ready, 501 a feature not built, 500
unexpected failure or no ACK from the Pico.

Every request runs under one lock, so the shell is only used by one thread at
a time. Usage:
    python3 pc/web.py --pico 192.168.1.57 [--session 7] [--port 8080]
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Callable, Dict, Optional, Tuple

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import asset_builder as ab  # noqa: E402
import dashboard as db  # noqa: E402

#: Port and bind address (the page is for the laptop and the wall's PC LAN).
DEFAULT_PORT = 8080
BIND_ADDRESS = "0.0.0.0"

#: The one static page.
PAGE_PATH = os.path.join(HERE, "web", "index.html")

#: Largest request body accepted (the API bodies are a few bytes).
MAX_BODY = 4096


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
        "/api/mode": lambda b: shell.set_mode(_field(b, "mode", str)),
        "/api/game/start": lambda _b: shell.start_game(),
        "/api/game/abort": lambda _b: shell.abort_game(),
    }


def dispatch(shell, method: str, path: str,  # type: ignore[no-untyped-def]
             body: Optional[Dict[str, object]]) -> Tuple[int, Dict[str, object]]:
    """Answer one API request: (HTTP status, JSON-able body)."""
    routes = post_routes(shell)
    known = path == "/api/state" or path in routes
    if not known:
        return 404, {"error": "no such endpoint: %s" % path}
    if method == "GET" and path == "/api/state":
        return 200, shell.snapshot()
    if method != "POST" or path not in routes:
        return 405, {"error": "%s is not allowed on %s" % (method, path)}
    try:
        return 200, routes[path](body or {})  # type: ignore[return-value]
    except db.Refused as err:
        return 409, {"error": str(err)}
    except NotImplementedError as err:
        return 501, {"error": str(err)}
    except (ApiError, ab.AssetError) as err:
        return 400, {"error": str(err)}
    except OSError as err:
        return 500, {"error": str(err)}


class _Handler(BaseHTTPRequestHandler):
    """HTTP front end; ``shell`` and ``lock`` are set by make_server."""

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

    def do_GET(self) -> None:  # noqa: N802 (http.server's name)
        path = self.path.split("?", 1)[0]
        if path in ("/", "/index.html"):
            try:
                with open(PAGE_PATH, "rb") as handle:
                    data = handle.read()
            except OSError:
                self._send_json(500, {"error": "web/index.html is missing"})
                return
            self._send(200, "text/html; charset=utf-8", data)
        elif path.startswith("/api/"):
            self._api("GET", None)
        else:
            self._send_json(404, {"error": "no such page: %s" % path})

    def do_POST(self) -> None:  # noqa: N802 (http.server's name)
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            length = -1
        if not 0 <= length <= MAX_BODY:
            self._send_json(400, {"error": "body must be 0 to %d bytes"
                                  % MAX_BODY})
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
    handler = type("Handler", (_Handler,),
                   {"shell": shell, "lock": threading.Lock()})
    server = ThreadingHTTPServer((host, port), handler)
    server.daemon_threads = True
    return server


def main(argv: Optional[list] = None) -> int:  # type: ignore[type-arg]
    """Command line entry point."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--pico", required=True, help="Pico IP address")
    parser.add_argument("--session", type=int, default=None,
                        help="session id 1 to 255; default: a fresh one")
    parser.add_argument("--host", default=BIND_ADDRESS)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    args = parser.parse_args(argv)
    try:
        session = db.resolve_session(args.session)
    except ValueError as err:
        print("web: %s" % err, file=sys.stderr)
        return 1
    shell = db.DashboardShell(args.pico, session)
    server = make_server(shell, args.host, args.port)
    print("website on http://%s:%d/ (session %d)" % (
        args.host, server.server_address[1], session))
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        shell.link.close()
        shell.telemetry.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
