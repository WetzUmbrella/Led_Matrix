# SE15 handover (29 Oct 2026)

Written by Ahsanul (Buddy 4) at the end of Phase 1, so a teammate with no context can carry on. **Build on `b4/phase1`** (integration head, contains every other `b*/` branch). `b4/handover` = `b4/phase1` plus this file, the hook copy and the prompt copy.

## 1. State

| Branch | Base | Head | What it holds | Status |
|---|---|---|---|---|
| `b4/phase1` | `b4/wire-w10` | `7a98781` | **Integration head.** Every branch below, merged and gated | Phase 1 integrated; review round 1 partly fixed (section 4) |
| `b4/handover` | `b4/phase1` | (this commit) | This file, `docs/tools/commit-msg`, `docs/prompts/` | Done |
| `b4/wire-w10` | `master` | `95aaf07` | Week 10 skew-rig build (tile and Pico); contains all `b4/dep-*`, `b4/barr-foundation`, `b4/timesync-barr`, `b4/od10-sync-ready` | Done (build); not run on hardware |
| `b4/barr-foundation` | `master` | `17998b2` | Barr-C standard, checker, templates, CI job | Done |
| `b4/timesync-barr` | `b4/barr-foundation` | `74ff6cd` | T-B4-02 `timesync` to Barr-C | Done in code |
| `b4/od10-sync-ready` | `b4/timesync-barr` | `c53d11b` | OD-10 SYNC_READY rule, `timesync_next_tick()` | Done in code (OD-10 PROVISIONAL) |
| `b4/dep-common-proto` | `b4/barr-foundation` | `2475a2b` | First BEACON-only `common/proto` | Superseded by `b2/t-b2-01-common-proto` |
| `b4/dep-coordinator-loop` | `b4/dep-common-proto` | `2c6b3a3` | T-B2-02 poll loop, absolute 100 ms beacon | Partial (T-B2-02) |
| `b4/dep-tile-tasks` | `b4/barr-foundation` | `099e3ee` | T-B5-04 task table and sdkconfig | Done in code |
| `b4/dep-tile-net` | merge `58a6331` | `401aa8e` | T-B3-03 beacon receive, Time Sync Queue 4, OD-09 switch | Partial (T-B3-03) |
| `b4/dep-display-engine` | merge `07732d0` | `f64f04b` | T-B5-01 gptimer tick, GPIO4 pulse, test pattern | Done in code |
| `b4/status-overall` | `b4/wire-w10` | `d05e933` | `docs/status/overall.md` (Phase 0 audit + Phase 1 update) | Done |
| `b4/t-b4-03-bench-checklist` | `b4/wire-w10` | `e54e7d6` | `docs/week10/bench_checklist.md` (repo copy, Part B0 dry run, C1) | Done |
| `b4/t-b4-01-before-sizes` | `b4/wire-w10` | `527be9e` | S-1 to S-3 before sizes | Partial (T-B4-01: B-1 to B-3 need hardware) |
| `b5/t-b5-01-delete-render` | `b4/wire-w10` | `61ff2f7` | `render.c/.h` deleted | Done |
| `b2/t-b2-01-common-proto` | `b4/wire-w10` | `a7e9de8` | `protocol/layout_table.md`, all 18 rows, host tests | Done in code; widths PROVISIONAL |
| `b1/t-b1-01-proto-layout-py` | `b2/t-b2-01-common-proto` | `466bd14` | `pc/proto_layout.py`, `pc/gen_vectors.py`, `protocol/vectors/`, C/Python test | Done in code |
| `b3/t-b3-06-fault-word` | `b4/wire-w10` | `2b9a979` | `fault_word.c/.h`, host tests 5 of 5 | Partial (T-B3-06: telemetry send/ACK not done); review fixes open |
| `b5/t-b5-04-tile-tasks` | `b4/wire-w10` | `43c74ca` | boot log, `tile_tasks.c`, DEFENCE | Done in code; review fixes open |

Nothing is merged to `master`. Nothing on these branches was pushed before this handover.

**Last results (29 Oct 2026, on `b4/phase1` `7a98781`):**

