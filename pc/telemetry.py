"""TELEMETRY receive and TELEMETRY_ACK for the dashboard (T-B1-05, VC-4).

Owner: Jolyn (Buddy 1), task T-B1-05. Implemented by Ahsanul (B4) for
Jolyn, written 29 Oct 2026 in Jia Jun's (B5) session.

Every tile broadcasts TELEMETRY {seq, node_id, fault word, orientation,
display state} to UDP 4215 once per telemetry period (1 s, PROVISIONAL,
OD-11). The receiver replies TELEMETRY_ACK {acked_seq = header seq} to the
sender's address on port 4214, so the tile clears the event bits it sent
(VC-4), and keeps one row per Node ID: address, last time seen, fault word,
orientation and display state. The address is how ``pc/uploader.py`` finds
a tile (layout_table.md section 11).
"""

from __future__ import annotations

import os
import socket
import sys
import time
from dataclasses import dataclass
from typing import Dict, Optional, Tuple

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import proto_layout as pl  # noqa: E402

#: PC port for TELEMETRY; tile port for TELEMETRY_ACK (section 11).
TELEMETRY_PORT = 4215
TILE_PC_PORT = 4214

#: Fault bit names (panel_sync/main/fault_word.h, PROVISIONAL OD-12).
FAULT_NAMES = {0: "WIFI", 1: "CRC_FAIL", 2: "QUEUE_OVERFLOW",
               3: "BEACON_LOSS", 4: "IMU", 5: "MISSED_DEADLINE",
               6: "ASSET_FORMAT", 7: "DISPLAY", 8: "SYNC_STALE"}

#: Display states (layout_table.md section 5.8).
DISPLAY_STATES = ("INITIALISING DISPLAY", "DISPLAY IDLE", "PREPARING FRAME",
                  "TRANSMITTING PIXELS", "WAITING FOR NEXT TICK",
                  "DISPLAY PAUSED", "DISPLAY FAULT")


@dataclass
class TileRow:
    """Latest telemetry of one tile."""

    node_id: int
    address: str
    last_seen: float
    seq: int
    fault_word: int
    orientation: int
    display_state: int
    #: The session in the TELEMETRY header: the one the tile works in
    #: (DS-12 A1: an upload waits until it is the PC's).
    session_id: int = 0

    def faults(self) -> str:
        """Names of the set fault bits, or ``none``."""
        names = [name for bit, name in sorted(FAULT_NAMES.items())
                 if self.fault_word & (1 << bit)]
        return ",".join(names) or "none"


def make_ack(session_id: int, seq: int) -> bytes:
    """TELEMETRY_ACK for the TELEMETRY with header ``seq``."""
    return pl.encode_telemetry_ack(session_id=session_id, seq=seq,
                                   acked_seq=seq)


def parse(data: bytes, address: str,
          now: float) -> Optional[Tuple[TileRow, bytes]]:
    """A datagram to (row, ACK), or None if it is not a valid TELEMETRY."""
    try:
        msg = pl.decode_telemetry(data)
    except pl.LayoutError:
        return None
    seq = int(msg["seq"])  # type: ignore[call-overload]
    row = TileRow(node_id=int(msg["node_id"]),  # type: ignore[call-overload]
                  address=address, last_seen=now, seq=seq,
                  fault_word=int(msg["fault_word"]),  # type: ignore
                  orientation=int(msg["orientation"]),  # type: ignore
                  display_state=int(msg["display_state"]),  # type: ignore
                  session_id=int(msg["session_id"]))  # type: ignore
    return row, make_ack(int(msg["session_id"]), seq)  # type: ignore


class TelemetryReceiver:
    """Non-blocking receiver on UDP 4215 that ACKs every TELEMETRY."""

    def __init__(self, port: int = TELEMETRY_PORT) -> None:
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        if hasattr(socket, "SO_REUSEPORT"):
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
        self.sock.bind(("", port))
        self.sock.setblocking(False)
        self.tiles: Dict[int, TileRow] = {}

    def poll(self) -> int:
        """Read every waiting datagram; returns how many rows changed."""
        changed = 0
        while True:
            try:
                data, (addr, _port) = self.sock.recvfrom(64)
            except (BlockingIOError, socket.timeout):
                return changed
            parsed = parse(data, addr, time.monotonic())
            if parsed is None:
                continue
            row, ack = parsed
            self.tiles[row.node_id] = row
            self.sock.sendto(ack, (addr, TILE_PC_PORT))
            changed += 1

    def address_of(self, node_id: int) -> Optional[str]:
        """Last address the tile sent TELEMETRY from, or None."""
        row = self.tiles.get(node_id)
        return row.address if row else None

    def session_of(self, node_id: int) -> Optional[int]:
        """The session of the tile's last TELEMETRY, or None."""
        row = self.tiles.get(node_id)
        return row.session_id if row else None

    def close(self) -> None:
        """Release the port."""
        self.sock.close()
