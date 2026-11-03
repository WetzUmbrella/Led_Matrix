# IRAM budget (HANDOVER step 7)

Owner: Ahsanul (B4) with Jia Jun (B5). Written 29 Oct 2026 in Ben's (B2) session from the measurements Ahsanul took on 29 Oct 2026 (`docs/HANDOVER.md` section 4.1: `panel_sync` on `b4/phase1`, ESP-IDF v5.3, one build per option). Nothing here is a new measurement.

## Why this matters

PostTri SRM-3M and VA-5.2 need the tick and RMT interrupts IRAM-safe, so they keep running while a flash operation disables the cache. Every IRAM-safe function lives in internal RAM. On the ESP32-S3 the first 16,384 B IRAM region is already full at the tag (`cr1-before`: 16,383 of 16,384 B), and the rest of the IRAM code sits in DIRAM `.text`, which shares internal RAM with the heap. The heap must still hold the 90,368 B RAM asset copy (OD-04, T-B3-01 rule: 57,600 B payload plus the 32,768 B margin, proposed, RAR-4) after WiFi starts. So IRAM code and the asset copy compete for the same memory.

## Measured baseline and options (from HANDOVER section 4.1)

IRAM code = `.iram0.text` + vectors over all archives. Static DRAM = `.dram0.data` + `.bss`.

| Option added to `sdkconfig.defaults` | IRAM code (B) | Saved (B) | Static DRAM (B) | Cost to check before adopting |
|---|---|---|---|---|
| none (baseline) | 91,906 | 0 | 35,291 | |
| `CONFIG_ESP_WIFI_IRAM_OPT=n`, `CONFIG_ESP_WIFI_RX_IRAM_OPT=n` | 77,286 | 14,620 | 35,291 | WiFi RX latency and jitter, which widens the beacon timestamp spread (OD-09 log) |
| `CONFIG_FREERTOS_PLACE_FUNCTIONS_INTO_FLASH=y` | 79,697 | 12,209 | 35,291 | scheduler calls stall during flash access; not usable from IRAM-safe ISRs while the cache is off |
| `CONFIG_COMPILER_OPTIMIZATION_SIZE=y` | 82,970 | 8,936 | 34,880 | speed of all code, including the tick path |
| `CONFIG_SPI_FLASH_ROM_IMPL=y` | 83,410 | 8,496 | 34,289 | ROM flash driver: fewer features and fixes |
| `CONFIG_HEAP_PLACE_FUNCTION_INTO_FLASH=y` | 84,468 | 7,438 | 35,291 | no heap calls from ISRs or with the cache off |
| `CONFIG_RINGBUF_PLACE_FUNCTIONS_INTO_FLASH=y` | 91,136 | 770 | 35,291 | ring buffers unusable with the cache off |
| `CONFIG_PERIPH_CTRL_FUNC_IN_IRAM=n` | 91,906 | 0 | 35,291 | no effect measured |
| `CONFIG_GDMA_ISR_IRAM_SAFE=y`, `CONFIG_GDMA_CTRL_FUNC_IN_IRAM=y` | 91,906 | 0 | 35,291 | no change until RMT uses DMA (OD-16): measure then |
| `CONFIG_SPIRAM=y`, quad | 93,678 | -1,772 | 35,542 | needed only for the OD-04 PSRAM fallback; `CONFIG_SPIRAM` is off today |

Free DIRAM after static use on `b4/phase1`: 229,069 B (static figure, before WiFi and task stacks allocate at run time).

## Rules for the Week 13 build (proposed, for Ahsanul and Jia Jun to agree)

1. **No option is adopted yet.** The baseline leaves 229,069 B of DIRAM after static use; whether 90,368 B is still free as one block after WiFi starts is exactly what T-B3-01 measures on the tile (`HEAP largest_internal=` line, 5 boots on each of 2 tiles). Decide only from that number.
2. If T-B3-01's minimum largest internal block is at least 90,368 B, keep the baseline and record the margin left.
3. If it is below, the first choice is the OD-04 PSRAM fallback (T-B3-04 already plans `MALLOC_CAP_SPIRAM`), not an IRAM option: every option in the table trades away timing or ISR safety that PostTri committed to.
4. Re-measure this table when T-B5-03 adds the RMT DMA or IRAM-safe RMT ISR (OD-16), because both add IRAM code.

## Still to measure

- `[NEEDS MEASUREMENT]` Largest internal block after WiFi, T-B3-01 (bench step A5 is the before value; the after value comes from the T-B3-01 probe now in `asset_store.c`).
- `[NEEDS MEASUREMENT]` IRAM cost of the OD-16 choice, once `display_engine` creates the RMT channel with DMA or an IRAM-safe ISR.
