"""PC to Pico link: SESSION_CONFIG, Start, Pause, Resume, Stop, STATUS and the
game's mode, start and end (T-B1-05, whack-a-mole build step 7).

Owner: Jolyn (Buddy 1), task T-B1-05. Implemented by Ahsanul (B4) for
Jolyn, written 29 Oct 2026 in Jia Jun's (B5) session.

Transport (layout_table.md section 11, PROVISIONAL, owner Benjamin): the PC
sends to the Pico's UDP 4212 from a socket bound to UDP 4213; the Pico sends
STATUS every 250 ms to the address and port of the last valid datagram from
the PC. Commands reuse the START_AT, PAUSE, RESUME_AT and STOP rows with
t_start, t_resume and frame_index 0, because the Pico chooses them
(section 10 item 11). One seq counter for everything this PC sends
(section 10 item 13).
"""

from __future__ import annotations

import os
import select
import socket
import sys
import time
from dataclasses import dataclass
from typing import List, Optional, Sequence

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import proto_layout as pl  # noqa: E402

#: Pico port for PC messages, PC port for STATUS (section 11).
PICO_PORT = 4212
STATUS_PORT = 4213

#: Coordinator states (layout_table.md section 5.6).
COORD_STATES = ("BOOTING", "CONNECTING TO ROUTER", "WAITING FOR SESSION",
                "VALIDATING SESSION", "WAITING FOR NODES",
                "WAITING FOR TILE READINESS", "SYNCHRONISING", "READY",
                "COORDINATING PLAYBACK", "PAUSED", "RECOVERING", "FAULT",
                "COORDINATING GAME")

#: Failure policies (section 5.3).
POLICY_CONTINUE = 0
POLICY_PAUSE = 1

#: GAME_CONTROL command codes (layout_table.md section 5.10): the mode
#: switch, then a round's start and its end (the PC's abort).
MODE_VIDEO = 0
MODE_GAME = 1
GAME_START = 2
GAME_END = 3

#: A GAME_CONTROL is sent again until the Pico's ACK: this many tries, each
#: waiting this long for the ACK (plan choice; the Pico ACKs within a pass).
GAME_TRIES = 5
GAME_ACK_WAIT_S = 0.2

#: Most ACKed sequence numbers kept while nobody asks for them.
ACK_KEEP = 32

#: Most TAP board numbers kept while nobody asks for them.
TAP_KEEP = 16


@dataclass
class Status:
    """One decoded STATUS."""

    readiness: int
    online: int
    state: int
    received: float
    score: int = 0

    def state_name(self) -> str:
        """Coordinator state name."""
        if 0 <= self.state < len(COORD_STATES):
            return COORD_STATES[self.state]
        return "UNKNOWN %d" % self.state


def required_mask(node_map: Sequence[int]) -> int:
    """Bit n - 1 for every Node ID n in the map (VC-11b)."""
    mask = 0
    for node in node_map:
        if node:
            mask |= 1 << (node - 1)
    return mask


def nodes_in(mask: int) -> List[int]:
    """Node IDs whose bit is set."""
    return [n for n in range(1, pl.NODE_MAX + 1) if mask & (1 << (n - 1))]


