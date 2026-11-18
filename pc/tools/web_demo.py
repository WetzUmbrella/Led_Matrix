#!/usr/bin/env python3
"""The website against a simulated wall (step 4e, 18 Nov 2026): no Pico, no
tile, no network. For working on the pages and practising the demo.

The real ``DashboardShell`` and ``web.py`` run; only the Pico link, the
telemetry receiver and the uploader are replaced by ``SimWall``:
  * nine tiles online, heard on telemetry, turned upright;
  * SESSION_CONFIG with video: WAITING FOR TILE READINESS until every
    placed tile is uploaded (about 1.7 s per tile, all at once), then READY;
  * Play, Pause, Resume, Stop move the Pico state as the real one does;
  * tap-to-place: a tap arrives 1.5 s after the cell is armed;
  * the tap test: a tap on a random tile every 0.7 s;
  * a round: ARMED until Start now (or a simulated child's tap after 4 s),
    a 3 s COUNTDOWN, 30 s PLAYING with moles hit about 80% of the time,
    then RESULTS.
GIFs go to the real library (pc/gifs). Usage:
    python3 pc/tools/web_demo.py [--port 8080]
"""

from __future__ import annotations

import argparse
import os
import random
import sys
import threading
import time
from typing import List, Optional, Tuple

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
import dashboard as db  # noqa: E402
import pico_link as plink  # noqa: E402
import telemetry  # noqa: E402
import uploader  # noqa: E402
import web  # noqa: E402

#: Coordinator states (pico_link.COORD_STATES).
WAITING_READY = plink.COORD_STATES.index("WAITING FOR TILE READINESS")
READY = plink.COORD_STATES.index("READY")
PLAYBACK = plink.COORD_STATES.index("COORDINATING PLAYBACK")
PAUSED = plink.COORD_STATES.index("PAUSED")
GAME = plink.COORD_STATES.index("COORDINATING GAME")

#: Round states (pico_link.ROUND_STATES).
IDLE, ARMED, COUNTDOWN, PLAYING, RESULTS = range(5)

#: Simulated timings, seconds.
UPLOAD_CHUNK_S = 0.03
PLACE_TAP_S = 1.5
TAP_TEST_PERIOD_S = 0.7
CHILD_TAP_S = 4.0
COUNTDOWN_S = 3.0
ROUND_S = 30
MOLE_PERIOD_S = 1.2


def address(node: int) -> str:
    return "10.99.0.%d" % node


