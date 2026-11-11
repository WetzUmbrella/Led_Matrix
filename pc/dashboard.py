#!/usr/bin/env python3
"""SE15 dashboard (T-B1-05): configure, build, upload, check readiness and
control playback from the PC.

Owner: Jolyn (Buddy 1), task T-B1-05. Implemented by Ahsanul (B4) for
Jolyn, written 29 Oct 2026 in Jia Jun's (B5) session. Build step 4 (6 Nov
2026): any rows x cols layout, tap-to-place, resumable uploads, the Pico's
reported state, and an optional session id.

States (W6 statechart 1): IDLE, CONFIGURING, VALIDATING CONFIGURATION,
PREPARING ASSETS, UPLOADING ASSETS, VERIFYING UPLOADS (waits for Pico
readiness), READY, MONITORING PLAYBACK, ERROR.

Rules from the T-B1-05 row, all in ``DashboardModel`` so pytest can check
them without a network:
  * brightness is 1 to 8 only (the 8/255 cap, VC-6, OD-02 option (b)); a
    brightness change after the assets exist means rebuilding and
    re-uploading every tile, and the dashboard says so first;
  * readiness comes only from Pico STATUS; Start is enabled only when the
    readiness bitmap equals the mask of the Node IDs in the map;
  * the coordinator state shown is the one the Pico last reported in STATUS;
    the dashboard never assumes it (a local state is kept for the PC's own
    steps only);
  * telemetry is ACKed with its seq (``pc/telemetry.py``);
  * a tile that drops out of the STATUS online bitmap is shown by Node ID,
    with the time, on the next STATUS (NFR9: within 2 s of the Pico log);
  * a new GIF, layout, slot or mode is accepted only while the Pico is
    stopped (see ``DashboardModel.stopped``);
  * an upload skips every tile that already ACKed the current asset, and a
    failed tile does not stop the others; the failures are reported.
The PC's WiFi power save must be off (README); the dashboard cannot check it.

Session id: the header field is 8 bits (layout_table.md section 2), so a
fresh session is a random value 1 to 255 that differs from the last one used,
kept in ``pc/.last_session`` (gitignored).

Usage (interactive shell, ``help`` lists the commands):
    python3 pc/dashboard.py --pico 192.168.1.57 [--session 7]
"""

from __future__ import annotations

import argparse
import cmd
import os
import random
import sys
import time
from typing import Callable, Dict, List, Optional, Sequence, Tuple

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asset_builder as ab  # noqa: E402
import pico_link  # noqa: E402
import proto_layout as pl  # noqa: E402
import telemetry  # noqa: E402
import uploader  # noqa: E402

STATES = ("IDLE", "CONFIGURING", "VALIDATING CONFIGURATION",
          "PREPARING ASSETS", "UPLOADING ASSETS", "VERIFYING UPLOADS",
          "READY", "MONITORING PLAYBACK", "ERROR")

#: PC-side states in which the wall may be stopped (no Pico state known).
STOPPED = ("IDLE", "READY", "ERROR")

#: Pico coordinator states in which the wall is playing or about to play.
PLAYING_PICO_STATES = ("SYNCHRONISING", "COORDINATING PLAYBACK", "PAUSED",
                       "RECOVERING", "COORDINATING GAME")

#: Modes of the wall (GAME_CONTROL codes are in pico_link).
MODES = ("video", "game")

#: The Pico's coordinator state while a round runs (layout_table.md 5.6).
GAME_STATE = "COORDINATING GAME"

#: How long a tap-to-place waits for a TAP, seconds.
PLACE_WAIT_S = 30.0

#: Session ids: 8 bits on the wire (layout_table.md section 2).
SESSION_MIN, SESSION_MAX = 1, 255

#: Where the last session id is kept (gitignored).
SESSION_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            ".last_session")

#: Messages kept in memory for the website.
MESSAGE_KEEP = 200


class Refused(RuntimeError):
    """A change the current state does not allow (the website answers 409)."""


def pick_session_id(last: Optional[int],
                    rng: random.Random = random.Random()) -> int:
    """A fresh session id: random, 1 to 255, not ``last``."""
    while True:
        value = rng.randint(SESSION_MIN, SESSION_MAX)
        if value != last:
            return value


