# SE15 wire layout table (T-B2-01)

Owner: Benjamin (Buddy 2). Implemented by Ahsanul (B4) for Benjamin, task T-B2-01, 29 Oct 2026. Field owners: Jolyn (asset, manifest, dashboard rows), Cheston (tile comms rows), Ahsanul (timing rows), Jia Jun (display and orientation fields).

This file is the single source of every wire field (plan section 0.1). `common/proto/proto.c/.h` (C, tiles and Pico) and `pc/proto_layout.py` (Python, PC) follow it; `pc/tests/test_layout_table.py` checks the Python field list against the tables in section 3, and the cross-language vectors in `protocol/vectors/` (T-B1-01) check C against Python byte for byte.

**Status: PROVISIONAL, not frozen.** Benjamin freezes the table (OD-07). Every width that PostTri does not fix comes from the OD-07 draft in `Week10_Kickoff/decision_briefs.md` and is marked **(plan choice, valid-range rule)**; section 10 lists everything Benjamin must confirm.

## 1. Conventions

| Rule | Value | Source |
|---|---|---|
| Bit numbering | Bit 0 is the least significant bit of byte 0; bit 8k + j is bit j of byte k. A field of width w at offset o occupies bits o to o + w - 1, its least significant bit at o. | plan choice (OD-07, owner Benjamin); it fixes the byte order VB-2a and LDC-4 require ("little-endian") and makes OD-05 option (a) the matching nibble order |
| Byte order | Every multi-byte field is little-endian (a consequence of the bit numbering). | VB-2a, LDC-4 |
| Packing | Explicit shifts and masks only (`proto_put_bits()`, `proto_get_bits()`; Python `int.to_bytes(..., "little")`). No wire struct is passed to `sendto`, `udp_sendto` or `recvfrom`; no `__attribute__((packed))`; no bit-field in any wire type. | VB-2a, VB-2b, VC-9 |
| Layout | Fields follow one another with no gap, starting at bit 0; the message is padded with zero bits to a whole byte. Exception: the ASSET_CHUNK payload starts on the next byte boundary. | plan choice (OD-07) |
| Length | A decoder accepts a datagram only when its length equals the row's length (ASSET_CHUNK: header bytes plus 1 to 1,024 payload bytes). | plan choice |
| Valid range | An encoder refuses, and a decoder rejects, any field outside the valid range in its row. The receiver, not the codec, checks session_id and seq against its state. | IFM-2a |
| Transport | UDP; port numbers are set by `coordinator_loop` and `tile_net`, not here. | LDC-4 |

Width rule for every field PostTri does not size: bits = ceil(log2(number of valid values)) (CR1 change requirement "only the number of bits required to represent its valid range"; Round 0 ruling SC-10).

## 2. Common header (every message)

| Field | Bit offset | Bits | Valid range | Meaning | Source |
|---|---|---|---|---|---|
| type | 0 | 5 | 0 to 17 | message type code (section 5.1) | IFM-2a; width plan choice (18 types) |
| session_id | 5 | 8 | 0 to 255 | session the message belongs to | IFM-2a, W6D-2f; width plan choice |
| seq | 13 | 16 | 0 to 65,535 | sender's sequence number, wrap-aware | IFM-2a, W6D-2f; width plan choice |

Header: 29 bits; a receiver needs 4 bytes to read it (`proto_decode_header()`).

seq is compared wrap-aware: s is newer than last when (uint16_t)(s - last) is 1 to 32,767. At 10 Hz beacons a 16-bit counter wraps after 65,536 / 10 = 6,553.6 s, 109.2 min **(derived)**. Which counter (per sender or per sender and type) is OD-07 open item 2.

## 3. Message rows

Offsets are absolute bit offsets in the message. Direction: T = tile, P = Pico, C = PC.

### REGISTER (type 0, T to P)

| Field | Bit offset | Bits | Valid range | Meaning | Source |
|---|---|---|---|---|---|
| node_id | 29 | 4 | 1 to 9 | permanent Node ID of the tile | T-B2-01, FR9 |
| cached_session_id | 33 | 8 | 0 to 255 | session of the asset in the tile's flash | T-B2-01, T-B3-05 |
| cached_crc32 | 41 | 32 | 0 to 2^32 - 1 | CRC32 of that asset | T-B2-01, T-B3-05 |

