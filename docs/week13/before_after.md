# CR1 before/after: RAM and message size

"Before" is the tag `cr1-before` on commit `45e038ca2aa7d8386d5cb895838512d708e45b4d` (T-B4-01). At that commit there is no pack or unpack code: `git grep -n -e proto_put_bits -e proto_encode -e pack_frame -e unpack 45e038c -- '*.c' '*.h' '*.py'` returns 0 lines, and `frames.h:21` stores 3 bytes per pixel. "After" is filled by T-B3-01, T-X-04 and T-X-06 once the packed build exists.

Every before cell carries one of two labels, as the T-B4-01 row requires:

- **measured**: taken at the tag. A blank measured cell points to its procedure in `docs/week13/before/BENCH.md`; it is never filled with an estimate.
- **derived, W6 8-bit design**: what the prototype never had, computed from PostTri numbers with the arithmetic shown.

The 8-bit timing baseline for STP-3 is not in this file: by the Round 0 ruling on OD-15 it is a `DE_PROFILE` build of the new `display_engine` (T-X-03), and `cr1-before` stays the before reference for RAM and message size.

## 1. Measured at the tag

| Item | Before | Label | Source or procedure | After |
|---|---|---|---|---|
| `sizeof(beacon_t)` on the target (Xtensa compiler) | | measured | `BENCH.md` S-2, `docs/week13/before/sizeof_xtensa.txt` | |
| `sizeof(hello_t)` on the target (Xtensa compiler) | | measured | `BENCH.md` S-2, `docs/week13/before/sizeof_xtensa.txt` | |
| `sizeof(beacon_t)`, host x86_64 gcc | 50 B | measured on the host, not the target | `docs/week13/before/sizeof.txt` | |
| `sizeof(hello_t)`, host x86_64 gcc | 9 B | measured on the host, not the target | `docs/week13/before/sizeof.txt` | |
| `beacon_t` field sum (`panel_sync.c:52-63`) | 50 B | derived | magic 4 + version 1 + seq 4 + cmd 1 + frame_no 4 + layout_ver 1 + count 1 + cols 1 + rows 1 + ids 8 x 4 = 18 + 32 = 50 | |
| `hello_t` field sum (`panel_sync.c:65-69`) | 9 B | derived | magic 4 + id 4 + board 1 = 9 | |
| Beacon, on-air UDP payload bytes | | measured | `BENCH.md` B-2, `docs/week13/before/onair_before.pcapng` and `onair_before_beacon.txt` | |
| HELLO, on-air UDP payload bytes (`p->tot_len`, `ledwall_sync/main.c:123`) | | measured | `BENCH.md` B-3, `docs/week13/before/onair_before_hello.log` | |
| `panel_sync` static DRAM used | | measured | `BENCH.md` S-1, `docs/week13/before/idf_size.txt` | |
| `panel_sync` static IRAM used | | measured | `BENCH.md` S-1, `docs/week13/before/idf_size.txt` | |
| `ledwall_sync.elf` text / data / bss | | measured | `BENCH.md` S-3, `docs/week13/before/pico_size.txt` | |
| Free internal heap after WiFi (`heap_caps_get_free_size(MALLOC_CAP_INTERNAL)`), minimum of 10 boots | | measured | `BENCH.md` B-1, `docs/week13/before/heap_at_tag.log` | |
| Largest internal block after WiFi (`heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)`), minimum of 10 boots | | measured | `BENCH.md` B-1, `docs/week13/before/heap_at_tag.log` | |

Notes on section 1:

- **Why the host `sizeof` is shown but the target cells stay blank.** Host and Xtensa agree by construction: both structs are `__attribute__((packed))` (`panel_sync.c:52`, `:65`), so GCC adds no padding on any target, and every field is an exact-width `uint32_t` or `uint8_t`, with no `int`, `enum`, pointer or bit-field whose size could change between targets. The plan still lists `sizeof` under "measured at the tag", and a target check costs one compiler command with no hardware (S-2), so the target cells wait for `sizeof_xtensa.txt` rather than copying the host value.
- The Pico's copies of the two structs (`ledwall_sync/main.c:46-63`) have the same field names, types, order and `packed` attribute as the tile's, so the beacon the Pico sends (`sizeof(b)` at `main.c:278-280`) has the same field sum.
- Heap statistic: the minimum over 10 boots **(plan choice: T-B3-01 decides OD-04 from the minimum internal value, so before and after use the same statistic)**. Prototype state during the probe, from the T-B4-01 row: whole-GIF animation mapped from flash (`frames.c:66-71`) at 30 fps.

## 2. Derived reference, W6 8-bit design: asset and RAM

