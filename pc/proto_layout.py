"""Python twin of the SE15 wire layout (T-B1-01).

Owner: Jolyn (Buddy 1). Implemented by Ahsanul (B4) for Jolyn, task T-B1-01.

Every field, offset and width comes from ``protocol/layout_table.md`` (T-B2-01,
owner Benjamin), the same table ``common/proto/proto.c`` follows. A message
is one little-endian integer: each field value is shifted to its bit offset
and the sum is written with ``int.to_bytes(n, "little")``. Bit 0 is the least
significant bit of byte 0. No ``ctypes`` or ``struct`` layout is used for a
wire message.

``pc/tests/test_layout_table.py`` checks ``LAYOUT`` against the tables in
``layout_table.md`` section 3; ``pc/tests/test_cross_lang.py`` checks every
vector in ``protocol/vectors/`` against the C codec byte for byte.

PROVISIONAL (OD-07, owner Benjamin): every width the table marks as a plan
choice. PROVISIONAL (OD-05, owners Jolyn and Jia Jun): nibble order (a) in
``pack_frame()`` and ``unpack_frame()``.
"""

from __future__ import annotations

from typing import Dict, List, Mapping, Tuple

#: Width of the header fields, bits (layout_table.md section 2).
TYPE_BITS = 5
SESSION_BITS = 8
SEQ_BITS = 16
HEADER_BITS = TYPE_BITS + SESSION_BITS + SEQ_BITS

#: Ranges and sizes shared with proto.h.
NODE_MAX = 9
MAP_SLOTS = 9
FRAME_MAX = 1023
FRAME_RATE = 60
FRAME_BYTES = 96
FRAME_NIBBLES = 192
NIBBLE_MAX = 8
CHUNK_PAYLOAD_MAX = 1024
CHUNK_INDEX_MAX = 112
CHUNK_HEAD_BYTES = 5

#: One field: (name, width in bits, smallest valid value, largest valid
#: value). Order is the order on the wire.
Field = Tuple[str, int, int, int]

_U16 = (1 << 16) - 1
_U32 = (1 << 32) - 1
_U64 = (1 << 64) - 1
_NODE: Field = ("node_id", 4, 1, NODE_MAX)
_CRC_MAX = _U32

#: Message rows: name -> (type code, body fields after the header).
#: Copied from layout_table.md section 3, in T-B2-01 row order.
LAYOUT: Dict[str, Tuple[int, List[Field]]] = {
    "REGISTER": (0, [_NODE, ("cached_session_id", 8, 0, 255),
                     ("cached_crc32", 32, 0, _CRC_MAX)]),
    "HEARTBEAT": (1, [_NODE]),
    "SESSION_CONFIG": (2, [("layout", 2, 0, 2), ("policy", 1, 0, 1)]
                       + [("node_map[%d]" % slot, 4, 0, NODE_MAX)
                          for slot in range(MAP_SLOTS)]
                       + [("frame_count", 10, 1, FRAME_MAX)]
                       + [("crc32[%d]" % slot, 32, 0, _CRC_MAX)
                          for slot in range(MAP_SLOTS)]),
    "MANIFEST": (3, [("format_version", 2, 0, 3),
                     ("frame_count", 10, 1, FRAME_MAX),
                     ("crc32", 32, 0, _CRC_MAX)]),
    "ASSET_CHUNK": (4, [("chunk_index", 7, 0, CHUNK_INDEX_MAX)]),
    "CHUNK_ACK": (5, [_NODE, ("chunk_index", 7, 0, CHUNK_INDEX_MAX)]),
    "NACK": (6, [_NODE, ("reason", 2, 0, 3), ("ref_seq", 16, 0, _U16)]),
    "BUSY": (7, [_NODE, ("ref_seq", 16, 0, _U16)]),
    "ASSET_READY": (8, [_NODE, ("crc32", 32, 0, _CRC_MAX)]),
    "SYNC_READY": (9, [_NODE]),
    "BEACON": (10, [("t_p", 64, 0, _U64)]),
    "START_AT": (11, [("t_start", 64, 0, _U64)]),
    "RESUME_AT": (12, [("t_resume", 64, 0, _U64),
                       ("frame_index", 10, 0, FRAME_MAX)]),
    "PAUSE": (13, []),
    "STOP": (14, []),
    "STATUS": (15, [("readiness", 9, 0, 0x1FF), ("online", 9, 0, 0x1FF),
                    ("state", 4, 0, 11)]),
    "TELEMETRY": (16, [_NODE, ("fault_word", 32, 0, _U32),
                       ("orientation", 2, 0, 3),
                       ("display_state", 3, 0, 6)]),
    "TELEMETRY_ACK": (17, [("acked_seq", 16, 0, _U16)]),
}