| Check | Result |
|---|---|
| `clang-format --dry-run --Werror` (18.1.8) on the 30 scope files | exit 0 |
| `python3 tools/barr_check.py` | 30 file(s), 0 findings |
| `make -C panel_sync/test` and `asan` | Pass = 4 of 4; Fault word: Pass = 5 of 5 |
| `make -C common/proto/test` and `asan` | Pass = 7 of 7 |
| `python3 -m pytest -q tools/tests pc/tests` | 155 passed, 1 skipped (matplotlib not installed) |
| C/Python vectors | 57 vectors, 0 mismatches |
| Firmware builds (last run on `b4/phase1` before review fixes `180fba6`; fixes touched only host test code, Python and docs) | `panel_sync` and `-DTN_RX_HOOK=2`: complete, 0 warnings; `boardtest`: complete; Pico `ledwall_sync`: complete, text 341,852 B, bss 46,928 B |

Uncommitted: nothing.

## 2. Setup from zero (WSL Ubuntu 20.04)

```bash
sudo apt-get update && sudo apt-get install -y git wget flex bison gperf python3 python3-pip python3-venv cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0 gcc-arm-none-eabi libnewlib-arm-none-eabi build-essential pandoc
git clone https://github.com/WetzUmbrella/Led_Matrix.git ~/Led_Matrix
cd ~/Led_Matrix && git fetch --all --tags && git switch b4/phase1

# ESP-IDF v5.3, target esp32s3
mkdir -p ~/esp && cd ~/esp && git clone -b v5.3 --recursive https://github.com/espressif/esp-idf.git
cd ~/esp/esp-idf && ./install.sh esp32s3
. ~/esp/esp-idf/export.sh                 # in every new shell before idf.py

# Pico SDK 2.3.1 (the CI version)
mkdir -p ~/pico && cd ~/pico && git clone -b 2.3.1 --recursive https://github.com/raspberrypi/pico-sdk.git
export PICO_SDK_PATH=~/pico/pico-sdk

# Tools pinned as in CI (.github/workflows/build.yml)
python3 -m venv ~/se15-venv && ~/se15-venv/bin/pip install clang-format==18.1.8 pytest==8.3.3
```

Drive (project documents, read only): `sudo mount -t drvfs G: /mnt/g`; documents are in `/mnt/g/Other computers/My Computer/SIT/Year 2 Tri 1/INF2004 - Embedded Systems/Project/`. Never build in or write to it.

WiFi credentials (git-ignored): `cp panel_sync/main/wifi_secrets.example.h panel_sync/main/wifi_secrets.h` and `cp ledwall_sync/wifi_secrets.example.h ledwall_sync/wifi_secrets.h`, then edit.

Every check and build, from the repo root:

```bash
V=~/se15-venv/bin
$V/clang-format --dry-run --Werror $(grep -v '^#' tools/barr_scope.txt | grep -v '^$')
python3 tools/barr_check.py
$V/python -m pytest -q tools/tests pc/tests
make -C panel_sync/test && make -C panel_sync/test asan
make -C common/proto/test && make -C common/proto/test asan
(. ~/esp/esp-idf/export.sh; cd panel_sync && idf.py set-target esp32s3 && idf.py build && idf.py -B build2 -DTN_RX_HOOK=2 build)
(. ~/esp/esp-idf/export.sh; cd boardtest && idf.py set-target esp32s3 && idf.py build)
(mkdir -p ledwall_sync/build && cd ledwall_sync/build && cmake .. && make -j4)
python3 pc/gen_vectors.py          # only when the layout changes; commit protocol/vectors with it
```

## 3. Rules

- **Barr-C:2018 is mandatory** for team C: `docs/coding_standard.md`, deviations D-1 (C11 `<stdatomic.h>`) and D-2 (vendor names and types) only; a new deviation needs Ahsanul's approval. Add each new C file to `tools/barr_scope.txt` when it lands; `tools/barr_check.py` must report 0 findings. Python: PEP 8, type hints, pytest for every behaviour in its plan row.
- **PostTri constants and behaviour are frozen.** Change one only with Ahsanul's approval, logging the PostTri row it touches.
- **Wire format:** explicit shifts and masks, little-endian, from `protocol/layout_table.md`. No packed structs, no bit-fields on the wire.
- **Prototype code is deleted in the commit that lands its replacement** (plan section 0.1). Never two paths for one job.
- **No invented measurements.** Unmeasured values are `[NEEDS MEASUREMENT]` and get a bench step in `docs/week10/bench_checklist.md`.
- **Git:** one branch per plan task, `b<n>/<task-id>-<short-name>`; never push to `master`, never force-push, never rewrite pushed history, never move `cr1-before`.
- **Commit as yourself, with no Claude or AI attribution:** set `"includeCoAuthoredBy": false` in `~/.claude/settings.json`, and install the hook: `cp docs/tools/commit-msg .git/hooks/commit-msg && chmod +x .git/hooks/commit-msg`. Check after every commit: `git log -1 --format='%an <%ae>%n%B'`.
- Text: British English, metric units, no em dashes.

