# Buddy 4 dependency code (minimal implementations for other buddies' modules)

Owner of this record: Ahsanul (Buddy 4). Written 29 Oct 2026.

Buddy 4's tasks need parts of four other buddies' modules before the Week 10 two-tile skew rig (T-B4-03) can run. Each part below is the minimum T-B4-03 needs, written in the real module file named by the plan, with Barr-C:2018 (`docs/coding_standard.md`) and, where the logic is pure, host tests. Each file starts with "Minimal implementation by Buddy 4 for T-B4-xx. Owner: ..., task ... Implemented: ... Not yet implemented: ...". The owner extends the code; nothing here is final until the owner reviews it.

## How to review

Each dependency is on its own branch `b4/dep-<module>`. Review only the commits after the listed base:

```
git diff <base>..<branch>
```

| Branch | Base for review | Why that base |
|---|---|---|
| `b4/dep-common-proto` | `b4/barr-foundation` | Barr-C tooling only |
| `b4/dep-coordinator-loop` | `b4/dep-common-proto` | needs the beacon encoder |
| `b4/dep-tile-tasks` | `b4/barr-foundation` | Barr-C tooling only |
| `b4/dep-tile-net` | `58a6331` (merge of proto, tile_tasks and timesync) | needs the decoder, the task table and `timesync_pair_t` |
| `b4/dep-display-engine` | `07732d0` (merge of tile_net and timesync) | needs the task table and the timesync reader |

`b4/wire-w10` merges all of them and wires them into `panel_sync.c` and `ledwall_sync/main.c`.

## Dependencies

