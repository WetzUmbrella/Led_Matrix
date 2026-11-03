# asset_store: design defence (T-B3-04, T-B3-05, T-B3-01)

Owner: Cheston (Buddy 3). Implemented by Ahsanul (B4) for Cheston: the heap probe (T-B3-01) in Ben's session (29 Oct 2026); the Asset Storage Task, RAM copy and upload path (T-B3-04, T-B3-05) in Jia Jun's session (29 to 30 Oct 2026), for Cheston to review, change where they disagree, and defend. Files: `panel_sync/main/asset_store.c/.h`, `panel_sync/main/asset_rx.c/.h`; host tests `panel_sync/test/test_asset_rx.c` ("Asset: Pass = 5 of 5"). Coding standard: Barr-C:2018, deviations D-1 (published pointer and phase word, PROVISIONAL extension for Ahsanul to confirm) and D-2.

**Names.** Public: `asset_store_start()`, `asset_store_probe_heap()`, `asset_store_post_upload()`, `asset_store_take_reply()`, `asset_store_active()`, `asset_store_get()`, `asset_store_cached()`, `asset_store_flash_allowed()`, `asset_store_phase()`. The plan's `asset_store_task()`, `asset_store_load_to_ram()` keep their names as statics; the plan's `asset_store_rx_chunk()` and `asset_store_verify()` are `as_on_chunk()` and `as_verify()`, with the pure rules in `asset_rx` (`asset_rx_check_manifest()`, `asset_rx_chunk()`, `asset_rx_crc32()`) so they run in the host tests.

## 1. One RAM block by the OD-04 rule, before ASSET_READY

- Source: VA-3.1, RAR-4, R2; PostTri Version A Buddy 3 "copy the validated asset into RAM before ASSET_READY, so flash leaves the 60 Hz path"; OD-04 rule from T-B3-01.
- Choice: after the CRC32 over the flash copy matches, one `heap_caps_malloc()` of the payload size, `MALLOC_CAP_INTERNAL` when the largest internal block is at least 90,368 B (57,600 B + 32,768 B margin, proposed), otherwise `MALLOC_CAP_SPIRAM`; the block is filled from flash and its CRC32 checked again before ASSET_READY. The log line `ASSET ready ... ram=<address> region=<internal|psram>` is acceptance (3).
- Examiner: "Why check the CRC32 twice, and what would a PSRAM copy cost the Display Engine per frame?"
- Review round 1 (R1-3): the rule compared the largest internal block with the fixed 90,368 B of a 600-frame asset, but manifests up to 1,023 frames (98,208 B) and the 115,200 B 8-bit asset are valid, and `CONFIG_SPIRAM` is off, so the PSRAM branch could never succeed and the upload ended in CRC_FAIL with no ASSET_READY. Now `as_ram_caps()` checks the real payload length plus the 32,768 B margin, at the MANIFEST before any erase and again for the copy; PSRAM is tried only when it is enabled. An asset that cannot fit is refused with NACK reason 1 and the ASSET_FORMAT bit (PROVISIONAL for Cheston: reason 1 reused rather than a new wire value). The boot probe's `HEAP largest_internal=` line (bench B0) gives the real headroom.

## 2. Published once, constant while it can be read

- Source: W6D-6a, W6D-6b, IFM-3b "pointer", SRM-3M; T-B3-04 "published once with release ordering and stays constant in ARMED and PLAYING".
- Choice: readers load an `_Atomic` pointer to an immutable descriptor (payload, length, frame_count, session, CRC32) with acquire; the Asset Storage Task stores it with release. Two descriptors alternate, and an upload first stores NULL, then frees the block; an upload is only accepted while `asset_store_flash_allowed()` is true, that is while no playback state can be reading the block.
- Examiner: "The Display Engine is reading frame 300 when a new manifest arrives. Show that it can never read freed memory."

## 3. Flash written only in the two upload states, through one gate