### HEARTBEAT (type 1, T to P)

| Field | Bit offset | Bits | Valid range | Meaning | Source |
|---|---|---|---|---|---|
| node_id | 29 | 4 | 1 to 9 | sender | T-B3-03; sender field plan choice |

### SESSION_CONFIG (type 2, C to P)

| Field | Bit offset | Bits | Valid range | Meaning | Source |
|---|---|---|---|---|---|
| layout | 29 | 2 | 0 to 2 | 1x1, 2x2, 3x3 (section 5.2) | T-B2-01, FR3 |
| policy | 31 | 1 | 0 to 1 | continue, pause (section 5.3) | T-B2-01, FR18 |
| node_map[0] | 32 | 4 | 0 to 9 | Node ID at row 0, column 0; 0 = blank | T-B2-01, OD-21 |
| node_map[1] | 36 | 4 | 0 to 9 | row 0, column 1 | as above |
| node_map[2] | 40 | 4 | 0 to 9 | row 0, column 2 | as above |
| node_map[3] | 44 | 4 | 0 to 9 | row 1, column 0 | as above |
| node_map[4] | 48 | 4 | 0 to 9 | row 1, column 1 | as above |
| node_map[5] | 52 | 4 | 0 to 9 | row 1, column 2 | as above |
| node_map[6] | 56 | 4 | 0 to 9 | row 2, column 0 | as above |
| node_map[7] | 60 | 4 | 0 to 9 | row 2, column 1 | as above |
| node_map[8] | 64 | 4 | 0 to 9 | row 2, column 2 | as above |
| frame_count | 68 | 10 | 1 to 1023 | frames in every tile asset of the session (all tiles share one duration) | T-B1-03, VC-3; field PROVISIONAL (section 10 item 9) |
| crc32[0] | 78 | 32 | 0 to 2^32 - 1 | manifest CRC32 of the tile at map position 0; 0 for a blank position | W6D-7a, T-B2-04; field PROVISIONAL (section 10 item 9) |
| crc32[1] | 110 | 32 | 0 to 2^32 - 1 | manifest CRC32 of the tile at map position 1; 0 for a blank position | W6D-7a, T-B2-04; field PROVISIONAL (section 10 item 9) |
| crc32[2] | 142 | 32 | 0 to 2^32 - 1 | manifest CRC32 of the tile at map position 2; 0 for a blank position | W6D-7a, T-B2-04; field PROVISIONAL (section 10 item 9) |
| crc32[3] | 174 | 32 | 0 to 2^32 - 1 | manifest CRC32 of the tile at map position 3; 0 for a blank position | W6D-7a, T-B2-04; field PROVISIONAL (section 10 item 9) |
| crc32[4] | 206 | 32 | 0 to 2^32 - 1 | manifest CRC32 of the tile at map position 4; 0 for a blank position | W6D-7a, T-B2-04; field PROVISIONAL (section 10 item 9) |
| crc32[5] | 238 | 32 | 0 to 2^32 - 1 | manifest CRC32 of the tile at map position 5; 0 for a blank position | W6D-7a, T-B2-04; field PROVISIONAL (section 10 item 9) |
| crc32[6] | 270 | 32 | 0 to 2^32 - 1 | manifest CRC32 of the tile at map position 6; 0 for a blank position | W6D-7a, T-B2-04; field PROVISIONAL (section 10 item 9) |
| crc32[7] | 302 | 32 | 0 to 2^32 - 1 | manifest CRC32 of the tile at map position 7; 0 for a blank position | W6D-7a, T-B2-04; field PROVISIONAL (section 10 item 9) |
| crc32[8] | 334 | 32 | 0 to 2^32 - 1 | manifest CRC32 of the tile at map position 8; 0 for a blank position | W6D-7a, T-B2-04; field PROVISIONAL (section 10 item 9) |

The map is always 9 positions in row-major order of a 3x3 grid; a 1x1 layout uses position 0 and a 2x2 layout positions 0, 1, 3 and 4 **(plan choice)**. Unused positions are 0. `crc32[i]` belongs to the tile at `node_map[i]`. **PROVISIONAL (OD-07 item 9, owner Benjamin):** `frame_count` and `crc32[]` were added on 29 Oct 2026 so the Pico can check ASSET_READY against the manifest CRC32 of each Node ID (T-B2-04) and compute the saved frame index it sends in RESUME_AT and in a rejoin (T-B2-05, OD-14). Every tile of a session plays the same GIF duration, so one frame_count serves all of them.