| Item | Before | Label | Arithmetic and source | After |
|---|---|---|---|---|
| Asset per tile | 115,200 B | derived, W6 8-bit design | 64 pixels x 3 channels x 1 B (8 bits per channel) = 192 B per frame; 192 B x 600 frames = 115,200 B (PostTri Version A Buddy 1: "192 B per frame, 600 frames, 115,200 B per tile"; LDC-1) | |
| Upload chunks of 1,024 B | 113 chunks | derived, W6 8-bit design | 115,200 / 1,024 = 112.5, so 113 chunks; 112 x 1,024 = 114,688 B, last chunk 115,200 - 114,688 = 512 B (LDC-1: "115,200 B, 113 chunks") | |
| RAM copy plus margin | 147,968 B | derived, W6 8-bit design | 115,200 B + 32,768 B margin **(proposed, RAR-4)** = 147,968 B (RAR-4: "147,968 B falls to 90,368 B") | |

## 3. Derived reference, W6 8-bit design: message types that did not exist at the tag

### 3.1 Typing rule and common header

The cr1-before C typing rule, copied from the T-B4-01 row: "`uint32_t` for magic, IDs, seq and counters, `uint8_t` for each enum or flag, `uint64_t` for time, 8 bits per colour channel". Sizes are plain byte sums of the fields, with no padding, as in the packed tag structs.

Common header on every message (T-B2-01 row; IFM-2a, W6D-2f): message type, session_id, seq.

| Header field | C type by the rule | Bytes |
|---|---|---|
| message type | `uint8_t` (enum) | 1 |
| session_id | `uint32_t` (ID) | 4 |
| seq | `uint32_t` (seq) | 4 |
| **Header total** | | **9** |

Counting decisions, used in every row below:

