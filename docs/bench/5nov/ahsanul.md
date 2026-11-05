# Bench 5 Nov 2026: Ahsanul (S1)

Build tag: bench-5nov-2 (6be4096). Net A = router "Matrix", no internet uplink, 2.4 GHz.
COM ports: Pico A COM3, tile 1 (9c:13:9e:f1:30:70) COM5, tile 2 (b8:f8:62:d8:09:f8) COM6.

## Found and fixed on the day
- playback_task waited pdMS_TO_TICKS(5) = 0 ticks at the 100 Hz FreeRTOS tick, so it spun at priority 15 on core 0, starved IDLE0 (task watchdog every 5 s) and tile_net (no WiFi, no JOIN). Fix: wait at least one tick (PB_WAIT_TICKS), commit 6be4096, tag bench-5nov-2. Host tests could not catch it; the task loop only runs on hardware.
- build_bench_images.sh only runs set-target when panel_sync/sdkconfig is missing. A stale sdkconfig with target esp32 gave a bootloader at 0x1000. Workaround: delete panel_sync/sdkconfig before building. Script fix still to do.
- Tile 5 (9c:13:9e:f1:2f:70) has no board number: it registers as node 0 and the Pico ignores it. Needs set_board_number before G6.

## B0 (log checks only, no analyser)
- Pico: COORDINATOR up, 10 beacons/s, online=0x003 sync=0x003 with tiles 1 and 2.
- Tiles: BCN at 10/s, TN_STATS dropped=0 foreign=0, display_engine test pin GPIO4 first tick printed.
- B0.5/B0.6 (analyser capture) not run: no analyser available. GPIO4 reachability not checked.

## D1/D2/D7 (Pico, 13 min, docs/week10/pico_lateness_10min.log)
- D1 PASS: 7901 beacons, all in the first LATE_HIST bin (under 1 ms); every bin at 5 ms and above is 0.
- D2 PASS: exc_max=0, send_fail=0, cb_max_us=29 to 32, dispatch_max_us=113 to 116.
- D7 PASS: one JOIN each for node 1 (14:16:48) and node 2 (14:16:56); online=0x003 to the end.

## D4 (docs/week10/beacon_spread_hook1.log, beacon_spread_hook2.log)
- Hook 1 (tile 1): received=7400 dropped=0 lost=211.
- Hook 2 (tile 2): received=7300 dropped=0 lost=210.
- Both tiles lose about 2.8% of beacons, and the same sequence numbers go missing on both, so the loss is the router dropping broadcasts, not the receive hook.
- tel_sent about 750 per tile, tel_acked=0 (no PC dashboard on the network). For Cheston (E tests).

## Not run today (moved to next session)
B, two-Pico test, G6, G4, H2, H3 (need an analyser or pico_tap), G5, G2.