#: Type code -> message name.
NAMES: Dict[int, str] = {code: name for name, (code, _) in LAYOUT.items()}


class LayoutError(ValueError):
    """A field is out of range, or the bytes are not a valid message."""


def array_fields(name: str) -> List[str]:
    """Names of the array fields of a row (``node_map``, ``crc32``), in
    wire order; their elements are the fields ``name[i]``."""
    names: List[str] = []
    for field, _, _, _ in LAYOUT[name][1]:
        if "[" in field:
            base = field.split("[", 1)[0]
            if base not in names:
                names.append(base)
    return names


def body_bits(name: str) -> int:
    """Bits of a message without padding (ASSET_CHUNK: without payload)."""
    return HEADER_BITS + sum(width for _, width, _, _ in LAYOUT[name][1])


def encoded_length(name: str, payload_len: int = 0) -> int:
    """Bytes on the wire (layout_table.md section 4)."""
    if name == "ASSET_CHUNK":
        return CHUNK_HEAD_BYTES + payload_len
    return (body_bits(name) + 7) // 8


def offsets(name: str) -> List[Tuple[str, int, int]]:
    """(field, bit offset, width) for each body field, in wire order."""
    result = []
    bit = HEADER_BITS
    for field, width, _, _ in LAYOUT[name][1]:
        result.append((field, bit, width))
        bit += width
    return result


def _check(field: str, value: int, low: int, high: int) -> None:
    if not isinstance(value, int) or not low <= value <= high:
        raise LayoutError("%s=%r outside %d to %d" % (field, value, low, high))


def encode(name: str, fields: Mapping[str, object]) -> bytes:
    """Encode one message from a dict of its fields.

    ``fields`` holds ``session_id``, ``seq`` and every body field of the row;
    an array field (``node_map``, ``crc32`` of SESSION_CONFIG) may be given
    as a list of 9 entries; ASSET_CHUNK also needs ``payload`` (bytes, 1 to
    1,024).
    """
    if name not in LAYOUT:
        raise LayoutError("unknown message %r" % name)
    code, body = LAYOUT[name]
    flat = dict(fields)
    for array in array_fields(name):
        if isinstance(flat.get(array), (list, tuple)):
            items = list(flat.pop(array))  # type: ignore[call-overload]
            for slot, item in enumerate(items):
                flat["%s[%d]" % (array, slot)] = item
    header = [("session_id", SESSION_BITS, 0, (1 << SESSION_BITS) - 1),
              ("seq", SEQ_BITS, 0, _U16)]
    value = code
    bit = TYPE_BITS
    for field, width, low, high in header + body:
        item = flat.get(field)
        _check(field, item, low, high)  # type: ignore[arg-type]
        value |= int(item) << bit  # type: ignore[call-overload]
        bit += width
    if name == "ASSET_CHUNK":
        payload = flat.get("payload")
        if not isinstance(payload, (bytes, bytearray)) \
                or not 1 <= len(payload) <= CHUNK_PAYLOAD_MAX:
            raise LayoutError("payload must be 1 to 1,024 bytes")
        return value.to_bytes(CHUNK_HEAD_BYTES, "little") + bytes(payload)
    return value.to_bytes(encoded_length(name), "little")


def decode(data: bytes) -> Dict[str, object]:
    """Decode any message; returns ``type`` (name), the header and fields.

    An array field is returned as a list of 9 entries and an ASSET_CHUNK
    ``payload`` as bytes. Raises LayoutError on a wrong length, an unknown
    type or a field outside its range.
    """
    if len(data) < 4:
        raise LayoutError("shorter than a header")
    head = int.from_bytes(data[:4], "little")
    code = head & ((1 << TYPE_BITS) - 1)
    if code not in NAMES:
        raise LayoutError("unknown type code %d" % code)
    name = NAMES[code]
    if name == "ASSET_CHUNK":
        if not CHUNK_HEAD_BYTES < len(data) <= CHUNK_HEAD_BYTES \
                + CHUNK_PAYLOAD_MAX:
            raise LayoutError("ASSET_CHUNK length %d" % len(data))
        value = int.from_bytes(data[:CHUNK_HEAD_BYTES], "little")
    else:
        if len(data) != encoded_length(name):
            raise LayoutError("%s length %d, expected %d"
                              % (name, len(data), encoded_length(name)))
        value = int.from_bytes(data, "little")
    result: Dict[str, object] = {
        "type": name,
        "session_id": (value >> TYPE_BITS) & ((1 << SESSION_BITS) - 1),
        "seq": (value >> (TYPE_BITS + SESSION_BITS)) & _U16,
    }
    arrays: Dict[str, List[int]] = {}
    for (field, bit, width), (_, _, low, high) in zip(offsets(name),
                                                      LAYOUT[name][1]):
        item = (value >> bit) & ((1 << width) - 1)
        _check(field, item, low, high)
        if "[" in field:
            arrays.setdefault(field.split("[", 1)[0], []).append(item)
        else:
            result[field] = item
    result.update(arrays)
    if name == "ASSET_CHUNK":
        result["payload"] = bytes(data[CHUNK_HEAD_BYTES:])
    return result