## 4. Next steps, in order

| # | Task | Owner (doer) | Depends on | Exact first action |
|---|---|---|---|---|
| 1 | Two-tile skew rig **dry run** (not yet done) | Ahsanul | none | Follow `docs/week10/bench_checklist.md` Part B0 on `b4/wire-w10` (or `b4/phase1`): power the Pico first, then reset the tiles |
| 2 | Finish review round 1 on `b3/t-b3-06-fault-word` | Cheston | none | Fix: plan quote `fault_set()` at `fault_word.h:50-51`; release/acquire only orders writes before the set, counters must be atomic (`fault_word.c:12-14`, DEFENCE :24); state how owner-only clearing is checked (DEFENCE :42); owner question 1 wording "first-time event" (:71); stale "need the TELEMETRY rows" (`fault_word.h:15`); label holdover "(proposed, W6D-2b)" (`fault_word.h:42`) |
| 3 | Finish review round 1 on `b5/t-b5-04-tile-tasks` | Jia Jun | none | `docs/tile_tasks_DEFENCE.md:12` core 1 also runs ipc1 and IDLE1; `:24` qualify "not delayed by the stall" (re-arm runs from flash); `:36` match `tile_tasks.c:38-44`; `tile_tasks.c:41` high-water type is `uint32_t` |
| 4 | Remaining doc NITs from round 1 | Ahsanul | none | `timesync_DEFENCE.md:110` cite VC-1 not VC-10 (on `b5/t-b5-01-delete-render`); `docs/b4/dependencies.md:38` and `RIG.md:36` still name `render.c`; `bench_checklist.md:5,7,15,20,78` (A1 to A4 now run, compiler name `xtensa-esp32s3-elf-gcc` as invoked, branch `b4/t-b4-01-before-sizes`, grep pattern escaping); `docs/week13/before/sizeof.txt:13-14` stale sentence; `docs/b4/status.md:13` old name |
| 5 | Rule 1.4.b decision | Ahsanul | none | Decide: parenthesise every function call used as an `&&`/`\|\|` operand, or add an interpretation in `docs/coding_standard.md` section 3. Long pre-existing test functions in `panel_sync/test/test_timesync.c:197,480` exceed 100 lines (rule 6.2.a) |
| 6 | Re-run both reviewers on `b4/phase1`, log the round in `docs/status/review_log.md` | Ahsanul | 2 to 5 | Loop until 0 BLOCKING and at most 3 SHOULD FIX, or 3 rounds |
| 7 | **IRAM budget** (before any display code) | Ahsanul with Jia Jun | none | Write `docs/week13/iram_budget.md` from the measured builds in section 4.1, then measure the RMT DMA (OD-16) cost once `display_engine` creates an RMT channel with DMA |
| 8 | T-B3-03 completion: Command Queue length 3 with NACK, 0.5 s heartbeat staggered by Node ID, `wifi_event` into `tile_net` | Cheston (Ahsanul) | T-B2-01 | Branch `b3/t-b3-03-tile-net` from `b4/phase1`; add `q_command` with `proto_encode_nack()` reason 2 |
| 9 | T-B2-02 completion: OD-01 citation into `coordinator_loop.c` header; remove packed `hello_t` (`ledwall_sync/main.c:42-46`) | Benjamin | T-B2-01 | Branch `b2/t-b2-02-coordinator-loop`; replace the HELLO receive with `proto_decode_register()` |
| 10 | T-B3-01 heap probe `asset_store_probe_heap()`, T-B3-02 flash check | Cheston | none | Branch `b3/t-b3-01-heap-probe`; add `asset_store.c` with the three `heap_caps` logs called after `IP_EVENT_STA_GOT_IP` |
| 11 | Start-up defect: `display_engine` takes its first tick before any beacon | Jia Jun with Ahsanul (T-B5-01, T-B4-04) | none | Wait for `timesync_sync_ready()` (or the first publication) before computing the first tick in `de_task` |
| 12 | Week 13 chain (critical path): T-B2-03 → T-B2-04 → T-B2-05 → T-B4-04 → T-B4-05, T-B4-06, T-X-07 | Benjamin, then Ahsanul | T-B2-02 | T-B2-03: `cl_registry[9]`, `CL_MAX_NODES 9`, delete `layout_for()`/`pack_wall()` |
| 13 | Parallel Week 13: T-B1-02 → T-B1-03 → T-B3-04 → T-B3-05 → T-B1-04 → T-X-01, T-X-04, T-X-08 → T-X-06 → T-B1-07; T-B5-02 → T-B5-03 → T-B5-07, T-X-03; T-B5-05; T-B2-06, T-B2-07; T-X-02, T-X-05 | per plan | per plan section 7 | T-B1-02 first: settle OD-06, then `pc/asset_builder.py` `resample_60fps()` with the 2, 2, 2, 2, 1 pytest |

