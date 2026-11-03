"""pytest: pc/tests/upload_faults.py (T-X-08) helpers.

Owner: Cheston (Buddy 3). Implemented by Ahsanul (B4) for Cheston. The cases
themselves need a tile (bench checklist Part G); these tests check the
pieces that do not.
"""

from __future__ import annotations

import os
import sys
import zlib

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import proto_layout as pl  # noqa: E402
import upload_faults as uf  # noqa: E402


def test_corrupted_changes_one_byte_and_the_crc() -> None:
    payload = bytes(57600)
    bad = uf.corrupted(payload)
    assert sum(1 for a, b in zip(payload, bad) if a != b) == 1
    assert zlib.crc32(bad) != zlib.crc32(payload)


def test_nack_reasons() -> None:
    replies = [pl.decode(pl.encode_nack(session_id=1, seq=1, node_id=4,
                                        reason=0, ref_seq=9)),
               pl.decode(pl.encode_chunk_ack(session_id=1, seq=2, node_id=4,
                                             chunk_index=3))]
    assert uf.nack_reasons(replies) == [0]


def test_vc5_payload_is_115200_bytes() -> None:
    with open(os.path.join(uf.VECTORS, "payload_115200.bin"), "rb") as fh:
        assert len(fh.read()) == 115200