def load_last_session(path: str = SESSION_FILE) -> Optional[int]:
    """The session id saved by the last run, or None."""
    try:
        with open(path, "r", encoding="utf-8") as handle:
            return int(handle.read().strip())
    except (OSError, ValueError):
        return None


def save_last_session(session_id: int, path: str = SESSION_FILE) -> None:
    """Remember the session id in use."""
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("%d\n" % session_id)


def resolve_session(requested: Optional[int],
                    path: str = SESSION_FILE) -> int:
    """``requested`` when given (1 to 255), else a fresh id; the id in use is
    saved so the next fresh one differs from it."""
    if requested is None:
        session = pick_session_id(load_last_session(path))
    else:
        if not SESSION_MIN <= requested <= SESSION_MAX:
            raise ValueError("session must be 1 to 255 (8 bits on the wire)")
        session = requested
    save_last_session(session, path)
    return session


def run_uploads(model: "DashboardModel", session_id: int,
                send: Callable[[str, int, bytes], object],
                address_of: Callable[[int], Optional[str]]) -> Dict[int, str]:
    """Upload every tile asset that is not yet ACKed for this session.

    ``send(address, session_id, payload)`` raises ``uploader.UploadError``
    when a tile refuses; the next tile is still tried. Returns the failed
    Node IDs with the reason. Rerunning sends only the failed tiles.
    """
    failed: Dict[int, str] = {}
    model.state = "UPLOADING ASSETS"
    for node in model.pending_uploads(session_id):
        address = address_of(node)
        if address is None:
            failed[node] = "no TELEMETRY yet, address unknown"
            continue
        try:
            send(address, session_id, model.assets[node].payload)
        except uploader.UploadError as err:
            failed[node] = str(err)
            continue
        model.mark_uploaded(node, session_id)
    model.state = "ERROR" if failed else "VERIFYING UPLOADS"
    for node, reason in sorted(failed.items()):
        model._say("node %d not uploaded: %s" % (node, reason))
    return failed


