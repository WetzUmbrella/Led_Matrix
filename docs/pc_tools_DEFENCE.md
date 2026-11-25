# PC tools: design defence (T-B1-02 to T-B1-07)

Owner: Jolyn (Buddy 1). Implemented by Ahsanul (B4) for Jolyn in Jia Jun's session (29 to 30 Oct 2026), to review, change where they disagree, and defend. Files: `pc/asset_builder.py`, `pc/uploader.py`, `pc/telemetry.py`, `pc/pico_link.py`, `pc/dashboard.py`, `pc/tests/numbered_pattern.py`, `pc/tests/check_assets.py`, `pc/tools/evidence_index.py`; pytest in `pc/tests/` (`python3 -m pytest -q tools/tests pc/tests`). Style: PEP 8 with type hints (`pycodestyle` clean), standard library plus Pillow for GIFs. Updated 25 Nov 2026 against `a6d4902`: sections 1 to 8 are the October tools, still in the code with the values checked; section 9 covers the website and what the game and the 9-tile wall added (`pc/web.py`, `pc/web/index.html`, `pc/web/score.html`, `pc/gif_library.py`, `pc/tap_log.py`). pytest: 518 passed, 1 skipped (25 Nov).

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

## 9. The website and what the game added (12 to 25 Nov 2026)

- **A website over the tested model** (step 4e, 18 Nov). `pc/web.py` serves the control page and a `/score` page for a TV, with a small JSON API over `DashboardShell`. It uses only the Python standard library (`http.server`): no CDN, no WebSockets, and it works on the router's subnet with no internet. The pages poll `/api/state` every 250 ms. A pump thread calls `shell.pump()` every 20 ms, so STATUS, TAPs and TELEMETRY are read and ACKed even with no browser open. Uploads run in the background, so a request never waits on one.
- **Any layout** (A1, A2, DS-15). A grid of 1 to 9 rows and columns with up to 9 tiles. Tap-to-place: choose a cell, then tap a tile, and that tile goes into the cell. Placing a tile already on the grid moves it. A tile can be switched off (blank, keeps its cell) or removed.
- **Parallel uploads** (A13, 18 Nov). One worker per tile (`UPLOAD_WORKERS` = 9), so a whole wall uploads in about 3 s. Each worker keeps the October rules: window 4, 1 s after the MANIFEST, ACK timeout 0.25 s, 100 chunks/s, at most 20 rounds. A tile not heard on TELEMETRY for 5 s is offline and not uploaded to: on 18 Nov an upload to an unplugged tile retried for 27 s and held up every layout change.
- **Game controls** (A4 to A12). Video or game mode (only while stopped), level, ARM with the tap threshold, Start now, Abort, and the tap test, which shows every tap's peak so the operator can tune the threshold. An ARM waits up to 1.5 s for STATUS to show the round armed, because the Pico ACKs a refused GAME_CONTROL too (19 Nov: ARM pressed, nothing happened, no error). Results show hits, missed, wrong, accuracy, average reaction, stars and the best score per level. Every tap goes to a CSV log (`pc/tap_log.py`).
- **Recovery from a Pico restart with no operator action** (18 to 24 Nov). After 2 s with no STATUS the website treats the Pico as silent, disables the buttons, and resends the last SESSION_CONFIG every 1 s until the Pico answers. It then publishes the wall again and restores the mode, but only once the Pico is READY: a restarted Pico refuses a mode change while SYNCHRONISING but still ACKs it, so an earlier resend was lost (24 Nov). The PC's command seq is saved in `pc/.last_seq`, so a restarted PC is not dropped as stale (step 4b).
- **Re-upload of a tile that lost its video** (`7f500c7`, 24 Nov). A tile that ACKed its asset this session but is online and not ready for 5 s (`LOST_ASSET_S`), while the Pico waits for readiness, gets its section again.
- **Failure policy on the command line** (`--policy pause|continue`, 25 Nov). The demo uses `continue`, because the pause policy restarts the Pico after a tile is away more than 5 s (`coordinator_loop_DEFENCE.md` section 6).
- **GIF library.** GIFs are uploaded through the page (at most 8 MB, checked name and magic bytes) and kept in `pc/gifs/`. That folder is git-ignored, so a fresh clone has no demo GIF.

## Settled values (25 Nov 2026)

| Item | Value |
|---|---|
| OD-06 (0 and 10 ms delays) | option (a): played as 100 ms, like most browsers |
| OD-02 (brightness) | option (b): a control from 1 to 8; above 8 refused |
| Upload | window 4, 1 s after the MANIFEST, 0.25 s ACK timeout, 100 chunks/s, 20 rounds, 9 tiles in parallel |
| Pico silent, resend | 2 s without STATUS, SESSION_CONFIG resent every 1 s |
| Tile offline (website) | 5 s without TELEMETRY |
| Website | port 8080 on 0.0.0.0, pages poll every 250 ms, pump every 20 ms |

## Questions to practise before the demo

Answer each in your own words, without reading the code.

1. Work the 30 ms example by hand for the first 5 source frames and show where each tick boundary falls.
2. The Pico restarts in the middle of the demo. List what the website does, in order, until the wall is back.
3. Why does the website wait for STATUS after an ARM instead of trusting the ACK?
4. Why are the uploads parallel now, and what stops one dead tile from holding up the other eight?