class SimWall:
    """The Pico link, the telemetry receiver and the uploader in one."""

    def __init__(self, shell: "db.DashboardShell") -> None:
        self.shell = shell
        self.lock = threading.Lock()
        self.rng = random.Random(15)
        self.state = READY
        self.readiness = 0
        self.required = 0
        self.crcs: dict = {}
        self.online = (1 << 9) - 1
        self.round = IDLE
        self.level = 0
        self.round_since = 0.0
        self.score = self.missed = self.wrong = self.shown = 0
        self.reaction_ms = 0
        self.tap_test = False
        self.next_test_tap = 0.0
        self.place_since: Optional[float] = None
        self.taps: List[int] = []
        self.tap_events: List[Tuple[int, int, float]] = []
        self.status: Optional[plink.Status] = None
        now = time.monotonic()
        self.tiles = {n: telemetry.TileRow(
            node_id=n, address=address(n), last_seen=now, seq=1,
            fault_word=0, orientation=0, display_state=1)
            for n in range(1, 10)}

    # Telemetry receiver.

    def poll(self) -> int:
        now = time.monotonic()
        for row in self.tiles.values():
            row.last_seen = now
            row.session_id = self.shell.session_id
        return len(self.tiles)

    def address_of(self, node: int) -> Optional[str]:
        return address(node) if node in self.tiles else None

    def session_of(self, node: int) -> Optional[int]:
        return self.shell.session_id if node in self.tiles else None

    def close(self) -> None:
        pass

    # Pico link.

    def _advance(self, now: float) -> None:
        if self.round == ARMED and now - self.round_since > CHILD_TAP_S:
            self.round, self.round_since = COUNTDOWN, now
        if self.round == COUNTDOWN and now - self.round_since > COUNTDOWN_S:
            self.round, self.round_since = PLAYING, now
            self.score = self.missed = self.wrong = self.shown = 0
        if self.round == PLAYING:
            elapsed = now - self.round_since
            moles = int(elapsed // MOLE_PERIOD_S)
            while self.shown < moles:
                self.shown += 1
                if self.rng.random() < 0.8:
                    self.score += 1
                    self.reaction_ms = self.rng.randint(450, 900)
                else:
                    self.missed += 1
                if self.rng.random() < 0.15:
                    self.wrong += 1
            if elapsed >= ROUND_S:
                self.round, self.state = RESULTS, READY
        if self.tap_test and now >= self.next_test_tap:
            self.next_test_tap = now + TAP_TEST_PERIOD_S
            node = self.rng.randint(1, 9)
            self.tap_events.append((node, self.rng.randint(180, 1900), now))

    def poll_status(self) -> Optional[plink.Status]:
        with self.lock:
            now = time.monotonic()
            self._advance(now)
            left = 0
            if self.round == PLAYING:
                left = max(0, ROUND_S - int(now - self.round_since))
            self.status = plink.Status(
                self.readiness, self.online, self.state, now, self.score,
                round_state=self.round, level=self.level,
                missed=self.missed, wrong=self.wrong, shown=self.shown,
                time_left_s=left, reaction_ms=self.reaction_ms)
            return self.status

    def next_tap(self, _timeout_s: float) -> Optional[int]:
        now = time.monotonic()
        if self.place_since is None:
            self.place_since = now
            return None
        if now - self.place_since < PLACE_TAP_S:
            return None
        self.place_since = None
        placed = set(self.shell.model.node_map)
        free = [n for n in range(1, 10) if n not in placed]
        return free[0] if free else 1

    def send_session_config(self, session_id: int, rows: int, cols: int,
                            policy: int, node_map, frame_count: int,
                            crcs) -> None:  # type: ignore[no-untyped-def]
        with self.lock:
            self.required = plink.required_mask(node_map)
            video = any(crcs)
            # A tile whose section did not change keeps it, and stays ready.
            same = 0
            for node, crc in enumerate(crcs, start=1):
                if crc and crc == self.crcs.get(node):
                    same |= 1 << (node - 1)
            self.crcs = dict(enumerate(crcs, start=1))
            self.readiness = (self.readiness & same) if video \
                else self.required
            ready = (self.readiness & self.required) == self.required
            self.state = READY if ready else WAITING_READY

    def send_game_control(self, command: int, level: int = 0,
                          threshold: int = 0) -> bool:
        with self.lock:
            now = time.monotonic()
            if command == plink.GAME_ARM:
                self.state, self.round, self.level = GAME, ARMED, level
                self.round_since = now
                self.tap_test = False
            elif command == plink.GAME_START_NOW and self.round == ARMED:
                self.round, self.round_since = COUNTDOWN, now
            elif command == plink.GAME_END:
                if self.round == PLAYING:
                    self.round = RESULTS
                else:
                    self.round = IDLE
                self.state = READY
            elif command == plink.GAME_TAP_TEST_ON:
                self.tap_test, self.next_test_tap = True, now
            elif command == plink.GAME_TAP_TEST_OFF:
                self.tap_test = False
            elif command in (plink.MODE_VIDEO, plink.MODE_GAME):
                self.round = IDLE
        return True

    def start(self) -> None:
        with self.lock:
            self.state = PLAYBACK

    def pause(self) -> None:
        with self.lock:
            self.state = PAUSED

    def resume(self) -> None:
        with self.lock:
            self.state = PLAYBACK

    def stop(self) -> None:
        with self.lock:
            self.state = READY

    # Uploader (one per upload, as the shell asks).

    def new_uploader(self) -> "SimUploader":
        return SimUploader(self)


class SimUploader:
    def __init__(self, wall: SimWall) -> None:
        self.wall = wall

    def upload_asset(self, tile_ip: str, session_id: int, payload: bytes,
                     stats: Optional[uploader.UploadStats] = None
                     ) -> uploader.UploadStats:
        stats = stats or uploader.UploadStats()
        stats.chunks = uploader.chunk_count(len(payload))
        started = time.monotonic()
        for index in range(stats.chunks):
            time.sleep(UPLOAD_CHUNK_S)
            stats.acked.add(index)
        stats.seconds = time.monotonic() - started
        node = int(tile_ip.rsplit(".", 1)[1])
        with self.wall.lock:
            self.wall.readiness |= 1 << (node - 1)
            if (self.wall.readiness & self.wall.required) == \
                    self.wall.required:
                self.wall.state = READY
        return stats

    def close(self) -> None:
        pass


def make_shell() -> "db.DashboardShell":
    """A DashboardShell whose Pico, tiles and uploader are a SimWall, with
    a 3x3 wall placed in reading order."""
    shell = db.DashboardShell.__new__(db.DashboardShell)
    db.cmd.Cmd.__init__(shell)
    shell.model = db.DashboardModel()
    shell.session_id = 1
    shell.lock = threading.RLock()
    shell.background = True
    shell.live = {}
    wall = SimWall(shell)
    shell.link = wall  # type: ignore[assignment]
    shell.telemetry = wall  # type: ignore[assignment]
    shell.new_uploader = wall.new_uploader  # type: ignore[assignment]
    shell.model.layout = "3x3"
    shell.model.node_map = list(range(1, 10))
    return shell


def main(argv: Optional[list] = None) -> int:  # type: ignore[type-arg]
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=web.DEFAULT_PORT)
    args = parser.parse_args(argv)
    shell = make_shell()
    server = web.make_server(shell, args.host, args.port)
    stop = threading.Event()
    web.start_pump(shell, stop)
    print("simulated wall on http://%s:%d/ (scoreboard on /score)" % (
        args.host, server.server_address[1]))
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
        server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