def _named_encoder(name: str):  # type: ignore[no-untyped-def]
    def encoder(**fields: object) -> bytes:
        return encode(name, fields)
    encoder.__name__ = "encode_" + name.lower()
    encoder.__doc__ = "Encode %s (layout_table.md section 3)." % name
    return encoder


def _named_decoder(name: str):  # type: ignore[no-untyped-def]
    def decoder(data: bytes) -> Dict[str, object]:
        result = decode(data)
        if result["type"] != name:
            raise LayoutError("not a %s: %s" % (name, result["type"]))
        return result
    decoder.__name__ = "decode_" + name.lower()
    decoder.__doc__ = "Decode %s; LayoutError for any other type." % name
    return decoder


# One encode_<msg>() / decode_<msg>() pair per row, as the plan names them.
encode_register = _named_encoder("REGISTER")
decode_register = _named_decoder("REGISTER")
encode_heartbeat = _named_encoder("HEARTBEAT")
decode_heartbeat = _named_decoder("HEARTBEAT")
encode_session_config = _named_encoder("SESSION_CONFIG")
decode_session_config = _named_decoder("SESSION_CONFIG")
encode_manifest = _named_encoder("MANIFEST")
decode_manifest = _named_decoder("MANIFEST")
encode_asset_chunk = _named_encoder("ASSET_CHUNK")
decode_asset_chunk = _named_decoder("ASSET_CHUNK")
encode_chunk_ack = _named_encoder("CHUNK_ACK")
decode_chunk_ack = _named_decoder("CHUNK_ACK")
encode_nack = _named_encoder("NACK")
decode_nack = _named_decoder("NACK")
encode_busy = _named_encoder("BUSY")
decode_busy = _named_decoder("BUSY")
encode_asset_ready = _named_encoder("ASSET_READY")
decode_asset_ready = _named_decoder("ASSET_READY")
encode_sync_ready = _named_encoder("SYNC_READY")
decode_sync_ready = _named_decoder("SYNC_READY")
encode_beacon = _named_encoder("BEACON")
decode_beacon = _named_decoder("BEACON")
encode_start_at = _named_encoder("START_AT")
decode_start_at = _named_decoder("START_AT")
encode_resume_at = _named_encoder("RESUME_AT")
decode_resume_at = _named_decoder("RESUME_AT")
encode_pause = _named_encoder("PAUSE")
decode_pause = _named_decoder("PAUSE")
encode_stop = _named_encoder("STOP")
decode_stop = _named_decoder("STOP")
encode_status = _named_encoder("STATUS")
decode_status = _named_decoder("STATUS")
encode_telemetry = _named_encoder("TELEMETRY")
decode_telemetry = _named_decoder("TELEMETRY")
encode_telemetry_ack = _named_encoder("TELEMETRY_ACK")
decode_telemetry_ack = _named_decoder("TELEMETRY_ACK")


def pack_frame(values: List[int]) -> bytes:
    """Pack 192 channel values (0 to 8, R0 G0 B0 R1 ...) into 96 bytes.

    Nibble k is the 4-bit field at bit 4k (OD-05 option (a), PROVISIONAL).
    """
    if len(values) != FRAME_NIBBLES:
        raise LayoutError("a frame has 192 channel values")
    for value in values:
        _check("channel", value, 0, NIBBLE_MAX)
    packed = sum(value << (4 * k) for k, value in enumerate(values))
    return packed.to_bytes(FRAME_BYTES, "little")


def unpack_frame(frame: bytes) -> List[int]:
    """Inverse of pack_frame(): 96 bytes to 192 nibbles (no range check)."""
    if len(frame) != FRAME_BYTES:
        raise LayoutError("a frame is 96 bytes")
    packed = int.from_bytes(frame, "little")
    return [(packed >> (4 * k)) & 0xF for k in range(FRAME_NIBBLES)]