- Source: SRM-9M, SRM-9E, VA-3.4; T-B3-04 "every `esp_partition_write`, `esp_partition_erase_range` and `nvs_commit` path checks it"; T-B3-05 "written to the frames partition only in WAITING FOR ASSET or RECEIVING ASSET".
- Choice: `as_flash_erase()` and `as_flash_write()` are the only flash calls; both pass `as_write_allowed()` (upload phase and flash_allowed) and count writes, erases and refusals; `ASSET_FLASH` prints them after each verify and every 60 s, which is acceptance (2). The tile has no NVS write path while running (WiFi storage is RAM, T-B3-04).
- Examiner: "Prove that 10 minutes of PLAYING did zero flash writes. Which log line, and why can no code path bypass it?"

## 4. BUSY while playback can read the asset

- Source: T-B3-04 "A chunk or manifest arriving while the tile is PLAYING gets BUSY"; acceptance (1) 57 of 57 BUSY.
- Choice: BUSY in ARMED, PLAYING, PAUSED and CLOCK HOLDOVER (PROVISIONAL: the row names ARMED and PLAYING; PAUSED and holdover are added because RESUME_AT plays the same asset). The playback state is read through Playback Control's atomic copy. `busy=` counts them; `pc/uploader.py --expect-busy` sends the 57-chunk re-upload and counts replies.
- Examiner: "Why is PAUSED in your BUSY list when the row only names ARMED and PLAYING?"

## 5. Verify: format, length, session, CRC32 (VC-5)

- Source: T-B3-05 verify rules; VC-5; layout_table.md sections 5.4 and 5.5.
- Choice: at the MANIFEST: format_version 2 only (1 only in the `DE_PROFILE` build, OD-15), frame_count 1 to 1023, payload within the 113 chunk indices; otherwise NACK reason 0 or 1 and the ASSET_FORMAT condition bit (this module is its owner and clears it on the next valid manifest). Per chunk: wrong index or size is NACK reason 1 and the upload is dropped; another session is ignored. After the last chunk: CRC32 over the bytes read back from flash; a mismatch raises the CRC_FAIL event bit and sends no ASSET_READY.
- Examiner: "The 115,200 B VC-5 payload arrives. At which byte does your tile refuse it, and with which message?"

## 6. ACK every chunk by index, only missing ones resent

- Source: T-B3-05 "each chunk is ACKed by index and only missing chunks are requested again"; LDC-8.
- Choice: a 113-bit chunk map; a new chunk is written then ACKed, a repeat is ACKed again without a write (flash cannot be rewritten without an erase). The PC resends only what has no ACK.
- Examiner: "A CHUNK_ACK is lost. What does the PC send next, and what does the tile do with it?"

## 7. Cached asset across a reboot

- Source: T-B3-05 "REGISTER carries the cached session_id and CRC32 so a matching cached asset is reused".
- Choice: the MANIFEST is written as a 16 B header (magic, format, session, frame_count, CRC32 and its complement) in its own sector, inside RECEIVING ASSET; at boot a header whose payload CRC32 still matches is copied into RAM again with no flash write. A repeated manifest of the asset in RAM is answered with ASSET_READY, no upload.
- Examiner: "Power fails during chunk 30. What does the tile believe at the next boot?"

## 8. Erase stall during an upload

- Source: PostTri Version A Buddy 3 "a flash write stalls both cores through IPC (only IRAM-safe ISRs keep running)".
- Choice: the manifest erases about 16 sectors before the first chunk; this can only happen while not playing, and the tick ISR is IRAM-safe, but the Display Engine task can still miss ticks during the erase (shown as `missed=`). The uploader waits 1 s after the manifest (PROVISIONAL). T-B5-07 (c) sizes the stall.
- Examiner: "What does the wall show during an upload, and why is that acceptable?"

## PROVISIONAL list for Cheston to confirm

- OD-11: Upload Queue 4 (reject and NACK reason 3), Reply Queue 8 (drop-oldest).
- BUSY also in PAUSED and CLOCK HOLDOVER.
- Frames partition layout: 16 B header sector, payload from 4 KB.
- A wrong-length chunk drops the whole upload.
- D-1 extended to this module (Ahsanul to confirm).

## Questions for Cheston before Tribunal 2

1. Draw the life of one asset from MANIFEST to the Display Engine's first read, marking every point where the published pointer changes and the ordering that protects it.
2. Explain why a repeated chunk is ACKed but not written, using what you know about NOR flash.
3. Give the numbers behind the OD-04 decision on your bench tiles and say which region the RAM copy went to.