class PicoLink:
    """Commands to one Pico and the STATUS it sends back."""

    def __init__(self, pico_ip: str, port: int = STATUS_PORT,
                 sock: Optional[socket.socket] = None) -> None:
        self.pico = (pico_ip, PICO_PORT)
        if sock is None:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            sock.bind(("", port))
        self.sock = sock
        self.sock.setblocking(False)
        self.seq = 0
        self.session_id = 0
        self.status: Optional[Status] = None
        #: Board numbers of TAP datagrams not yet taken (build step 4).
        self.taps: List[int] = []
        #: Seq numbers of the Pico's ACKs (node 0) not yet taken.
        self.acks: List[int] = []

    def _send(self, data: bytes) -> None:
        self.sock.sendto(data, self.pico)

    def _next_seq(self) -> int:
        self.seq = (self.seq + 1) & 0xFFFF
        return self.seq

    def send_session_config(self, session_id: int, rows: int, cols: int,
                            policy: int, node_map: Sequence[int],
                            frame_count: int,
                            crcs: Sequence[int]) -> bytes:
        """SESSION_CONFIG with the wall shape, the map (row-major over
        rows x cols), frame_count and one CRC32 per map position (0 for
        blank); returns the bytes sent."""
        self.session_id = session_id
        data = pl.encode_session_config(
            session_id=session_id, seq=self._next_seq(), rows=rows, cols=cols,
            policy=policy, node_map=list(node_map), frame_count=frame_count,
            crc32=list(crcs))
        self._send(data)
        return data

    def _command(self, name: str) -> bytes:
        fields = {"session_id": self.session_id, "seq": self._next_seq()}
        if name == "START_AT":
            fields["t_start"] = 0
        elif name == "RESUME_AT":
            fields["t_resume"] = 0
            fields["frame_index"] = 0
        data = pl.encode(name, fields)
        self._send(data)
        return data

    def send_mode(self, command: int) -> bytes:
        """GAME_CONTROL to the Pico (node_id 0), sent once: MODE_VIDEO or
        MODE_GAME. Use send_game_control() to wait for the ACK."""
        data = pl.encode("GAME_CONTROL", {
            "session_id": self.session_id, "seq": self._next_seq(),
            "node_id": 0, "command": command})
        self._send(data)
        return data

    def send_game_control(self, command: int) -> bool:
        """GAME_CONTROL to the Pico, resent with the same seq until the Pico
        ACKs it (GAME_TRIES tries of GAME_ACK_WAIT_S each). Returns True when
        acknowledged; a repeat with the same seq is ACKed again by the Pico
        and changes nothing."""
        seq = self._next_seq()
        data = pl.encode("GAME_CONTROL", {
            "session_id": self.session_id, "seq": seq,
            "node_id": 0, "command": command})
        for _ in range(GAME_TRIES):
            self._send(data)
            if self._wait_ack(seq, GAME_ACK_WAIT_S):
                return True
        return False

    def _wait_ack(self, seq: int, wait_s: float) -> bool:
        """Read datagrams for up to wait_s until the ACK of seq arrives."""
        deadline = time.monotonic() + wait_s
        while True:
            self.poll_status()
            if seq in self.acks:
                self.acks.remove(seq)
                return True
            left = deadline - time.monotonic()
            if left <= 0:
                return False
            select.select([self.sock], [], [], left)

    def start(self) -> bytes:
        """Start: the Pico picks t_start (T-B2-05)."""
        return self._command("START_AT")

    def pause(self) -> bytes:
        """Pause."""
        return self._command("PAUSE")

    def resume(self) -> bytes:
        """Resume: the Pico picks t_resume and the frame."""
        return self._command("RESUME_AT")

    def stop(self) -> bytes:
        """Stop."""
        return self._command("STOP")

    def poll_status(self) -> Optional[Status]:
        """Read every waiting datagram: STATUS updates the status, TAP is
        queued for ``next_tap``. Returns the newest STATUS (or the last)."""
        while True:
            try:
                data, _addr = self.sock.recvfrom(64)
            except (BlockingIOError, socket.timeout):
                return self.status
            try:
                msg = pl.decode(data)
            except pl.LayoutError:
                continue
            if msg["type"] == "TAP":
                self.taps.append(int(msg["node_id"]))  # type: ignore
                del self.taps[:-TAP_KEEP]
                continue
            if msg["type"] == "ACK":
                if int(msg["node_id"]) == 0:  # type: ignore
                    self.acks.append(int(msg["acked_seq"]))  # type: ignore
                    del self.acks[:-ACK_KEEP]
                continue
            if msg["type"] != "STATUS":
                continue
            self.status = Status(int(msg["readiness"]),  # type: ignore
                                 int(msg["online"]),  # type: ignore
                                 int(msg["state"]),  # type: ignore
                                 time.monotonic(),
                                 int(msg["score"]))  # type: ignore

    def next_tap(self, timeout_s: float) -> Optional[int]:
        """Wait up to ``timeout_s`` for the next TAP; returns its board
        number (the tapped tile), or None when no tap came in time."""
        deadline = time.monotonic() + timeout_s
        while True:
            self.poll_status()
            if self.taps:
                return self.taps.pop(0)
            left = deadline - time.monotonic()
            if left <= 0:
                return None
            select.select([self.sock], [], [], left)

    def close(self) -> None:
        """Release the port."""
        self.sock.close()