### MANIFEST (type 3, C to T)

| Field | Bit offset | Bits | Valid range | Meaning | Source |
|---|---|---|---|---|---|
| format_version | 29 | 2 | 0 to 3 | asset format (section 5.4) | VC-5 |
| frame_count | 31 | 10 | 1 to 1023 | frames in the asset | VB-4b, VC-3 |
| crc32 | 41 | 32 | 0 to 2^32 - 1 | zlib CRC32 over this tile's packed payload | LDC-1, VB-1 |

The manifest's session is the header session_id (VC-5 "beside session_id and CRC32"). frame_rate is not on the wire: its only valid value is 60 (T-B1-03), which needs 0 bits by the width rule; the constant `PROTO_FRAME_RATE` (60) stands in for it on both sides **(plan choice, OD-07 open item)**. Payload length = frame_count x 96 B (VC-5).

### ASSET_CHUNK (type 4, C to T)

| Field | Bit offset | Bits | Valid range | Meaning | Source |
|---|---|---|---|---|---|
| chunk_index | 29 | 7 | 0 to 112 | chunk number | T-B2-01, LDC-8 |

Bits 36 to 39 are zero padding. The payload, 1 to 1,024 bytes, starts at byte 5 and runs to the end of the datagram, so its length is the datagram length minus 5. A 600-frame asset is 57 chunks, the last of 256 B = 57,600 - 56 x 1,024 **(derived)**; a 1023-frame packed asset is 1023 x 96 = 98,208 B, 96 chunks; the 600-frame 8-bit asset of the `DE_PROFILE` build (OD-15) is 115,200 B, 113 chunks, so the largest index is 112 **(derived)**. The same range lets the PC send the whole 115,200 B VC-5 test payload, which the tile NACKs by length (T-X-08 case 5).

### CHUNK_ACK (type 5, T to C)

| Field | Bit offset | Bits | Valid range | Meaning | Source |
|---|---|---|---|---|---|
| node_id | 29 | 4 | 1 to 9 | sender | sender field plan choice |
| chunk_index | 33 | 7 | 0 to 112 | chunk acknowledged | T-B3-05 "each chunk is ACKed by index" |

### NACK (type 6, T to C or P)

| Field | Bit offset | Bits | Valid range | Meaning | Source |
|---|---|---|---|---|---|
| node_id | 29 | 4 | 1 to 9 | sender | sender field plan choice |
| reason | 33 | 2 | 0 to 3 | reason code (section 5.5) | T-B2-01, VC-5, VA-3.6 |
| ref_seq | 35 | 16 | 0 to 65,535 | header seq of the rejected message | plan choice (the NACK names what it refuses) |

### BUSY (type 7, T to C)

| Field | Bit offset | Bits | Valid range | Meaning | Source |
|---|---|---|---|---|---|
| node_id | 29 | 4 | 1 to 9 | sender | sender field plan choice |
| ref_seq | 33 | 16 | 0 to 65,535 | header seq of the refused chunk or manifest | plan choice |

### ASSET_READY (type 8, T to P)

| Field | Bit offset | Bits | Valid range | Meaning | Source |
|---|---|---|---|---|---|
| node_id | 29 | 4 | 1 to 9 | sender | sender field plan choice |
| crc32 | 33 | 32 | 0 to 2^32 - 1 | CRC32 of the validated payload | T-B2-01 "ASSET_READY {session_id, CRC32}", W6D-7a |

### SYNC_READY (type 9, T to P)

| Field | Bit offset | Bits | Valid range | Meaning | Source |
|---|---|---|---|---|---|
| node_id | 29 | 4 | 1 to 9 | sender | sender field plan choice; rule OD-10 |

### BEACON (type 10, P to all tiles, broadcast)

| Field | Bit offset | Bits | Valid range | Meaning | Source |
|---|---|---|---|---|---|
| t_p | 29 | 64 | 0 to 2^64 - 1 | Pico `time_us_64()`, read immediately before `udp_sendto` | T-B2-01, T-B2-02, LDC-6 |

### START_AT (type 11, P to all tiles)

