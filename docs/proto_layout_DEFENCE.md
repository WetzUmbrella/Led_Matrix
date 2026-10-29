# pc/proto_layout.py: design defence (T-B1-01)

Owner: Jolyn (Buddy 1). Implemented by Ahsanul (B4) for Jolyn, task T-B1-01. Files: `pc/proto_layout.py` (Python twin of `common/proto`), `pc/gen_vectors.py` (writes `protocol/vectors/`), `common/proto/test/proto_vector_main.c` (C side of the cross-language test, `make -C common/proto/test vector_tool`), `pc/tests/test_proto_layout.py`, `pc/tests/test_layout_table.py`, `pc/tests/test_cross_lang.py`. Python follows PEP 8 with type hints and runs on Python 3.8 (local) and 3.12 (CI).

**Names.** The plan names `encode_<msg>()` and `decode_<msg>()`; they exist for all 18 rows (`encode_beacon()`, `decode_beacon()` and so on) as thin wrappers over the generic `encode(name, fields)` and `decode(data)`, so all 18 rows share one packing routine. `LAYOUT` holds the field list. `pack_frame()` and `unpack_frame()` are added for the OD-05 frame vector; T-B1-03 `pack_frame()` in `asset_builder.py` will call this one rather than repeat the nibble order. The C tool is `proto_vector_main.c` because Barr-C 4.1.d requires "main" in the name of a file with `main()`.

Each entry gives the choice, the PostTri row it comes from and the examiner's likely question. Numbers not in PostTri are labelled (plan choice).

## 1. One integer, shifts and `int.to_bytes(..., "little")`

- Source: T-B1-01, "`encode_<msg>()`, `decode_<msg>()` built from `int.to_bytes(..., "little")` and shifts/masks; no `ctypes` structures"; VB-2a.
- Choice: each field is shifted to its bit offset and added into one Python integer, which is written with `to_bytes(length, "little")`. Decoding is the reverse with `int.from_bytes` and a mask per field. This is the same numbering as `proto_put_bits()` (bit 0 = least significant bit of byte 0), so no byte swapping is needed anywhere.
- Examiner: "Why is `struct.pack('<BBH...')` not used, when it also gives little-endian bytes?"

## 2. The field list comes from `layout_table.md`

- Source: T-B1-01, "Python and C read the same `layout_table.md` field list."; R3, "One shared layout table, rounding rule and cross-language test vectors."
- Choice: `LAYOUT` is copied from the table, and `test_layout_table.py` parses the table's sections 2, 3 and 4 and fails if any name, offset, width, range or length differs. The C codec is tied to Python by the vectors (section 3), so a change to any of the three shows up as a test failure.
- Examiner: "Benjamin widens seq to 24 bits in the table. Which tests fail, in which order, and what do you change?"

## 3. Vectors in both directions

- Source: T-B1-01 acceptance, "Python to C and C to Python: byte-for-byte equal for 100% of vectors. Pass = 0 mismatches."
- Choice: 58 vectors: for each of the 18 rows a typical vector (every field non-zero and distinct, so a misplaced field changes the bytes), a minimum and a maximum (the T-B2-01 boundary set); the OD-05 worked frame and a full pattern frame; a manifest with format_version 0; and the 115,200 B payload. For each message vector the C tool decodes the Python bytes and prints the same fields, and encodes the same fields to the same bytes, which Python decodes back. Result 29 Oct 2026: 57 checked vectors (the payload is checked by length), 0 mismatches. Mutation check (procedure, no log kept): swap the order of two TELEMETRY fields in `proto_encode_telemetry()`, rebuild the tool, run `pytest pc/tests/test_cross_lang.py`: it must fail with py->c and c->py mismatches; restore the file. Run on 29 Oct 2026: it failed as expected.
- Examiner: "Your C and Python both come from one author. What does a cross-language test prove that a review cannot, and what would it miss?"

## 4. Vectors are regenerated and compared

- Source: T-B1-01 evidence, "`protocol/vectors/` commit".
- Choice: `gen_vectors.py` uses fixed values only, and `test_committed_vectors_are_current` regenerates them into a temporary folder and compares every file with the committed ones, so the committed vectors can never drift from the code.
- Examiner: "Who is allowed to regenerate the vectors, and what must be true before the new ones are committed?"

## 5. Range checks in Python too

- Source: VC-3, "an 18 s GIF must be rejected on the PC"; VC-6 (channel values 0 to 8).
- Choice: `encode()` raises `LayoutError` for any field outside its row's range, and `pack_frame()` refuses a channel value above 8, so the PC cannot send what the tile would reject. The 1023-frame check for a GIF belongs to `asset_builder.check_frame_limit()` (T-B1-02); the codec refuses frame_count 1024 as a second line.
- Examiner: "The dashboard lets a user type brightness 40. At which point does the value 40 / 255 x 8 get stopped, and by which function?"

## 6. Unknown format_version and the 115,200 B payload are valid inputs to the codec

- Source: VC-5, "Test vectors with an unknown version and a 115,200 B payload must both give the NACK and no ASSET_READY."
- Choice: the codec encodes and decodes format_version 0, because the tile must receive it to NACK it (T-B3-05, T-X-08 case 4). The 115,200 B payload is a file, not a message: it is too long for one ASSET_CHUNK (the C decoder refuses it by length), it needs 113 chunks, which the 0 to 112 chunk index allows, and against a 600-frame manifest it fails the length rule 600 x 96 = 57,600 B (T-X-08 case 5).
- Examiner: "Walk the 115,200 B payload through the upload: how many chunks, and at which chunk index does the tile first know the payload is longer than frame_count x 96 B?"

## Open items

OD-05 (nibble order) and OD-07 (widths) are PROVISIONAL; `protocol/layout_table.md` section 10 lists what Benjamin confirms. When the table is frozen, `LAYOUT`, `proto.c` and the vectors change in one commit.

## Questions for Jolyn before Tribunal 2

Answer each in your own words, without reading the code.

1. Encode a HEARTBEAT from Node ID 3 with session_id 1 and seq 2 by hand, bit by bit, and check your bytes against `pc/proto_layout.py`.
2. Why does packing nibble k at bit 4k give the bytes `21 43 65` for pixels (1, 2, 3) and (4, 5, 6), and what would option (b) give?
3. The cross-language test passes. Name one kind of error in the layout that it cannot catch, and the test or review that would.
