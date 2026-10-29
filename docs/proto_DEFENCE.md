# common/proto: design defence (T-B2-01)

Owner: Benjamin (Buddy 2). Implemented by Ahsanul (B4) for Benjamin, task T-B2-01. Files: `protocol/layout_table.md` (the single source of every field), `common/proto/proto.c/.h` (C codec, an ESP-IDF component for the tiles through `EXTRA_COMPONENT_DIRS` and a source file in `ledwall_sync/CMakeLists.txt` for the Pico), `common/proto/test/` (host tests, `make -C common/proto/test`). Coding standard: Barr-C:2018, `docs/coding_standard.md` (D-2 only for `int main`). The Python twin is `pc/proto_layout.py` (T-B1-01, Jolyn).

**Names.** The plan names the files `proto_layout.c/.h` and the functions `proto_put_bits()`, `proto_get_bits()`, `proto_put_u16_le/u32_le/u64_le`, `proto_get_*_le` and `proto_encode_<msg>()` / `proto_decode_<msg>()`. Barr-C 6.1.i requires a public function to start with its module name, and `tools/barr_check.py` takes the module name from the file name, so the files are `proto.c/.h` and every plan function name is kept unchanged. Plan `proto_layout.h` = `proto.h`; plan `proto_layout.c` = `proto.c`; plan `test_proto.c` = `test_proto.c` (bit packing and BEACON) plus `test_messages.c` (every other row). Added beyond the plan: `proto_decode_header()` (dispatch on type before choosing a decoder) and `proto_beacon_set_t_p()` (T-B2-02 reads t_p after encoding). Message types are `proto_<msg>_t`; PAUSE and STOP share `proto_command_t` because both are header only.

Each entry gives the choice, the PostTri row it comes from (inventory ID and wording, quoted from `Plan/commitment_inventory.md` or the T-B2-01 plan row), and the question an examiner is likely to ask. Numbers not in PostTri are labelled (plan choice).

## 1. Explicit shifts and masks, never a wire struct

- Source: VB-2a, "Wire format: explicit shifts and masks, little-endian, one shared table, cross-language test vectors."; VB-2b, "C bit-fields only for local structs, never on the wire."; VC-9 (rejected AI suggestion), "bit-field order is implementation-defined; Python struct has no bit-fields; unaligned member pointers fault on the M0+ and raise LoadStoreAlignment on Xtensa".
- Choice: every field goes through `proto_put_bits()` / `proto_get_bits()` one bit at a time from its least significant bit. The message structs in `proto.h` are RAM structs the caller fills; no struct is ever passed to a socket. `grep -rn "__attribute__((packed" common/proto` returns 0 lines.
- Examiner: "Your RAM struct `proto_beacon_t` has a 64-bit member after a 4-byte header struct. What is its `sizeof` on the RP2040 and on the ESP32-S3, and why does that not matter for the wire?"

## 2. Bit 0 is the least significant bit of byte 0

- Source: LDC-4, "explicit shifts and masks, little-endian, one shared layout table."
- Choice: field offsets count from the least significant bit of byte 0, so any field wider than 8 bits is little-endian on the wire and the whole message equals one integer written with Python `int.to_bytes(n, "little")`. The same numbering makes OD-05 option (a) the natural nibble order (section 7).
- Examiner: "Write the bytes of a BEACON with type 10, session_id 0x5A, seq 0x1234 and t_p 0 by hand. Where does the seq start?"

## 3. Common header: type 5 bits, session_id 8, seq 16

- Source: IFM-2a, "every message carries session_id and seq"; W6D-2f, "Every command carries a session ID and a sequence number."; Round 0 ruling SC-10, widths sized "by the valid-range rule ... labelled (plan choice, valid-range rule)"; OD-07 brief.
- Choice: 5 + 8 + 16 = 29 bits on every message. 18 types need 5 bits (14 spare for OD-14 and OD-20). 16-bit seq wraps after 109.2 min at 10 Hz, so "newer" is wrap-aware: (uint16_t)(s - last) in 1 to 32,767. PROVISIONAL (OD-07).
- Examiner: "A show runs for two hours. Show that your seq comparison still rejects a beacon from 50 minutes ago and accepts the next one."

## 4. Contiguous packing, padded to a whole byte

- Source: CB-0, "applies to states, shared task and ISR data, queue messages and payloads (binary)"; change requirement "only the number of bits required to represent its valid range".
- Choice: fields follow each other with no gap; the last byte is padded with zeros. Bytes saved against the before rows (`layout_table.md` section 4): BEACON 50 B to 12 B, TELEMETRY 15 B to 9 B, STATUS 28 B to 7 B. Exception: the ASSET_CHUNK payload starts on a byte boundary (section 6). (plan choice)
- Examiner: "Your BEACON is 93 bits. How many bytes go on air, and what is in the last 3 bits?"

## 5. Valid-range checks on both sides