| Field | Bit offset | Bits | Valid range | Meaning | Source |
|---|---|---|---|---|---|
| t_start | 29 | 64 | 0 to 2^64 - 1 | timeline origin, Pico time, microseconds | T-B2-01, T-B2-05 |

### RESUME_AT (type 12, P to all tiles)

| Field | Bit offset | Bits | Valid range | Meaning | Source |
|---|---|---|---|---|---|
| t_resume | 29 | 64 | 0 to 2^64 - 1 | Pico time at which frame_index shows | T-B2-01, T-B2-05 |
| frame_index | 93 | 10 | 0 to 1023 | saved frame index | VB-4b, VC-3 |

### PAUSE (type 13, P to all tiles)

| Field | Bit offset | Bits | Valid range | Meaning | Source |
|---|---|---|---|---|---|

Header only. The tile saves its own frame index (T-B4-04).

### STOP (type 14, P to all tiles)

| Field | Bit offset | Bits | Valid range | Meaning | Source |
|---|---|---|---|---|---|

Header only. Accepted in PLAYING and PAUSED (W6D-2e).

### STATUS (type 15, P to C)

| Field | Bit offset | Bits | Valid range | Meaning | Source |
|---|---|---|---|---|---|
| readiness | 29 | 9 | 0 to 0x1FF | bit n - 1 = Node ID n ready | T-B2-04, VC-11b |
| online | 38 | 9 | 0 to 0x1FF | bit n - 1 = Node ID n online | T-B2-04 |
| state | 47 | 4 | 0 to 11 | coordinator state (section 5.6) | T-B2-04, W6 statechart 2 |

### TELEMETRY (type 16, T to C)

| Field | Bit offset | Bits | Valid range | Meaning | Source |
|---|---|---|---|---|---|
| node_id | 29 | 4 | 1 to 9 | sender | sender field plan choice |
| fault_word | 33 | 32 | 0 to 2^32 - 1 | fault word snapshot | LDC-2, VC-4 |
| orientation | 65 | 2 | 0 to 3 | 0, 90, 180, 270 degrees (section 5.7) | VB-5c |
| display_state | 67 | 3 | 0 to 6 | display state (section 5.8) | VB-5c, W6 statechart 5 |

The telemetry seq of VC-4 is the header seq.

### TELEMETRY_ACK (type 17, C to T)

| Field | Bit offset | Bits | Valid range | Meaning | Source |
|---|---|---|---|---|---|
| acked_seq | 29 | 16 | 0 to 65,535 | header seq of the TELEMETRY acknowledged | VC-4; separate from the PC's own header seq (`before_after.md` decision 2) |

## 4. Encoded lengths, before and after

"Before" is copied from `docs/week13/before_after.md` (T-B4-01) with its label; T-X-04 measures the on-air "after" values.