class DashboardModel:
    """The dashboard's rules and state, independent of sockets and GIFs."""

    def __init__(self, clock: Callable[[], float] = time.time) -> None:
        self.state = "IDLE"
        self.clock = clock
        self.layout = "1x1"
        self.node_map: List[int] = [1] + [0] * 8
        self.policy = pico_link.POLICY_PAUSE
        self.brightness = ab.BRIGHTNESS_MAX
        self.gif: Optional[str] = None
        self.assets: Dict[int, ab.TileAsset] = {}
        self.readiness = 0
        self.online = 0
        self.status: Optional[pico_link.Status] = None
        #: Final score of the last round, None before any round. STATUS
        #: reports 0 outside a round, so it is kept when the round ends.
        self.last_score: Optional[int] = None
        #: The last round's counts (score, hits, missed, wrong, shown,
        #: reaction_ms, stars), None before any round.
        self.last_result: Optional[Dict[str, int]] = None
        self.mode = "video"
        #: The next round's level (DS-08) and tap threshold in mg (0: the
        #: tiles' default, A12).
        self.level = "easy"
        self.threshold_mg = 0
        #: The tap test (A11) as last asked of the Pico.
        self.tap_test = False
        #: Tap-to-place: (slot, deadline) while the website waits for a TAP.
        self.armed: Optional[Tuple[int, float]] = None
        #: Board number taken off each slot, put back by set_slot(on).
        self.parked: Dict[int, int] = {}
        #: Node ID -> (session_id, crc32) of the asset it last ACKed.
        self.acked: Dict[int, Tuple[int, int]] = {}
        self.messages: List[str] = []

    def _say(self, text: str) -> None:
        self.messages.append("%.3f %s" % (self.clock(), text))
        del self.messages[:-MESSAGE_KEEP]

    def shape(self) -> Tuple[int, int]:
        """(rows, cols) of the layout."""
        return ab.parse_shape(self.layout)

    def required(self) -> int:
        """Readiness mask of the configured map."""
        return pico_link.required_mask(self.node_map)

    def pico_state(self) -> str:
        """Coordinator state as the Pico last reported it in STATUS."""
        return self.status.state_name() if self.status else "NO STATUS YET"

    def stopped(self) -> bool:
        """True when the wall is stopped. The Pico's STATUS decides once it
        has reported; before that the PC's own state does."""
        if self.status is not None:
            return self.status.state_name() not in PLAYING_PICO_STATES
        return self.state in STOPPED

    def _check_slot(self, slot: int) -> None:
        if not self.stopped():
            raise Refused("stop playback before changing the layout")
        rows, cols = self.shape()
        if not 0 <= slot < rows * cols:
            raise ab.AssetError("slot %d is outside the %s layout"
                                % (slot, self.layout))

    def _drop_assets(self) -> None:
        """A change of GIF, layout, map or brightness: assets are rebuilt."""
        self.assets = {}
        self.state = "IDLE"

    def configure(self, gif: str, layout: str, node_map: Sequence[int],
                  policy: int, brightness: int) -> bool:
        """CONFIGURING then VALIDATING CONFIGURATION; True when accepted."""
        if not self.stopped():
            self._say("refused: stop playback before changing the GIF or "
                      "layout")
            return False
        self.state = "CONFIGURING"
        self.state = "VALIDATING CONFIGURATION"
        try:
            ab.parse_layout(layout, node_map)
            ab.check_brightness(brightness)
        except ab.AssetError as err:
            self._say("refused: %s" % err)
            self.state = "IDLE" if not self.assets else "READY"
            return False
        self.gif, self.layout = gif, layout
        self.node_map, self.policy = list(node_map), policy
        self.brightness = brightness
        self._drop_assets()
        self._say("configuration accepted")
        return True

    def set_brightness(self, value: int) -> bool:
        """Change the cap; refused above 8. Existing assets are dropped:
        a rebuild and re-upload of every tile is needed (VC-6)."""
        try:
            ab.check_brightness(value)
        except ab.AssetError as err:
            self._say("refused: %s" % err)
            return False
        if not self.stopped():
            self._say("refused: stop playback first")
            return False
        if self.assets and value != self.brightness:
            self._say("brightness change: every tile asset will be rebuilt "
                      "and re-uploaded (VC-6)")
            self._drop_assets()
        self.brightness = value
        return True

    def set_grid(self, rows: int, cols: int) -> None:
        """Change the wall shape. Tiles keep their row and column when the
        new grid still has that position; the rest are taken off the map."""
        if not self.stopped():
            raise Refused("stop playback before changing the grid")
        layout = "%dx%d" % (rows, cols)
        ab.parse_shape(layout)
        _old_rows, old_cols = self.shape()
        new_map = [0] * pl.MAP_SLOTS
        for slot, node in enumerate(self.node_map):
            row, col = divmod(slot, old_cols)
            if node and row < rows and col < cols:
                new_map[row * cols + col] = node
        if not any(new_map):
            raise ab.AssetError("no tile is left inside %s; place one first"
                                % layout)
        self.layout, self.node_map = layout, new_map
        self.parked = {}
        self._drop_assets()
        self._say("grid %s" % layout)

    def set_slot(self, slot: int, on: bool) -> int:
        """Slot off writes 0 and remembers the board number; slot on writes
        that board number back. Returns the board number now or before."""
        self._check_slot(slot)
        new_map = list(self.node_map)
        if on:
            if new_map[slot]:
                return new_map[slot]
            node = self.parked.get(slot, 0)
            if not node:
                raise ab.AssetError("slot %d has no tile to put back; place "
                                    "one there first" % slot)
            if node in new_map:
                raise ab.AssetError("tile %d is already in another slot"
                                    % node)
            new_map[slot] = node
            self.parked.pop(slot, None)
        else:
            node = new_map[slot]
            if not node:
                return 0
            if sum(1 for other in new_map if other) == 1:
                raise ab.AssetError("at least one tile must stay on")
            self.parked[slot] = node
            new_map[slot] = 0
        self.node_map = new_map
        self._drop_assets()
        self._say("slot %d %s" % (slot, "on" if on else "off"))
        return node

    def _place_node(self, slot: int, node: int) -> None:
        new_map = [0 if other == node else other for other in self.node_map]
        new_map[slot] = node
        self.node_map = new_map
        self.parked.pop(slot, None)
        self._drop_assets()
        self._say("slot %d holds tile %d" % (slot, node))

    def place(self, slot: int, next_tap: Callable[[], Optional[int]]
              ) -> Optional[int]:
        """Tap-to-place (build step 4): ``next_tap()`` returns the board
        number of the next TAP, or None when none came. That number goes into
        ``node_map[slot]``; a tile already elsewhere moves here."""
        self._check_slot(slot)
        node = next_tap()
        if node is None:
            self._say("place: no tap for slot %d" % slot)
            return None
        if not 1 <= node <= pl.NODE_MAX:
            raise ab.AssetError("TAP named board %d; boards are 1 to 9"
                                % node)
        self._place_node(slot, node)
        return node

    def arm_place(self, slot: int) -> None:
        """Highlight ``slot`` and wait for a TAP for it (website flow)."""
        self._check_slot(slot)
        self.armed = (slot, self.clock() + PLACE_WAIT_S)
        self._say("place: tap the tile for slot %d" % slot)

    def poll_place(self, next_tap: Callable[[], Optional[int]]
                   ) -> Optional[int]:
        """Called on every poll: place the armed slot if a TAP is waiting,
        or give up after PLACE_WAIT_S. Returns the board number placed."""
        if self.armed is None:
            return None
        slot, deadline = self.armed
        if self.clock() > deadline:
            self.armed = None
            self._say("place: slot %d timed out" % slot)
            return None
        node = next_tap()
        if node is None:
            return None
        self.armed = None
        return self.place(slot, lambda: node)

    def set_mode(self, mode: str) -> None:
        """Video or game; only while the wall is stopped."""
        if mode not in MODES:
            raise ab.AssetError("mode must be video or game")
        if not self.stopped():
            raise Refused("stop playback before changing the mode")
        self.mode = mode
        self._say("mode %s" % mode)

    def start_game(self) -> None:
        """Round start: game mode, and the Pico ready (every tile ready and
        synchronised). Raises Refused otherwise."""
        if self.mode != "game":
            raise Refused("switch to game mode before starting a round")
        if self.pico_state() != "READY":
            raise Refused("the wall must be READY to start a round "
                          "(now %s)" % self.pico_state())

    def abort_game(self) -> None:
        """Abort: only while the Pico runs a round (armed, counting down or
        playing: all COORDINATING GAME)."""
        if self.pico_state() != GAME_STATE:
            raise Refused("no round is running")

    def round_state(self) -> str:
        """The Pico's round state (DS-08), GAME IDLE before any STATUS."""
        return self.status.round_name() if self.status else "GAME IDLE"

    def arm_game(self) -> None:
        """Arm a round (DS-08): as start_game(); the start sprite appears and
        the child's tap on it starts the countdown."""
        self.start_game()

    def start_now(self) -> None:
        """Skip the start sprite: only while armed."""
        if self.round_state() != "ARMED":
            raise Refused("no round is armed (now %s)" % self.round_state())

    def set_tap_test(self, on: bool) -> None:
        """The tap test (A11): game mode, and the wall READY with no round
        armed or playing."""
        if self.mode != "game":
            raise Refused("switch to game mode before the tap test")
        if self.pico_state() != "READY":
            raise Refused("the tap test needs the wall READY "
                          "(now %s)" % self.pico_state())

    def set_level(self, level: str) -> None:
        """The next round's level: easy, normal or hard."""
        if level not in pico_link.LEVELS:
            raise ab.AssetError("level must be easy, normal or hard")
        self.level = level
        self._say("level %s" % level)

    def set_threshold(self, threshold_mg: int) -> None:
        """The tap threshold for the next ARM, mg: 0 (the tiles' default) or
        a multiple of 20 from 60 to 5,000 (A12)."""
        step = pico_link.THRESHOLD_STEP_MG
        if threshold_mg != 0 and not (
                60 <= threshold_mg <= pico_link.THRESHOLD_MAX_MG and
                threshold_mg % step == 0):
            raise ab.AssetError("threshold must be 0 or a multiple of %d "
                                "from 60 to %d mg" % (
                                    step, pico_link.THRESHOLD_MAX_MG))
        self.threshold_mg = threshold_mg
        self._say("tap threshold %s" % (
            "default" if threshold_mg == 0 else "%d mg" % threshold_mg))

    def level_code(self) -> int:
        """The next round's level on the wire."""
        return pico_link.LEVELS.index(self.level)

    def threshold_code(self) -> int:
        """The tap threshold on the wire, 20 mg steps."""
        return self.threshold_mg // pico_link.THRESHOLD_STEP_MG

    def on_status(self, status: pico_link.Status) -> None:
        """New Pico STATUS: readiness, online losses, READY or not."""
        lost = self.online & ~status.online & self.required()
        for node in pico_link.nodes_in(lost):
            self._say("tile %d lost (Pico STATUS online bit cleared)" % node)
        prev = self.status
        if status.round_name() == "RESULTS" and (
                prev is None or prev.round_name() != "RESULTS"):
            # The round ended: RESULTS carries the final counts (DS-08).
            self.last_score = status.score
            self.last_result = {
                "score": status.score, "hits": status.hits(),
                "missed": status.missed, "wrong": status.wrong,
                "shown": status.shown, "reaction_ms": status.reaction_ms,
                "stars": status.stars()}
            self._say("round over, score %d, %d of %d moles, %d wrong, "
                      "%d stars" % (status.score, status.hits(),
                                    status.shown, status.wrong,
                                    status.stars()))
        elif (prev is not None) and (prev.round_name() in
                                     ("ARMED", "COUNTDOWN")) and \
                (status.round_name() == "GAME IDLE"):
            self._say("round not started (aborted or the start sprite "
                      "timed out)")
        self.status = status
        self.online = status.online
        self.readiness = status.readiness
        if self.state == "VERIFYING UPLOADS" and self.start_enabled():
            self.state = "READY"
            self._say("all tiles ready")

    def start_enabled(self) -> bool:
        """Start only when the Pico readiness bitmap equals the mask."""
        return (self.readiness & self.required()) == self.required() \
            and self.state in ("VERIFYING UPLOADS", "READY")

    def missing(self) -> List[int]:
        """Node IDs in the map that are not ready."""
        return pico_link.nodes_in(self.required() & ~self.readiness)

    def pending_uploads(self, session_id: int) -> List[int]:
        """Node IDs whose current asset is not ACKed yet for this session."""
        return [node for node, asset in sorted(self.assets.items())
                if self.acked.get(node) != (session_id, asset.crc32)]

    def mark_uploaded(self, node: int, session_id: int) -> None:
        """The tile ACKed every chunk of its current asset."""
        self.acked[node] = (session_id, self.assets[node].crc32)

    def snapshot(self) -> Dict[str, object]:
        """What the website shows. Built from the model only; no sockets."""
        rows, cols = self.shape()
        status = self.status
        return {
            "rows": rows, "cols": cols, "layout": self.layout,
            "node_map": list(self.node_map),
            "parked": [self.parked.get(slot, 0)
                       for slot in range(pl.MAP_SLOTS)],
            "pico_state": self.pico_state(),
            "readiness": self.readiness, "online": self.online,
            "score": status.score if status else 0,
            "last_score": self.last_score,
            "last_result": self.last_result,
            "round_state": self.round_state(),
            "level": self.level, "threshold_mg": self.threshold_mg,
            "tap_test": self.tap_test,
            "round": ({
                "level": status.level_name(), "hits": status.hits(),
                "missed": status.missed, "wrong": status.wrong,
                "shown": status.shown, "time_left_s": status.time_left_s,
                "reaction_ms": status.reaction_ms} if status else None),
            "start_now_enabled": self.round_state() == "ARMED",
            "mode": self.mode, "stopped": self.stopped(),
            "game_running": self.pico_state() == GAME_STATE,
            "game_start_enabled": (self.mode == "game" and
                                   self.pico_state() == "READY"),
            "armed": self.armed[0] if self.armed else None,
            "pc_state": self.state, "gif": self.gif,
            "start_enabled": self.start_enabled(),
            "missing": self.missing(),
            "messages": self.messages[-8:],
        }