- Source: VC-5, "On an unknown version or wrong length it NACKs with a reason code"; T-B2-01, "GIFs over 1023 frames rejected on the PC".
- Choice: each encoder returns 0 and each decoder returns false for a field outside its row's range (Node ID 0 or 10 to 15, frame_count 0, chunk index 96 to 127, layout 3, coordinator state 12 to 15, display state 7) and for a wrong length or type. An unknown format_version is a valid wire value: the tile decodes it and then NACKs (T-B3-05), so the codec does not hide the case VC-5 must test. session_id and seq are the receiver's checks, not the codec's.
- Examiner: "A MANIFEST arrives with format_version 0. Which layer rejects it, what goes back to the PC, and why is the codec not that layer?"

## 6. ASSET_CHUNK payload on a byte boundary, length from the datagram

- Source: T-B2-01, "ASSET_CHUNK (chunk index, 1,024 B payload; last chunk 256 B = 57,600 - 56 x 1,024 **(derived)**)"; LDC-8.
- Choice: header and 7-bit chunk index take 36 bits; 4 zero bits pad to byte 5; the payload follows and its length is the datagram length minus 5. The decoder returns a pointer into the received buffer, so a 1,024 B chunk is never copied bit by bit. The largest index is 95 (1023 frames x 96 B = 98,208 B = 96 chunks). (plan choice)
- Examiner: "Why not bit-pack the payload straight after the 7-bit index, and what would that cost the tile per chunk?"

## 7. Packed frame nibble order, OD-05 option (a)

- Source: VB-1, "rounding round(v x 8 / 255) and nibble order go into the format specification"; LDC-1; OD-05 brief recommendation (a) with vector `21 43 65`.
- Choice: nibble k of a frame is the 4-bit field at bit 4k, the same numbering as every other field, so `proto_get_bits(frame, 4 * k, 4)` reads it. PROVISIONAL (OD-05, Jolyn and Jia Jun).
- Examiner: "Pixel 0 is (1, 2, 3) and pixel 1 is (4, 5, 6). Give the three bytes, and say what the tile writes into the LED buffer for G1."

## 8. node_id in every message a tile sends

- Source: T-B2-03, "Registry holds 9 Node IDs"; FR9; OD-07 brief, "HEARTBEAT, BUSY, CHUNK_ACK, SYNC_READY and ASSET_READY list no sender field."
- Choice: a 4-bit node_id (1 to 9) in REGISTER, HEARTBEAT, CHUNK_ACK, NACK, BUSY, ASSET_READY, SYNC_READY and TELEMETRY. The Pico indexes its registry and bitmaps by Node ID, and a source IP is not a Node ID (DHCP can change it). PROVISIONAL (OD-07 open item 4).
- Examiner: "A tile reconnects and DHCP gives it a new IP. How does the Pico know which readiness bit its ASSET_READY sets?"

## 9. ref_seq in NACK and BUSY; acked_seq in TELEMETRY_ACK

- Source: VC-4, "Telemetry carries a sequence number and the PC returns an ACK with it"; `before_after.md` counting decision 2 (the ACK's own header seq is the PC's sequence).
- Choice: TELEMETRY_ACK carries the TELEMETRY header seq it acknowledges in its own 16-bit field, so the PC's header seq stays a normal increasing counter (IFM-2a). NACK and BUSY carry the header seq of the message they refuse, so the sender knows which one to resend. PROVISIONAL (OD-07 open item 5).
- Examiner: "The tile sends TELEMETRY seq 41 and 42; the ACK for 41 arrives after 42 was sent. Which bits does the Network Task clear?"

## 10. frame_rate left off the wire

- Source: T-B1-03, "Manifest {session_id, format_version, frame_count (10 bits), frame_rate 60, CRC32 per tile}"; change requirement valid-range rule.
- Choice: frame_rate has one valid value, 60, which needs 0 bits; the decoder reports `PROTO_FRAME_RATE` 60. If a second rate is ever allowed it needs a new format_version. PROVISIONAL (OD-07 open item 6).
- Examiner: "The plan lists frame_rate in the manifest. Defend leaving it out, and say what changes if the PC wanted to send a 30 fps asset."

## 11. Message type codes in row order

- Source: T-B2-01 message rows, REGISTER to TELEMETRY_ACK.
- Choice: codes 0 to 17 in the order of the plan row (BEACON = 10). PROVISIONAL (OD-07).
- Examiner: "What does your tile do with a datagram whose type code is 20?"

## Open items

`protocol/layout_table.md` section 10 lists the ten items Benjamin confirms before the freeze; item 9 (how the Pico learns each Node ID's CRC32 for the T-B2-04 ASSET_READY check) blocks T-B2-04.

## Questions for Benjamin before Tribunal 2

Answer each in your own words, without reading the code.

1. Why would a `__attribute__((packed))` struct with a 5-bit type bit-field give different bytes on the RP2040 compiler and in Python, and how do your test vectors prove that `proto.c` and `proto_layout.py` agree?
2. Take the STATUS row. Derive each field's width from its valid range, add up the bits, and say how many bytes go on air and why that is smaller than the before size.
3. The seq field wraps every 109 minutes at 10 Hz. Explain the wrap-aware "newer" test, and give one stale message it rejects and one fresh message a plain `>` comparison would wrongly reject.