| Message | Bits | Bytes after | Bytes before | Before label |
|---|---|---|---|---|
| REGISTER | 73 | 10 | 21 | derived, W6 8-bit design |
| HEARTBEAT | 33 | 5 | 9 (`sizeof(hello_t)`, the tag's heartbeat) | measured (Xtensa `sizeof`); on-air pending B-3 |
| SESSION_CONFIG | 366 | 46 | 87 | derived |
| MANIFEST | 73 | 10 | 22 | derived |
| ASSET_CHUNK | 40 + payload | 5 + 1 to 1,024 (1,029 full) | 13 + 1,024 = 1,037 | derived |
| CHUNK_ACK | 40 | 5 | 13 | derived |
| NACK | 51 | 7 | 10 | derived |
| BUSY | 49 | 7 | 9 | derived |
| ASSET_READY | 65 | 9 | 13 | derived |
| SYNC_READY | 33 | 5 | 9 | derived |
| BEACON | 93 | 12 | 50 (`sizeof(beacon_t)`) | measured (Xtensa `sizeof`); on-air pending B-2 |
| START_AT | 93 | 12 | 17 | derived |
| RESUME_AT | 103 | 13 | 21 | derived |
| PAUSE | 29 | 4 | 9 | derived |
| STOP | 29 | 4 | 9 | derived |
| STATUS | 51 | 7 | 28 | derived |
| TELEMETRY | 70 | 9 | 15 | derived |
| TELEMETRY_ACK | 45 | 6 | 13 | derived |

The "after" sizes include the node_id, ref_seq and acked_seq fields that the before rows do not count (section 10, items 4 and 5).

## 5. Code tables

### 5.1 Message type (5 bits, 18 codes, 14 spare)

| Code | Type | Code | Type | Code | Type |
|---|---|---|---|---|---|
| 0 | REGISTER | 6 | NACK | 12 | RESUME_AT |
| 1 | HEARTBEAT | 7 | BUSY | 13 | PAUSE |
| 2 | SESSION_CONFIG | 8 | ASSET_READY | 14 | STOP |
| 3 | MANIFEST | 9 | SYNC_READY | 15 | STATUS |
| 4 | ASSET_CHUNK | 10 | BEACON | 16 | TELEMETRY |
| 5 | CHUNK_ACK | 11 | START_AT | 17 | TELEMETRY_ACK |

Codes follow the order of the T-B2-01 message rows **(plan choice)**.

### 5.2 Layout (2 bits)

0 = 1x1, 1 = 2x2, 2 = 3x3. Code 3 is free for a 1x2 strip if OD-21 widens FR3.

### 5.3 Failure policy (1 bit)

0 = continue, 1 = pause (FR18, T-B2-04).

### 5.4 format_version (2 bits)

0 = unknown-version test value (no firmware knows it, used by the VC-5 vector), 1 = 8-bit format (192 B frames, accepted only in the `DE_PROFILE` build, OD-15), 2 = packed 4-bit format (96 B frames, shipped), 3 = reserved for a 5-bit format (VC-12, deferred) **(plan choice, OD-07 brief)**.

### 5.5 NACK reason (2 bits)

0 = unknown format_version (VC-5), 1 = wrong payload length (VC-5), 2 = Command Queue full (VA-3.6, T-B3-03), 3 = Upload Queue full (VA-3.6, T-B3-05) **(plan choice, OD-07 brief)**.

### 5.6 Coordinator state (4 bits, W6 statechart 2, 12 states)

0 BOOTING, 1 CONNECTING TO ROUTER, 2 WAITING FOR SESSION, 3 VALIDATING SESSION, 4 WAITING FOR NODES, 5 WAITING FOR TILE READINESS, 6 SYNCHRONISING, 7 READY, 8 COORDINATING PLAYBACK, 9 PAUSED, 10 RECOVERING, 11 FAULT.

### 5.7 Orientation (2 bits)

0 = 0 degrees, 1 = 90, 2 = 180, 3 = 270 (IFM-6a).

### 5.8 Display state (3 bits, W6 statechart 5 as T-B5-03 lists it, 7 states)

0 INITIALISING DISPLAY, 1 DISPLAY IDLE, 2 PREPARING FRAME, 3 TRANSMITTING PIXELS, 4 WAITING FOR NEXT TICK, 5 DISPLAY PAUSED, 6 DISPLAY FAULT.

## 6. Packed asset frame (OD-05)

**PROVISIONAL (OD-05, owners Jolyn and Jia Jun, recorded by Benjamin): option (a).** A frame is 64 pixels in LED order, each R, G, B, so 192 channel values of 0 to 8, one 4-bit nibble each. Nibble k (k = 0 to 191, in R0 G0 B0 R1 G1 B1 ... order) is the 4-bit field at bit 4k of the frame, the same bit numbering as section 1. So byte 0 holds R0 in its low nibble and G0 in its high nibble, byte 1 holds B0 and R1, and so on.

| Item | Value | Source |
|---|---|---|
| Bytes per frame | 64 x 12 bits / 8 = 96 | LDC-1, T-B1-03 |
| Frame n starts at | byte n x 96 of the payload | T-B5-02 |
| Channel value | round(v x 8 / 255) on the PC only, 0 to 8 | VB-1, VC-6 |
| CRC32 | zlib, over the packed payload | LDC-1, VB-1 |
| Payload, 600 frames | 600 x 96 = 57,600 B | LDC-1 |

Worked vector (OD-05 brief): pixel 0 = (1, 2, 3), pixel 1 = (4, 5, 6) packs to `21 43 65`. In C, nibble k is `proto_get_bits(frame, 4 * k, 4)`; in Python, `pack_frame()` and `unpack_frame()` in `pc/proto_layout.py`.

## 7. Data representation table

Every field and every shared packed word, with its valid range, bits before (the `cr1-before` type at the tag or, for fields that did not exist, the T-B4-01 typing rule) and after, and the method. "Before" labels follow `before_after.md`.

| Field | Where | Valid range | Bits before | Bits after | Method after | Justification |
|---|---|---|---|---|---|---|
| message type | header | 0 to 17 | 8 (`uint8_t` enum, rule) | 5 | shifts and masks | 18 types, ceil(log2 18) = 5 |
| session_id | header | 0 to 255 | 32 (`uint32_t` ID, rule) | 8 | shifts and masks | plan choice; CRC32 also checked, so a reused ID never reuses an asset alone |
| seq | header | 0 to 65,535, wrap-aware | 32 (`uint32_t seq`, `panel_sync.c:55` at the tag) | 16 | shifts and masks | 109.2 min wrap at 10 Hz with a wrap-aware comparison |
| Node ID | messages from tiles; map | 1 to 9 (0 = blank in the map) | 32 (`uint32_t` ID, rule; the tag's `board` was `uint8_t`) | 4 | shifts and masks | 9 nodes plus blank, ceil(log2 10) = 4 |
| t_p, t_start, t_resume | BEACON, START_AT, RESUME_AT | 0 to 2^64 - 1 us | 64 (`uint64_t` time, rule; tile times were 64-bit at the tag, `panel_sync.c:78`) | 64 | shifts and masks | see justification row J1 |
| frame_count, frame index | MANIFEST, SESSION_CONFIG, RESUME_AT, tile RAM | 0 to 1023 (frame_count 1 to 1023) | 32 (`uint32_t frame_no`, `panel_sync.c:57` at the tag) | 10 | shifts and masks | J3 |
| slew | tile RAM (`timesync`), not on the wire | -100 to +100 us per tick | none at the tag (the clock was stepped) | 8 (`int8_t`) | C type | J2 |
| CRC32 | REGISTER, SESSION_CONFIG (9 per message), MANIFEST, ASSET_READY | 0 to 2^32 - 1 | 32 | 32 | shifts and masks | zlib CRC32 is 32 bits |
| format_version | MANIFEST | 0 to 3 | 8 (`uint8_t version`, `panel_sync.c:54`) | 2 | shifts and masks | 4 codes (section 5.4) |
| chunk index | ASSET_CHUNK, CHUNK_ACK | 0 to 112 | 32 (counter, rule) | 7 | shifts and masks | 113 chunks for the 8-bit 600-frame asset, ceil(log2 113) = 7 |
| NACK reason | NACK | 0 to 3 | 8 (enum, rule) | 2 | shifts and masks | 4 reasons |
| layout | SESSION_CONFIG | 0 to 2 | 8 (enum, rule) | 2 | shifts and masks | 3 layouts |
| failure policy | SESSION_CONFIG | 0 to 1 | 8 (flag, rule) | 1 | shifts and masks | 2 policies |
| readiness bitmap | STATUS; Pico RAM | 9 bits | 72 (9 x `uint8_t` flag, rule) | 9 on the wire, one `uint16_t` in RAM | shifts and masks; single writer in poll context | VC-11b |
| online bitmap | STATUS; Pico RAM | 9 bits | 72 (rule) | 9 on the wire, one `uint16_t` in RAM | as above | as readiness |
| coordinator state | STATUS | 0 to 11 | 8 (enum, rule) | 4 | shifts and masks | 12 states, ceil(log2 12) = 4 |
| fault word | TELEMETRY; tile RAM | 32 bits | 32 (`uint32_t`, rule; no fault word at the tag) | 32 | `_Atomic uint32_t`, fetch-or and fetch-and (`fault_word`, T-B3-06) | LDC-2; bit map OD-12 |
| orientation | TELEMETRY; tile RAM | 0 to 3 | 8 (enum, rule) | 2 | shifts and masks | VB-5c |
| display state | TELEMETRY; tile RAM | 0 to 6 | 8 (enum, rule) | 3 | shifts and masks | VB-5c, 7 states |
| raw IMU sample | tile RAM | -32,768 to 32,767 per axis | none | 16 per axis | C type | VB-5c: raw readings stay 16-bit |
| packed channel | asset payload | 0 to 8 | 8 per channel (`frames.h:21` at the tag) | 4 | nibble packing (section 6) | VB-1: 9 values need 4 bits; base-9 and delta rejected (LDC-9) |
| LED back buffer | tile RAM, `display_engine` | 192 B | 192 B | 192 B | no change | LDC-7: WS2812B needs 24 bits per LED on the wire |
| asset per tile | flash and RAM | 600 frames | 115,200 B (derived, W6 8-bit design) | 57,600 B | nibble packing | LDC-1 |
| RAM copy with margin | tile RAM | | 147,968 B (derived) | 90,368 B | 57,600 B + 32,768 B margin **(proposed, RAR-4)** | RAR-4 |
| seqlock fields | tile RAM, `timesync` | offset, drift, reference | portMUX copy at the tag | relaxed 32-bit atomic halves under one sequence counter | VC-1 | VC-1: a 64-bit `_Atomic` would take a lock on Xtensa |

### 7.1 Justification rows

- **J1, 64-bit time (VB-4a, LDC-6).** A 32-bit microsecond counter wraps after 2^32 us = 4,294.97 s = 71.6 min. Keeping 64 bits costs 4 extra bytes per beacon, 4 B x 10 Hz = 40 B/s. A wrap inside a show would need wrap handling in every comparison of t_p, t_start and t_resume; 40 B/s is not worth that risk.
- **J2, `int8_t` slew (VC-2).** The slew is -100 to +100 us per tick: 201 values need 8 bits, and the sign needs the eighth bit, so the Version B figure of 7 bits was wrong. A 2 ms correction takes 2,000 / 100 = 20 ticks.
- **J3, 10-bit frame index (VB-4b, VC-3).** 10 bits hold 0 to 1023; 1023 frames at 60 fps = 17.05 s of animation. `asset_builder` rejects a GIF that needs more than 1023 frames and says why (T-B1-02); an 18 s GIF is rejected.
- **J4, state enum widths (T-B2-01).** Each state enum is ceil(log2(number of states in its W6 statechart)): coordinator 12 states = 4 bits, display 7 states = 3 bits.

## 8. What the codec does not check

The receiver checks session_id against its session and seq against the last accepted seq from that sender (T-B2-04, T-B4-04); the tile checks format_version is known, payload length = frame_count x 96 B and the CRC32 (T-B3-05). The codec checks only lengths, type codes and valid ranges.

## 9. Pico servicing mode citation (OD-01)

**PROVISIONAL (OD-01, owner Benjamin).** `ledwall_sync` links `pico_cyw43_arch_lwip_poll` (`ledwall_sync/CMakeLists.txt:10`). Citation, Raspberry Pi Pico SDK 2.3.1 (the CI version), `src/rp2_common/pico_cyw43_arch/include/pico/cyw43_arch.h`, https://github.com/raspberrypi/pico-sdk/blob/2.3.1/src/rp2_common/pico_cyw43_arch/include/pico/cyw43_arch.h, quoted in `Week10_Kickoff/decision_briefs.md`:

- line 56, poll: "This is not multi-core/IRQ safe, and requires the user to call `cyw43_arch_poll` periodically from their main loop";
- lines 98-99, threadsafe background: "lwIP callbacks happen in a (low priority) IRQ context";
- lines 237-242, `cyw43_arch_poll()`: "Perform any processing required by the `cyw43_driver` or the TCP/IP stack".

**Gap:** no SDK sentence says that lwIP callbacks run inside `cyw43_arch_poll()` in poll mode. The brief proposes logging `__get_current_exception()` in the receive callback during the T-B2-02 run (pass: 0 for every callback). Benjamin decides whether these quotes plus that log settle OD-01.

## 10. For Benjamin to confirm before the freeze (OD-07)

1. Every width marked plan choice: type 5, session_id 8, seq 16, Node ID 4, format_version 2, chunk index 7, NACK reason 2, layout 2, policy 1. (Coordinator state 4 and display state 3 are derived by the T-B2-01 rule, ceil(log2 states).)
1a. The bit numbering of section 1 (bit 0 = least significant bit of byte 0), which also fixes OD-05 option (a).
2. seq comparison rule and counter scope (per sender, or per sender and type).
3. Maximum asset size: chunk index 0 to 112 holds every packed asset up to 1023 frames (96 chunks) and the 600-frame 8-bit `DE_PROFILE` asset (113 chunks, OD-15). An 8-bit asset above 600 frames would need 8 bits.
4. node_id in every message a tile sends (REGISTER, HEARTBEAT, CHUNK_ACK, NACK, BUSY, ASSET_READY, SYNC_READY, TELEMETRY). The source IP address is not a Node ID, and the Pico registry is indexed by Node ID (T-B2-03).
5. ref_seq in NACK and BUSY, and acked_seq in TELEMETRY_ACK separate from the PC's header seq.
6. frame_rate left off the wire.
7. ASSET_CHUNK payload on a byte boundary, length from the datagram.
8. The NACK reason list (CRC mismatch, manifest session mismatch and upload while PLAYING are not in it; any one of them makes 5 reasons and 3 bits).
9. How the Pico learns each Node ID's manifest CRC32, which T-B2-04 needs to check ASSET_READY: the plan's SESSION_CONFIG row has no CRC field. **PROVISIONAL, implemented 29 Oct 2026:** 9 x 32-bit CRCs, one per map position, plus the shared frame_count, added to SESSION_CONFIG (46 B). The other option, the PC sending each tile's MANIFEST to the Pico, does not work as the row stands: MANIFEST carries no Node ID, so the Pico could not tell which tile a CRC belongs to.
10. OD-14 (rejoin message) and OD-20 (two-way exchange) may add rows; 14 type codes are spare. **PROVISIONAL, implemented:** OD-14 option (a), with no new row: the Pico sends the rejoining tile a RESUME_AT (unicast) whose t_resume is a tick deadline at least 600 ms ahead and whose frame_index is the frame the wall shows at that tick.
11. **PC commands to the Pico (PROVISIONAL, implemented).** The plan lists Start, Pause, Resume and Stop as dashboard commands (T-B1-05 `pico_link.py`) but no row carries them from the PC. They reuse the START_AT, PAUSE, RESUME_AT and STOP rows in the direction C to P, with t_start, t_resume and frame_index sent as 0 and ignored: the Pico chooses those values itself (T-B2-05). No type code is added.
12. **Transport (PROVISIONAL, implemented):** the ports and addressing of section 11.
13. **seq scope (item 2, PROVISIONAL, implemented):** one counter per sender for every message it sends; a REGISTER (tile) or a SESSION_CONFIG with a new session_id (PC) restarts the receiver's baseline for that sender, because a sender that reboots starts its counter again.

## 11. Transport (PROVISIONAL, section 10 item 12, owner Benjamin)

All messages are single UDP datagrams on the dedicated router's subnet. Port numbers are plan choices; 4210 and 4211 keep the prototype's beacon and HELLO ports.

| Port | Listener | Messages received | How the sender addresses it |
|---|---|---|---|
| 4210 | every tile (`tile_net`) | BEACON, START_AT, RESUME_AT, PAUSE, STOP from the Pico | subnet broadcast; a rejoin RESUME_AT is unicast to the one tile (section 10 item 10) |
| 4211 | Pico (`coordinator_loop`) | REGISTER, HEARTBEAT, ASSET_READY, SYNC_READY, NACK (reason 2) from tiles | unicast to the Pico, whose address the tile takes from the source address of the beacons |
| 4212 | Pico (`coordinator_loop`) | SESSION_CONFIG and the C to P commands of section 10 item 11 | unicast from the PC to the Pico's address (shown on the Pico's USB log and in the router's client list as `PICO-Main`) |
| 4213 | PC (`pc/pico_link.py`) | STATUS from the Pico | the Pico replies to the address and port of the last valid datagram from the PC |
| 4214 | every tile (`tile_net`) | MANIFEST, ASSET_CHUNK, TELEMETRY_ACK from the PC | unicast from the PC to the tile address it learnt from that tile's TELEMETRY |
| 4215 | PC (`pc/uploader.py`, `pc/telemetry.py`) | TELEMETRY (subnet broadcast); CHUNK_ACK, NACK, BUSY (unicast replies) from tiles | TELEMETRY is broadcast, so the PC learns every tile's address and Node ID without a lookup; replies go to the address and port the request came from |

TELEMETRY is broadcast because nothing else tells the PC which address a Node ID has, and the PC must unicast MANIFEST and ASSET_CHUNK to the right tile (MANIFEST has no Node ID field). One 9 B TELEMETRY per tile per telemetry period (OD-11) is small next to the 120 B/s of beacons.