| Module | Owner | Plan task it partly covers | Implemented | Left for the owner | PROVISIONAL choices |
|---|---|---|---|---|---|
| `common/proto` (`proto.c/.h`, `test/`) | Benjamin | T-B2-01 (BEACON row only) | `proto_put_bits()`, `proto_get_bits()`, common header {type, session_id, seq}, `proto_encode_beacon()`, `proto_decode_beacon()`, `proto_beacon_set_t_p()`; host tests: 512 width and offset round trips, reference bytes computed independently in Python, T-B2-01 boundary set (t_p 0 and 2^64 - 1, session_id and seq 0 and maximum), rejections; ESP-IDF component through `EXTRA_COMPONENT_DIRS`; CI job runs the tests | `protocol/layout_table.md`; the byte-aligned `proto_put_u16_le/u32_le/u64_le` and `proto_get_*_le` helpers; every other message row; the Python twin and test vectors (T-B1-01); the OD-01 citation in `layout_table.md` | OD-07 widths from the decision-brief draft: type 5, session_id 8, seq 16 bits; type codes in T-B2-01 row order (BEACON = 10); contiguous packing, 93 bits in 12 bytes. File names `proto.c/.h` instead of the plan's `proto_layout.c/.h`, so that the plan's function names carry their module prefix (Barr-C 6.1.i). |
| `coordinator_loop` (`ledwall_sync/coordinator_loop.c/.h`) | Benjamin | T-B2-02 | `coordinator_loop_run()`: `cyw43_arch_poll()` every pass, beacon on `next_beacon_us += 100,000`; `cl_send_beacon()`: beacon encoded first, then t_p = `time_us_64()` and `proto_beacon_set_t_p()` immediately before `udp_sendto`; lateness histogram (1 ms bins, 10 ms+ bin) printed every 100 beacons; one `BEACON seq= t_p= late_us=` line per beacon for the seq check; CMake links `pico_cyw43_arch_lwip_poll` (in `b4/wire-w10`, under review) | OD-01 citation in the header and `layout_table.md`; registry, heartbeat scan, router join and router-loss rejoin (T-B2-03, `cl_connect_router()`); sessions (T-B2-04); commands (T-B2-05); `CL_TEST_SUPPRESS_MS` (T-B4-05) | Poll mode (OD-01, first option); session_id 0 until T-B2-04; the plan's `cl_lateness_hist[]` is the static `g_lateness_hist[]` read through `coordinator_loop_lateness_bin()` (Barr-C 7.1.j, 4.2.c). |
| `tile_tasks.h` and `sdkconfig.defaults` | Jia Jun | T-B5-04 | Six task rows: Display 20 and Orientation 5 on core 1; Time Sync 16, Playback 15, Network 12, Asset Storage 8 on core 0; the six sdkconfig options of the row (tcpip on core 0, gptimer and RMT ISRs IRAM-safe, trace facility, stats formatting, core ID in `vTaskList()`), PM off | Boot log of `vTaskList()`; recorded CPU frequency and cycle-counter wrap time; the other tasks moving to their modules | Stack 4096 B for every task (the prototype's value) until high-water marks are measured. |
| `tile_net` (`panel_sync/main/tile_net.c/.h`) | Cheston | T-B3-03 | `tile_net_start()`, `tile_net_task()`; BEACON decode with `proto_decode_beacon()`; t_l = `esp_timer_get_time()` at the receive hook; (t_p, t_l) to `q_timesync` by drop-oldest with a drop counter; received, dropped and lost (wrap-aware seq gap) counters; `BCN,<seq>,<t_p>,<t_l>` spread log per beacon and a counter line every 100 beacons | WiFi start-up and `wifi_event` (still in `panel_sync.c`); heartbeat and Node ID stagger; Command Queue `q_command` and its NACK (Buddy 4 adds the minimum with T-B4-04); session_id and stale-seq rejection; every other message | OD-09 hook as a build switch: default (a) lwIP `udp_recv` in the tcpip task, (b) `idf.py -DTN_RX_HOOK=2 build` for `recvfrom` in the Network Task; OD-11 Time Sync Queue length 4. |
| `display_engine` (`panel_sync/main/display_engine.c/.h`) | Jia Jun | T-B5-01 | `display_engine_start()`; `de_task()` on core 1 at priority 20; gptimer created and `de_tick_isr()` (IRAM) registered from `de_task`; tick n armed as a one-shot at `timesync_deadline_local(origin, n)`, computed from n each frame; test pin pulse per tick; missed-tick counter; `timesync_notify_tick()` once per tick from `de_task`; `DE_W10_TEST_PATTERN` (LED n mod 64 at value 8) | Frame buffers, swap, RMT start and preparation order (T-B5-02, T-B5-03); asset playback; orientation; frame-timing histogram (T-B5-07); deleting `render.c` | Test pin GPIO4 (to be confirmed on the board header); marker pulse 1 ms, 4 ms when n mod 64 == 0 (RIG.md section 4b); origin 0 in Pico time for the rig build. |

## Not dependencies, but changed by the wiring (under review)

- `panel_sync/main/panel_sync.c` (prototype, no Barr-C scope): `app_main` keeps `wifi_init()` and then starts `tile_net`, the Time Sync Task at priority 16 on core 0 and `display_engine`. Removed because every caller is replaced: `sync_rx_task`, `hello_task`, the packed `beacon_t` and `hello_t`, `lock`, `get_render_state`, `FPS`, and `render_start()`.
- `panel_sync/main/CMakeLists.txt`: `render.c` and `frames.c` are no longer built (T-B5-01: "the old 30 fps GIF path is not built"); the files stay in the repository for Jia Jun.
- `ledwall_sync/main.c` (prototype): the beacon loop, `beacon_t`, `FPS`, `CMD_PLAY`, `SYNC_MAGIC`, `BEACON_MS` and `expire_nodes()` are removed; `main()` calls `coordinator_loop_run()`. The router join and the HELLO receive stay for T-B2-03.
- `ledwall_sync/CMakeLists.txt`: adds `coordinator_loop.c` and `../common/proto/proto.c`, links `pico_cyw43_arch_lwip_poll`.

## Builds checked

- Tile, ESP-IDF v5.3, fresh `sdkconfig`: builds with no warnings in the new files, both OD-09 variants.
- Pico, SDK 2.3.1: builds with no warnings.
- Host tests: `make -C panel_sync/test` (4 of 4 plus OD-10 and supplementary), `make -C common/proto/test` (3 of 3), both also under `make asan`.
- Nothing has run on hardware.
