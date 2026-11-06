#!/usr/bin/env python3
"""SE15 dashboard (T-B1-05): configure, build, upload, check readiness and
control playback from the PC.

Owner: Jolyn (Buddy 1), task T-B1-05. Implemented by Ahsanul (B4) for
Jolyn, written 29 Oct 2026 in Jia Jun's (B5) session.

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
  * telemetry is ACKed with its seq (``pc/telemetry.py``);
  * a tile that drops out of the STATUS online bitmap is shown by Node ID,
    with the time, on the next STATUS (NFR9: within 2 s of the Pico log);
  * a new GIF or layout is accepted only while stopped (IDLE, READY or
    ERROR).
The PC's WiFi power save must be off (README); the dashboard cannot check it.

Usage (interactive shell, ``help`` lists the commands):
    python3 pc/dashboard.py --pico 192.168.1.57
"""

from __future__ import annotations

import argparse
import cmd
import os
import sys
import time
from typing import Callable, Dict, List, Optional, Sequence

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asset_builder as ab  # noqa: E402
import pico_link  # noqa: E402
import proto_layout as pl  # noqa: E402

STATES = ("IDLE", "CONFIGURING", "VALIDATING CONFIGURATION",
          "PREPARING ASSETS", "UPLOADING ASSETS", "VERIFYING UPLOADS",
          "READY", "MONITORING PLAYBACK", "ERROR")

#: States in which a new GIF or layout may be accepted.
STOPPED = ("IDLE", "READY", "ERROR")


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
        self.messages: List[str] = []

    def _say(self, text: str) -> None:
        self.messages.append("%.3f %s" % (self.clock(), text))

    def required(self) -> int:
        """Readiness mask of the configured map."""
        return pico_link.required_mask(self.node_map)

    def configure(self, gif: str, layout: str, node_map: Sequence[int],
                  policy: int, brightness: int) -> bool:
        """CONFIGURING then VALIDATING CONFIGURATION; True when accepted."""
        if self.state not in STOPPED:
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
        self.assets = {}
        self.state = "IDLE"
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
        if self.state not in STOPPED:
            self._say("refused: stop playback first")
            return False
        if self.assets and value != self.brightness:
            self._say("brightness change: every tile asset will be rebuilt "
                      "and re-uploaded (VC-6)")
            self.assets = {}
            self.state = "IDLE"
        self.brightness = value
        return True

    def on_status(self, status: pico_link.Status) -> None:
        """New Pico STATUS: readiness, online losses, READY or not."""
        lost = self.online & ~status.online & self.required()
        for node in pico_link.nodes_in(lost):
            self._say("tile %d lost (Pico STATUS online bit cleared)" % node)
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


class DashboardShell(cmd.Cmd):
    """Interactive front end over DashboardModel, PicoLink, the uploader
    and the telemetry receiver."""

    intro = "SE15 dashboard. Type help or ? to list commands."
    prompt = "(se15) "

    def __init__(self, pico_ip: str, session_id: int) -> None:
        super().__init__()
        import telemetry  # Opens UDP 4215.
        import uploader
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
        for line in self.model.messages:
            print(line)
        self.model.messages.clear()

    def postcmd(self, stop: bool, line: str) -> bool:
        self._flush()
        return stop

    def do_config(self, arg: str) -> None:
        """config GIF LAYOUT MAP POLICY BRIGHTNESS
        e.g. config demo.gif 2x2 1,2,0,3,4,0,0,0,0 pause 8"""
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

    def do_build(self, _arg: str) -> None:
        """Build every tile asset (PREPARING ASSETS) and send
        SESSION_CONFIG."""
        model = self.model
        if model.gif is None:
            print("config first")
            return
        model.state = "PREPARING ASSETS"
        try:
            frames, delays = ab.load_gif(model.gif)
            model.assets = ab.build_assets(frames, delays, model.layout,
                                           model.node_map, model.brightness)
        except (ab.AssetError, OSError) as err:
            model.state = "ERROR"
            print("build failed: %s" % err)
            return
        frame_count = next(iter(model.assets.values())).frame_count
        crcs = [model.assets[n].crc32 if n else 0 for n in model.node_map]
        # The wire map is row-major over rows x cols (build step 3);
        # asset_builder keeps its 3-column positions until build step 4.
        rows, cols, wire_map, wire_crcs = pl.grid_map(model.layout,
                                                      model.node_map, crcs)
        self.link.send_session_config(self.session_id, rows, cols,
                                      model.policy, wire_map, frame_count,
                                      wire_crcs)
        for node, asset in sorted(model.assets.items()):
            print("node %d: %d frames, crc32 0x%08x" % (
                node, asset.frame_count, asset.crc32))

    def do_upload(self, _arg: str) -> None:
        """Upload every tile asset to the address its TELEMETRY came from."""
        import uploader
        model = self.model
        model.state = "UPLOADING ASSETS"
        self.telemetry.poll()
        for node, asset in sorted(model.assets.items()):
            address = self.telemetry.address_of(node)
            if address is None:
                print("node %d: no TELEMETRY yet, address unknown" % node)
                model.state = "ERROR"
                return
            try:
                stats = self.uploader.upload_asset(address, self.session_id,
                                                   asset.payload)
            except uploader.UploadError as err:
                print("node %d: %s" % (node, err))
                model.state = "ERROR"
                return
            print("node %d: %d of %d chunks ACKed, resent %d, %.2f s" % (
                node, len(stats.acked), stats.chunks, stats.resent,
                stats.seconds))
        model.state = "VERIFYING UPLOADS"

    def do_status(self, _arg: str) -> None:
        """Show Pico STATUS, readiness and telemetry rows."""
        status = self.link.poll_status()
        if status:
            print("pico %s readiness=0x%03x online=0x%03x" % (
                status.state_name(), status.readiness, status.online))
        print("state %s; start %s; missing %s" % (
            self.model.state,
            "enabled" if self.model.start_enabled() else "disabled",
            self.model.missing()))
        for row in sorted(self.telemetry.tiles.values(),
                          key=lambda r: r.node_id):
            print("tile %d %s faults=%s orient=%d display=%d" % (
                row.node_id, row.address, row.faults(), row.orientation,
                row.display_state))

    def do_start(self, _arg: str) -> None:
        """Start (only when every mapped tile is ready)."""
        if not self.model.start_enabled():
            print("start disabled: not ready %s" % self.model.missing())
            return
        self.link.start()
        self.model.state = "MONITORING PLAYBACK"

    def do_pause(self, _arg: str) -> None:
        """Pause."""
        self.link.pause()

    def do_resume(self, _arg: str) -> None:
        """Resume."""
        self.link.resume()

    def do_stop(self, _arg: str) -> None:
        """Stop."""
        self.link.stop()
        self.model.state = "READY" if self.model.assets else "IDLE"

    def do_quit(self, _arg: str) -> bool:
        """Quit."""
        return True


def main(argv: Optional[List[str]] = None) -> int:
    """Command line entry point."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--pico", required=True, help="Pico IP address")
    parser.add_argument("--session", type=int, default=1)
    args = parser.parse_args(argv)
    DashboardShell(args.pico, args.session).cmdloop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