### 4.1 IRAM measurements already taken (29 Oct 2026, `panel_sync` on `b4/phase1`, one build per option)

IRAM code = `.iram0.text` + vectors over all archives (the 16,384 B "IRAM" line of `idf.py size` is only the first region; the rest of the IRAM code sits in DIRAM `.text`, shared with the heap). Static DRAM = `.dram0.data` + `.bss`.

| Option added to `sdkconfig.defaults` | IRAM code (B) | Saved (B) | Static DRAM (B) | Cost to check |
|---|---|---|---|---|
| none (baseline) | 91,906 | 0 | 35,291 | |
| `CONFIG_ESP_WIFI_IRAM_OPT=n`, `CONFIG_ESP_WIFI_RX_IRAM_OPT=n` | 77,286 | 14,620 | 35,291 | WiFi RX latency and jitter, which feeds beacon timestamp spread |
| `CONFIG_FREERTOS_PLACE_FUNCTIONS_INTO_FLASH=y` | 79,697 | 12,209 | 35,291 | Scheduler calls stall during flash access; not usable from IRAM-safe ISRs while cache is off |
| `CONFIG_COMPILER_OPTIMIZATION_SIZE=y` | 82,970 | 8,936 | 34,880 | Speed of all code, including the tick path |
| `CONFIG_SPI_FLASH_ROM_IMPL=y` | 83,410 | 8,496 | 34,289 | ROM flash driver: fewer features and fixes |
| `CONFIG_HEAP_PLACE_FUNCTION_INTO_FLASH=y` | 84,468 | 7,438 | 35,291 | No heap calls from ISRs or with cache off |
| `CONFIG_RINGBUF_PLACE_FUNCTIONS_INTO_FLASH=y` | 91,136 | 770 | 35,291 | Ring buffers unusable with cache off |
| `CONFIG_PERIPH_CTRL_FUNC_IN_IRAM=n` | 91,906 | 0 | 35,291 | no effect measured |
| `CONFIG_GDMA_ISR_IRAM_SAFE=y`, `CONFIG_GDMA_CTRL_FUNC_IN_IRAM=y` | 91,906 | 0 | 35,291 | No change until RMT uses DMA (OD-16): measure then |
| `CONFIG_SPIRAM=y`, quad | 93,678 | -1,772 | 35,542 | Needed for the OD-04 PSRAM fallback; `CONFIG_SPIRAM` is currently off |

At the tag `cr1-before` the IRAM region was already 16,383 of 16,384 B, so the full region is normal. Free DIRAM after static use on `b4/phase1`: 229,069 B, which must hold the 90,368 B asset block (OD-04) after WiFi starts.

## 5. Open decisions