class DashboardShell(cmd.Cmd):
    """Interactive front end over DashboardModel, PicoLink, the uploader
    and the telemetry receiver. The website (pc/web.py) uses the same
    methods, without the printing."""

    intro = "SE15 dashboard. Type help or ? to list commands."
    prompt = "(se15) "

    def __init__(self, pico_ip: str, session_id: int) -> None:
        super().__init__()
        self.model = DashboardModel()
        self.link = pico_link.PicoLink(pico_ip)
        self.telemetry = telemetry.TelemetryReceiver()
        self.uploader = uploader.Uploader()
        self.session_id = session_id

    def _flush(self) -> None:
        self.telemetry.poll()
        status = self.link.poll_status()
        if status is not None:
            self.model.on_status(status)

    def _print_messages(self) -> None:
        for line in self.model.messages:
            print(line)
        self.model.messages.clear()

    def postcmd(self, stop: bool, line: str) -> bool:
        self._flush()
        self._print_messages()
        return stop

    # Steps shared by the shell and the website.

    def build(self) -> None:
        """Build every tile asset (PREPARING ASSETS). Raises on refusal."""
        model = self.model
        model.state = "PREPARING ASSETS"
        try:
            frames, delays = ab.load_gif(model.gif or "")
            model.assets = ab.build_assets(frames, delays, model.layout,
                                           model.node_map, model.brightness)
        except (ab.AssetError, OSError):
            model.state = "ERROR"
            raise

    def send_config(self) -> None:
        """SESSION_CONFIG: the shape, the map and one CRC32 per position."""
        model = self.model
        frame_count = next(iter(model.assets.values())).frame_count
        crcs = [model.assets[n].crc32 if n else 0 for n in model.node_map]
        rows, cols = model.shape()
        self.link.send_session_config(self.session_id, rows, cols,
                                      model.policy, list(model.node_map),
                                      frame_count, crcs)

    def _send_asset(self, address: str, session_id: int,
                    payload: bytes) -> object:
        stats = self.uploader.upload_asset(address, session_id, payload)
        self.model._say("uploaded to %s: %d of %d chunks ACKed, resent %d, "
                        "%.2f s" % (address, len(stats.acked), stats.chunks,
                                    stats.resent, stats.seconds))
        return stats

    def upload_pending(self) -> Dict[int, str]:
        """Upload every tile that has not ACKed its asset; returns failures."""
        self.telemetry.poll()
        return run_uploads(self.model, self.session_id, self._send_asset,
                           self.telemetry.address_of)

    def publish(self) -> Dict[int, str]:
        """Rebuild, send SESSION_CONFIG and upload what is pending. Used after
        every change of GIF, layout, slot or brightness (build step 6)."""
        if self.model.gif is None:
            raise ab.AssetError("no GIF configured yet (config first)")
        self.build()
        self.send_config()
        return self.upload_pending()

    # Website calls.

    def snapshot(self) -> Dict[str, object]:
        """Fresh STATUS and telemetry, an armed placement, then the model."""
        self._flush()
        if self.model.armed is not None:
            try:
                node = self.model.poll_place(
                    lambda: self.link.next_tap(0.0))
            except (ab.AssetError, Refused) as err:
                self.model._say("place refused: %s" % err)
                node = None
            if node is not None:
                self._publish_quiet()
        return self.model.snapshot()

    def _publish_quiet(self) -> None:
        try:
            self.publish()
        except (ab.AssetError, OSError) as err:
            self.model._say("publish failed: %s" % err)

    def _after_change(self) -> Dict[str, object]:
        """Rebuild and re-upload after a layout change, when a GIF is set."""
        if self.model.gif is not None:
            self.publish()
        return self.snapshot()

    def set_grid(self, rows: int, cols: int) -> Dict[str, object]:
        self.model.set_grid(rows, cols)
        return self._after_change()

    def set_slot(self, slot: int, on: bool) -> Dict[str, object]:
        self.model.set_slot(slot, on)
        return self._after_change()

    def arm_place(self, slot: int) -> Dict[str, object]:
        self.link.taps = []  # A TAP from before the press does not count.
        self.model.arm_place(slot)
        return self.snapshot()

    def set_mode(self, mode: str) -> Dict[str, object]:
        """Mode switch, acknowledged by the Pico; the old mode is kept when it
        is not acknowledged."""
        previous = self.model.mode
        self.model.set_mode(mode)
        code = pico_link.MODE_GAME if mode == "game" else pico_link.MODE_VIDEO
        if not self.link.send_game_control(code):
            self.model.mode = previous
            raise OSError("the Pico did not acknowledge the mode change")
        return self.snapshot()

    def start_game(self) -> Dict[str, object]:
        """Round start: refused unless ready in game mode; the Pico must
        acknowledge the command."""
        self._flush()
        self.model.start_game()
        if not self.link.send_game_control(pico_link.GAME_START):
            raise OSError("the Pico did not acknowledge the round start")
        return self.snapshot()

    def arm_game(self) -> Dict[str, object]:
        """Arm a round at the model's level and tap threshold (DS-08)."""
        self._flush()
        self.model.arm_game()
        if not self.link.send_game_control(
                pico_link.GAME_ARM, level=self.model.level_code(),
                threshold=self.model.threshold_code()):
            raise OSError("the Pico did not acknowledge the arm")
        self.model.tap_test = False
        return self.snapshot()

    def start_now(self) -> Dict[str, object]:
        """Skip the start sprite: the countdown starts now."""
        self._flush()
        self.model.start_now()
        if not self.link.send_game_control(pico_link.GAME_START_NOW):
            raise OSError("the Pico did not acknowledge start now")
        return self.snapshot()

    def set_tap_test(self, on: bool) -> Dict[str, object]:
        """The tap test on or off (A11)."""
        self._flush()
        self.model.set_tap_test(on)
        code = pico_link.GAME_TAP_TEST_ON if on else \
            pico_link.GAME_TAP_TEST_OFF
        if not self.link.send_game_control(code):
            raise OSError("the Pico did not acknowledge the tap test")
        self.model.tap_test = on
        return self.snapshot()

    def set_level(self, level: str) -> Dict[str, object]:
        self.model.set_level(level)
        return self.snapshot()

    def set_threshold(self, threshold_mg: int) -> Dict[str, object]:
        self.model.set_threshold(threshold_mg)
        return self.snapshot()

    def abort_game(self) -> Dict[str, object]:
        """Abort the round now: an armed or counting round clears, a playing
        one ends as at the time limit."""
        self._flush()
        self.model.abort_game()
        if not self.link.send_game_control(pico_link.GAME_END):
            raise OSError("the Pico did not acknowledge the abort")
        return self.snapshot()

    # Shell commands.

    def do_config(self, arg: str) -> None:
        """config GIF LAYOUT MAP POLICY BRIGHTNESS
        e.g. config demo.gif 2x2 1,2,3,4,0,0,0,0,0 pause 8"""
        try:
            gif, layout, map_text, policy, brightness = arg.split()
            node_map = [int(n) for n in map_text.split(",")]
            pol = pico_link.POLICY_PAUSE if policy == "pause" \
                else pico_link.POLICY_CONTINUE
            self.model.configure(gif, layout, node_map, pol, int(brightness))
        except ValueError:
            print("usage: config GIF LAYOUT MAP pause|continue BRIGHTNESS")

    def do_brightness(self, arg: str) -> None:
        """brightness N   (1 to 8)"""
        try:
            self.model.set_brightness(int(arg))
        except ValueError:
            print("usage: brightness N")

    def do_grid(self, arg: str) -> None:
        """grid ROWS COLS   (each 1 to 9, rows x cols at most 9)"""
        try:
            rows, cols = (int(part) for part in arg.split())
            self.set_grid(rows, cols)
        except (ValueError, ab.AssetError, Refused, OSError) as err:
            print("grid refused: %s" % err)
            return
        self._print_messages()

    def do_place(self, arg: str) -> None:
        """place SLOT   then tap the tile for that slot (30 s)"""
        try:
            slot = int(arg)
            self.link.taps = []
            node = self.model.place(
                slot, lambda: self.link.next_tap(PLACE_WAIT_S))
        except (ValueError, ab.AssetError, Refused) as err:
            print("place refused: %s" % err)
            return
        if node is not None:
            self._after_change()
        self._print_messages()

    def do_build(self, _arg: str) -> None:
        """Build every tile asset (PREPARING ASSETS) and send
        SESSION_CONFIG."""
        if self.model.gif is None:
            print("config first")
            return
        try:
            self.build()
            self.send_config()
        except (ab.AssetError, OSError) as err:
            print("build failed: %s" % err)
            return
        for node, asset in sorted(self.model.assets.items()):
            print("node %d: %d frames, crc32 0x%08x" % (
                node, asset.frame_count, asset.crc32))

    def do_upload(self, _arg: str) -> None:
        """Upload every tile asset that has not been ACKed yet; reports the
        tiles that failed (rerun to retry only those)."""
        if not self.model.assets:
            print("build first")
            return
        failed = self.upload_pending()
        print("failed: %s" % (
            ", ".join("node %d (%s)" % item for item in sorted(failed.items()))
            or "none"))

    def do_publish(self, _arg: str) -> None:
        """Rebuild, send SESSION_CONFIG and upload what is pending."""
        try:
            failed = self.publish()
        except (ab.AssetError, OSError) as err:
            print("publish failed: %s" % err)
            return
        print("failed: %s" % (
            ", ".join("node %d (%s)" % item for item in sorted(failed.items()))
            or "none"))

    def do_status(self, _arg: str) -> None:
        """Show the Pico's STATUS, the PC state and the telemetry rows."""
        status = self.model.status
        if status:
            print("pico %s readiness=0x%03x online=0x%03x score=%d" % (
                status.state_name(), status.readiness, status.online,
                status.score))
            print("round %s level=%s hits=%d missed=%d wrong=%d shown=%d "
                  "left=%d s reaction=%d ms" % (
                      status.round_name(), status.level_name(),
                      status.hits(), status.missed, status.wrong,
                      status.shown, status.time_left_s, status.reaction_ms))
        else:
            print("pico: no STATUS yet")
        print("pc %s; start %s; missing %s" % (
            self.model.state,
            "enabled" if self.model.start_enabled() else "disabled",
            self.model.missing()))
        for row in sorted(self.telemetry.tiles.values(),
                          key=lambda r: r.node_id):
            print("tile %d %s faults=%s orient=%d display=%d" % (
                row.node_id, row.address, row.faults(), row.orientation,
                row.display_state))

    def do_game(self, arg: str) -> None:
        """game arm [easy|normal|hard] | now | abort | start
        arm: the start sprite waits for the child's tap (DS-08); now: skip
        it; abort: clear or end the round; start: a v1 round at once."""
        words = arg.split()
        try:
            if words[:1] == ["arm"]:
                if len(words) > 1:
                    self.model.set_level(words[1])
                self.arm_game()
            elif words == ["now"]:
                self.start_now()
            elif words == ["abort"]:
                self.abort_game()
            elif words == ["start"]:
                self.start_game()
            else:
                print("usage: game arm [easy|normal|hard] | now | abort | "
                      "start")
        except (ab.AssetError, Refused, OSError) as err:
            print("game refused: %s" % err)
        self._print_messages()

    def do_tap(self, arg: str) -> None:
        """tap on|off   the tap test: every tap lights its tile (A11)"""
        try:
            if arg.strip() not in ("on", "off"):
                print("usage: tap on|off")
                return
            self.set_tap_test(arg.strip() == "on")
        except (Refused, OSError) as err:
            print("tap test refused: %s" % err)

    def do_threshold(self, arg: str) -> None:
        """threshold MG   tap threshold for the next arm; 0 = default"""
        try:
            self.set_threshold(int(arg))
        except (ValueError, ab.AssetError) as err:
            print("threshold refused: %s" % err)
        self._print_messages()

    def do_mode(self, arg: str) -> None:
        """mode video|game   (only while stopped)"""
        try:
            self.set_mode(arg.strip())
        except (ab.AssetError, Refused, OSError) as err:
            print("mode refused: %s" % err)
            return
        self._print_messages()

    def do_start(self, _arg: str) -> None:
        """Start (only when every mapped tile is ready)."""
        if not self.model.start_enabled():
            print("start disabled: not ready %s" % self.model.missing())
            return
        self.link.start()

    def do_pause(self, _arg: str) -> None:
        """Pause."""
        self.link.pause()

    def do_resume(self, _arg: str) -> None:
        """Resume."""
        self.link.resume()

    def do_stop(self, _arg: str) -> None:
        """Stop."""
        self.link.stop()

    def do_quit(self, _arg: str) -> bool:
        """Quit."""
        return True


def main(argv: Optional[List[str]] = None) -> int:
    """Command line entry point."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--pico", required=True, help="Pico IP address")
    parser.add_argument("--session", type=int, default=None,
                        help="session id 1 to 255; default: a fresh one, "
                             "different from the last run")
    args = parser.parse_args(argv)
    try:
        session = resolve_session(args.session)
    except ValueError as err:
        print("dashboard: %s" % err, file=sys.stderr)
        return 1
    DashboardShell(args.pico, session).cmdloop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
