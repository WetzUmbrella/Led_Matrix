# PC tools: design defence (T-B1-02 to T-B1-07)

Owner: Jolyn (Buddy 1). Implemented by Ahsanul (B4) for Jolyn in Jia Jun's session (29 to 30 Oct 2026), to review, change where they disagree, and defend. Files: `pc/asset_builder.py`, `pc/uploader.py`, `pc/telemetry.py`, `pc/pico_link.py`, `pc/dashboard.py`, `pc/tests/numbered_pattern.py`, `pc/tests/check_assets.py`, `pc/tools/evidence_index.py`; pytest in `pc/tests/` (`python3 -m pytest -q tools/tests pc/tests`). Style: PEP 8 with type hints (`pycodestyle` clean), standard library plus Pillow for GIFs.

**Names.** The plan's functions keep their names: `load_gif()`, `resample_60fps()`, `partition_tiles()`, `check_frame_limit()`, `pack_frame()`, `build_manifest()` (asset_builder); `upload_asset()`, `retry_missing()`, `RateLimiter` (uploader). The dashboard's rules are the class `DashboardModel`, so pytest checks them without sockets; `DashboardShell` is the interactive front end.

## 1. Resample by cumulative time (T-B1-02)

- Source: VA-1.4; T-B1-02 "mapped onto the 1,000,000 / 60 us grid by cumulative time, so a 30 ms frame becomes 2, 2, 2, 2, 1 ticks with no build-up".
- Choice: source frame i ends at tick ceil(cumulative_ms(i) x 60 / 1000); frame_count = round(total x 60 / 1000). Pytest: the 2, 2, 2, 2, 1 pattern, 10.0 s gives 600, after 600 frames the tick count never drifts by a tick.
- Examiner: "The prototype used the mean delay. Show a GIF where that goes wrong and yours does not."

## 2. 0 and 10 ms delays (OD-06)

- Source: OD-06 option (a), "treat 0 and 10 ms as 100 ms, like most browsers".
- Choice: PROVISIONAL (a); one pytest case per delay value.
- Examiner: "A GIF has a 10 ms delay on every frame. How long does it play, and why not 100 times faster?"

## 3. 1023-frame limit

- Source: VB-4b, VC-3 (10-bit frame_count).
- Choice: refused with a message naming the 1023-frame (10-bit) limit; the command line exits 1. Pytest: an 18 s GIF.
- Examiner: "Why is the limit 1023 and not 1024?"

## 4. Brightness cap on the PC only (VC-6, OD-02)

- Source: VC-6; OD-02 option (b) "keep a control limited to at most 8, with `asset_builder` refusing any value above 8"; the session-log note that 40 was too bright.
- Choice: each channel becomes round(v x b / 255), b from 1 to 8, half up; 9, 40 and 255 are refused (pytest and the dashboard model). A brightness change drops the built assets and says every tile must be rebuilt and re-uploaded.
- Examiner: "Where is brightness applied, once or twice, and which test would catch it being applied twice?"

## 5. Pack, CRC and manifest (T-B1-03)

- Source: LDC-1, VB-1, OD-05 option (a).
- Choice: `proto_layout.pack_frame()` packs 192 nibbles into 96 B; payload = frame_count x 96 B; CRC32 = `zlib.crc32` of the payload; the MANIFEST carries format_version 2, frame_count and CRC32 (frame_rate is not on the wire). Pytest: 57,600 B for 600 frames, the T-B1-01 worked vector byte for byte. The 8-bit format exists only for the `DE_PROFILE` timing run (OD-15).
- Examiner: "Show the byte that holds pixel 0's red and green."

## 6. Upload: window of 4, per-chunk ACK, only missing resent (T-B1-04)

- Source: T-B1-04; LDC-8; VA-3.6 (Upload Queue reject and NACK).
- Choice: MANIFEST, a 1 s pause for the tile's erase (PROVISIONAL), then rounds of the missing chunks 4 at a time (the tile's Upload Queue length); NACK reason 3 leaves a chunk missing for the next round; BUSY and NACK reasons 0 or 1 stop the upload. Rate limit 100 chunks/s until OD-13 is measured (PROVISIONAL). Pytest against a fake tile: 10 of 10 uploads all ACKed with equal CRC, 6 dropped chunks resent exactly 6 times, BUSY stops, `--expect-busy` counts 57 of 57.
- Examiner: "Why a window of 4, and what happens with a window of 57?"

## 7. Dashboard rules (T-B1-05)

- Source: T-B1-05 row; W6 statechart 1; NFR9.
- Choice: readiness only from Pico STATUS; Start enabled only when the readiness bitmap equals the map's mask; a tile that leaves the online bitmap is named with the time; a new GIF or layout only while stopped; telemetry ACKed with its seq. Pytest covers each rule.
- Examiner: "Your dashboard shows a tile lost. How many seconds after the Pico logged it, at worst?"

## 8. Evidence index from evidence (T-B1-07)

- Source: T-B1-07 row.
- Choice: generated from the inventory and the plan; a row is "evidence present" only when the plan's artefact file exists and is not a procedure still waiting for its run; Green readiness only with such an artefact. Commitment text stays outside the public repository.
- Examiner: "Who decides a readiness row is Green, and could a template document make it Green?"

## PROVISIONAL list for Jolyn to confirm

- OD-06 option (a); OD-02 option (b).
- Upload window 4, 1 s manifest pause, 100 chunks/s, 20 rounds.
- Numbered-pattern encoding (Node ID as red LEDs from the top left, frame number in 10 green bits).
- Dashboard as a command-line shell over a tested model.

## Questions for Jolyn before Tribunal 2

1. Work the 30 ms example by hand for the first 5 source frames and show where each tick boundary falls.
2. Explain how the uploader learns a tile's IP address and why MANIFEST carries no Node ID.
3. Show the dashboard refusing Start with one tile missing, and explain where that decision is made.