1. **magic and version are not counted.** The tag structs carry them (`panel_sync.c:53-54`), but neither is in the T-B2-01 common header or in any T-B2-01 message row. If T-X-04 decides the before cells should keep them, each message gains 4 + 1 = 5 B.
2. **A field the plan lists inside braces that is the header field is counted once**: session_id in MANIFEST and ASSET_READY, and seq in TELEMETRY (the tile's own telemetry sequence). The `{seq}` of TELEMETRY_ACK is counted as a separate field: it echoes the tile's TELEMETRY seq, while the ACK's own header seq is the PC's sequence, which IFM-2a requires to be newer than the PC's last one.
3. **Field types the rule does not name**, each taken from the tag structs where they have an equivalent:
   - format_version: `uint8_t`, as the tag's `version` field (`panel_sync.c:54`);
   - chunk index and frame index: `uint32_t` (counters), as the tag's `seq` and `frame_no` (`panel_sync.c:55,57`);
   - frame_rate: no equivalent field at the tag (`FPS` is a `#define`, `panel_sync.c:49`); counted as `uint32_t`, the rule's type for numeric counts **(plan choice)**;
   - Node ID: `uint32_t` (ID), as the tag's panel `id` (`panel_sync.c:67`). The tag keeps the board number in a `uint8_t` (`panel_sync.c:68`); the rule says IDs are `uint32_t`, so the rule is followed;
   - CRC32 and the fault word: `uint32_t`, 32 bits by definition (T-B2-01: "CRC32 32 bits", "fault word 32 bits (LDC-2)").
4. **Bitmaps** (STATUS): the rule says "`uint8_t` for each ... flag". Read literally, a 9-node bitmap is 9 flags = 9 B. The other reading, one 16-bit word per bitmap, is the after design (VC-11b), not the before typing. Both are shown; the literal one is the cell value.
5. **Arrays sized by node count** use 9 nodes (`CL_MAX_NODES 9`, T-B2-03; 3x3 layout), as the tag beacon's `ids[MAX_NODES]` (`panel_sync.c:62`) is an array of `uint32_t` IDs.
6. The rule puts counters at `uint32_t`, while the tag beacon keeps its panel `count` in a `uint8_t` (`panel_sync.c:59`). The plan rule is followed.
7. HEARTBEAT and BEACON are not in the T-B4-01 list because they existed at the tag: their before values are the measured HELLO (the tile heartbeat at the tag, every 0.3 s, `panel_sync.c:147-163`) and beacon in section 1.

### 3.2 Before size per message type

"Field list" says whether the plan fixes the fields: **as listed** means the T-B2-01 row (or the row it cites) names every field; **provisional until T-B2-01 freezes the table** means the plan or PostTri leaves the field list open, so the cell counts only the fields stated in the source column.

| Message | Fields beyond the 9 B header (C type, bytes) | Arithmetic | Before | Label | Field list | Source of the fields | After |
|---|---|---|---|---|---|---|---|
| MANIFEST | format_version `uint8_t` 1; frame_count `uint32_t` 4; frame_rate `uint32_t` 4 (decision 3); CRC32 `uint32_t` 4; session_id is the header field | 9 + 1 + 4 + 4 + 4 | 22 B | derived, W6 8-bit design | as listed (frame_rate type is decision 3) | T-B2-01 "MANIFEST (session_id, format_version, frame_count, frame_rate, CRC32)"; VC-5; IFM-1a | |
| ASSET_CHUNK | chunk index `uint32_t` 4; payload 1,024 B | 9 + 4 + 1,024 | 1,037 B (last of 113 chunks: 9 + 4 + 512 = 525 B) | derived, W6 8-bit design | as listed | T-B2-01 "ASSET_CHUNK (chunk index, 1,024 B payload)"; last chunk 512 B from section 2 | |
| CHUNK_ACK | chunk index `uint32_t` 4 | 9 + 4 | 13 B | derived, W6 8-bit design | provisional until T-B2-01 freezes the table | T-B2-01 names CHUNK_ACK with no fields; T-B3-05 "each chunk is ACKed by index"; PostTri change log "per-chunk ACK" | |
| NACK | reason code `uint8_t` 1 (enum) | 9 + 1 | 10 B | derived, W6 8-bit design | provisional until T-B2-01 freezes the table | T-B2-01 "NACK (reason code)"; VC-5. Not fixed: whether a NACK also names the chunk index or the seq it refuses | |
| BUSY | none stated | 9 | 9 B | derived, W6 8-bit design | provisional until T-B2-01 freezes the table | T-B2-01 names BUSY with no fields; T-B3-04; PostTri shared resource row "Immutable RAM playback asset" ("rejected with BUSY") | |
| ASSET_READY | CRC32 `uint32_t` 4; session_id is the header field | 9 + 4 | 13 B | derived, W6 8-bit design | as listed | T-B2-01 "ASSET_READY {session_id, CRC32}"; W6D-7a | |
| SYNC_READY | none stated | 9 | 9 B | derived, W6 8-bit design | provisional until T-B2-01 freezes the table | T-B2-01 names SYNC_READY with no fields; its rule is OD-10 | |
| START_AT | t_start `uint64_t` 8 | 9 + 8 | 17 B | derived, W6 8-bit design | as listed | T-B2-01 "START_AT {t_start}" | |
| RESUME_AT | t_resume `uint64_t` 8; saved frame index `uint32_t` 4 | 9 + 8 + 4 | 21 B | derived, W6 8-bit design | as listed | T-B2-01 "RESUME_AT {t_resume, saved frame index}" | |
| PAUSE | none stated | 9 | 9 B | derived, W6 8-bit design | provisional until T-B2-01 freezes the table | T-B2-01 names PAUSE with no fields; IFM-2b; T-B4-04 saves the frame index on the tile, not in the message | |
| STOP | none stated | 9 | 9 B | derived, W6 8-bit design | provisional until T-B2-01 freezes the table | T-B2-01 names STOP with no fields; IFM-2b | |
| STATUS | readiness bitmap 9 x `uint8_t` = 9; online bitmap 9 x `uint8_t` = 9; coordinator state `uint8_t` 1 (enum) | 9 + 9 + 9 + 1 | 28 B (one 16-bit word per bitmap instead: 9 + 2 + 2 + 1 = 14 B, decision 4) | derived, W6 8-bit design | provisional until T-B2-01 freezes the table | T-B2-01 "STATUS (readiness bitmap, online bitmap, coordinator state)"; T-B2-04. Bitmap typing at the before rule is not fixed (decision 4) | |
| TELEMETRY | fault word `uint32_t` 4; orientation `uint8_t` 1 (enum); display state `uint8_t` 1 (enum); seq is the header field | 9 + 4 + 1 + 1 | 15 B | derived, W6 8-bit design | as listed | T-B2-01 "TELEMETRY {seq, fault word, orientation, display state}"; VC-4; LDC-2 | |
| TELEMETRY_ACK | acked seq `uint32_t` 4 (decision 2) | 9 + 4 | 13 B | derived, W6 8-bit design | as listed | T-B2-01 "TELEMETRY_ACK {seq} (VC-4)" | |
| SESSION_CONFIG | layout `uint8_t` 1 (enum: 1x1, 2x2, 3x3); Node ID map 9 x `uint32_t` = 36 (decision 5); failure policy `uint8_t` 1 (enum: pause or continue) | 9 + 1 + 36 + 1 | 47 B | derived, W6 8-bit design | provisional until T-B2-01 freezes the table | T-B2-01 "SESSION_CONFIG (layout, Node ID map, failure policy)"; T-B1-05. The shape of the Node ID map (fixed 9 entries, one Node ID per position) is not fixed by the plan | |
| REGISTER | Node ID `uint32_t` 4; cached asset session_id `uint32_t` 4; cached CRC32 `uint32_t` 4 | 9 + 4 + 4 + 4 | 21 B | derived, W6 8-bit design | as listed | T-B2-01 "REGISTER (Node ID, cached asset session_id and CRC32)"; T-B3-05. The cached session_id is a separate field from the header session_id: the tile may hold an asset from an earlier session | |

The check that every message type in the frozen T-B2-01 table has a before cell labelled measured or derived is made in T-X-04 and T-X-06, not here (T-B4-01 row, Round 2 ruling F21).
