// timesync (T-B4-02): Time Sync Task and the clock interface used by
// Playback Control (T-B4-04) and the Display Engine (T-B5-01).
//
// The filter, drift, slew, seqlock and frame arithmetic are in
// timesync_core.h (pure C, host-tested). This header adds the (t_p, t_l) pair
// that tile_net posts and the FreeRTOS task. It includes no FreeRTOS header,
// so tile_net and the host tests can include it.
//
// Reader functions (from timesync_core.h), each reading only through
// ts_read_snapshot() (RAR-3). Each reading task owns one ts_reader_t.
// Proposed interface, to be agreed with Jia Jun (T-B5-01); see
// timesync_DEFENCE.md, "Open interface items":
//   int64_t  now_corrected(ts_reader_t *r);
//   int64_t  deadline_local(ts_reader_t *r, int64_t origin_us, uint64_t n);
//   uint32_t frame_index(int64_t t_us, int64_t origin_us, uint32_t frame_count);
//   bool     timesync_sync_ready(void);
// Never call them from an ISR (PostTri SRM row "Corrected Pico time").
#pragma once

#include <stdint.h>
#include "timesync_core.h"

// One beacon observation, posted by tile_net (T-B3-03) to the Time Sync Queue.
// t_p: Pico time carried in the beacon, microseconds.
// t_l: local esp_timer_get_time() at the receive hook chosen in OD-09.
typedef struct {
    int64_t t_p;
    int64_t t_l;
} ts_pair_t;

// SYNC_READY rule: open decision OD-10 (Ahsanul, before T-B4-04). Record the
// rule here once it is decided. Until then timesync_sync_ready() is a stub.

// Time Sync Task (priority 16 on core 0, (proposed) W6D-4a, created by T-B5-04).
// pvParameters is the QueueHandle_t of q_timesync, created by tile_net in
// T-B3-03 with item size sizeof(ts_pair_t). The task is the single writer of
// the published clock.
void timesync_task(void *q_timesync);

// Proposed (to be agreed with Jia Jun, T-B5-01): called by the Display
// Engine task (de_task, not the ISR) once per tick, so the Time Sync Task
// runs one slew step per tick. Safe before the task starts.
void timesync_notify_tick(void);
