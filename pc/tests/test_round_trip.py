"""pytest: pc/tests/round_trip.py (T-X-01) with a simulated tile dump.

Owner: Jolyn (Buddy 1). Implemented by Ahsanul (B4) for Jolyn. The tile is
simulated with the same unpack rule as display_frame.c (nibble k into the
G, R, B wire order), so the PC side of the check is proven before the
bench run.
"""

from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import proto_layout as pl  # noqa: E402
import round_trip as rt  # noqa: E402


def _tile_dump(payload: bytes, frame: int) -> str:
    values = pl.unpack_frame(payload[frame * 96:(frame + 1) * 96])
    wire = bytearray()
    for led in range(64):
        red, green, blue = values[3 * led:3 * led + 3]
        wire += bytes((green, red, blue))
    return "DE_DUMP,%d,%s" % (frame, wire.hex())


def test_every_value_0_to_255_present() -> None:
    seen = set()
    for frame in range(2):
        for pixel in rt.pattern_frame(frame):
            seen.update(pixel)
    assert seen == set(range(256))


def test_simulated_round_trip_600_of_600() -> None:
    asset = rt.build_asset()
    assert len(asset.payload) == 57600
    lines = [_tile_dump(asset.payload, f) for f in range(600)]
    passed, problems = rt.check(rt.parse_dump(["noise"] + lines))
    assert (passed, problems) == (600, [])


def test_double_scaling_is_caught() -> None:
    asset = rt.build_asset()
    lines = [_tile_dump(asset.payload, f) for f in range(600)]
    bad = bytearray.fromhex(lines[5].split(",", 2)[2])
    bad[0] = 9
    lines[5] = "DE_DUMP,5,%s" % bad.hex()
    passed, problems = rt.check(rt.parse_dump(lines))
    assert passed == 599 and "frame 5" in problems[0]