| OD | Owner | Status (value used) | Blocks |
|---|---|---|---|
| OD-01 Pico servicing mode | Benjamin | PROVISIONAL: poll mode coded; citation gap (no SDK sentence on where poll callbacks run; `layout_table.md` section 9) | T-B2-02 acceptance |
| OD-02 brightness enforcement | Jolyn, Jia Jun | open | T-B1-03, T-B1-05 |
| OD-03 flash size | Cheston | open; built `sdkconfig` 4 MB | T-B3-05 |
| OD-04 SRAM or PSRAM | Cheston | open (`CONFIG_SPIRAM` off) | T-B3-04 |
| OD-05 nibble order | Jolyn, Jia Jun | PROVISIONAL: option (a), vector `21 43 65` | T-B2-01 freeze, T-B1-03, T-B5-02 |
| OD-06 GIF delay rule | Jolyn | open | T-B1-02 |
| OD-07 field widths | Benjamin | PROVISIONAL: type 5, session_id 8, seq 16, Node ID 4, format_version 2, chunk index 7 (0 to 112), NACK reason 2, layout 2, policy 1; node_id in tile messages, ref_seq, acked_seq, frame_rate off the wire; 10 items in `layout_table.md` section 10 | T-B2-01 freeze, T-B2-04 (item 9: per-node CRC) |
| OD-08 loop bound | ruled Round 0 | settled: one routine per pass, 1 ms | none |
| OD-09 RX hook | Cheston, Ahsanul | PROVISIONAL: switch `TN_RX_HOOK`, default (a) lwIP callback | T-B3-03 acceptance |
| OD-10 SYNC_READY | Ahsanul | PROVISIONAL: window full, drift available, newest sample within 1 s holdover (coded) | T-B4-04 |
| OD-11 queue lengths | Cheston | PROVISIONAL: Time Sync 4 (coded), Command 3 (not coded); others open | T-B3-03, T-B3-06 |
| OD-12 fault bits | Cheston | PROVISIONAL: bits 0 to 8 WIFI, CRC_FAIL, QUEUE_OVERFLOW, BEACON_LOSS, IMU, MISSED_DEADLINE, ASSET_FORMAT, DISPLAY, SYNC_STALE (`fault_word.h`) | T-B3-06 |
| OD-13 re-upload rate | Jolyn, Benjamin | open | T-X-04 |
| OD-14 rejoin message | Benjamin, Ahsanul | open | T-B2-04 |
| OD-15 8-bit baseline | ruled Round 0; flag detail Ahsanul with Jia Jun | ruling settled; `DE_PROFILE` detail open | T-B5-02, T-X-03 |
| OD-16 RMT DMA or ISR | Jia Jun | open | T-B5-03 |
| OD-17 lecturer: pixels as application data | Ahsanul | open | T-X-06 |
| OD-18 heartbeat 2.5 s or 3 s | Benjamin | open (2.5 s primary) | T-B2-03 |
| OD-19 holdover beyond 1 s | Ahsanul | open | after T-B4-05 |
| OD-20 two-way exchange | Ahsanul, Benjamin | not triggered | T-B4-07 |
| OD-21 partial layouts | Jolyn, Benjamin | Round 0 ruling; 2-tile form open | T-X-07, T-X-09 |
| OD-22 thermal limits | Jia Jun | open | T-B5-08 |
| OD-23 WCET scope | Jia Jun | open | T-B1-07 |
| OD-24 Tribunal 2 date | Ahsanul | given as Mon 23 Nov 2026, to confirm on xSITe | T-B1-07 |
| Test pin (T-B5-01) | Jia Jun | PROVISIONAL guess: GPIO4 (`display_engine.h:48`); confirm it is broken out on the header | T-B4-03 wiring |
| Test-pin marker | Jia Jun | PROVISIONAL: 1 ms pulse, 4 ms every 64 ticks | T-B4-03 marker mode |

## 6. Hardware runs owed

All in `docs/week10/bench_checklist.md` (repo copy; the Drive copy is no longer maintained). Next three:

1. **Part B0 dry run** (two tiles, Pico, router, analyser): Ahsanul.
2. **C1 boot log** with `vTaskList()`: Jia Jun (Ahsanul supports).
3. **Part A steps A5 to A7** (heap at the tag, beacon and HELLO captures): Ahsanul (Benjamin supports).

## 7. Dates (to confirm on xSITe)

- Week 10: week of 2 Nov 2026.
- Week 13: week of 23 Nov 2026; Tribunal 2 on Mon 23 Nov 2026 (lecture slot); Final Run in the lab session that week.

## 8. How to resume with Claude Code

The full prompt is `docs/prompts/SE15_Complete_All_Prompt.md`. Paste it into a fresh Claude Code session opened in `~/Led_Matrix`, followed by this current-state note:

> Current state: Phase 1 is integrated on `b4/phase1` (it contains every other `b*/` branch); base all new work on it; read `docs/HANDOVER.md` first and continue from its section 4, step 2.
