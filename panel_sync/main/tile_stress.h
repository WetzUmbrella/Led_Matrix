/**
 * @file    tile_stress.h
 * @brief   Stress test builds T-X-02 (FAULT_STRESS) and T-X-05 (TS_STRESS).
 *          Neither flag is set in the shipped firmware; with one set,
 *          app_main starts these tasks instead of the normal tile.
 *
 * Owners: T-X-02 Cheston (Buddy 3), T-X-05 Ahsanul (Buddy 4). Implemented by
 * Ahsanul (B4) for them, written 30 Oct 2026 in Jia Jun's (B5) session.
 *
 * FAULT_STRESS (T-X-02): one setter on core 0 and one on core 1, each with
 * its own event bit (CRC_FAIL, MISSED_DEADLINE), set it with
 * fault_word_set() TILE_STRESS_SETS times; before each re-set the setter
 * waits until its bit is cleared (VC-4 merge rule). A reporter on core 0
 * snapshots, "sends" and clears the snapshot's event bits on a simulated
 * ACK, and counts per bit the snapshots that hold it. Log:
 * "FAULT_STRESS bit=<b> sets=<n> seen=<n> lost=<n> max_wait_us=<n>".
 * PROVISIONAL (owner Cheston): the reporter is a test task at the Network
 * Task's priority and core, not the Network Task itself.
 *
 * TS_STRESS (T-X-05): a writer on core 0 publishes snapshots whose offset,
 * drift and reference all come from one counter, as fast as the scheduler
 * allows; reader A on core 0 (Playback Control's priority) and reader B on
 * core 1 (the Display Engine's priority) read TILE_STRESS_READS snapshots
 * each and count torn ones (fields from different writes). Log:
 * "TS_STRESS reader=<A|B> reads=<n> torn=<n> stale=<n>".
 * PROVISIONAL (owner Ahsanul): the readers are test tasks at the priorities
 * and cores of Playback Control and the Display Engine.
 *
 * Every busy loop sleeps one tick every TILE_STRESS_YIELD_EVERY passes so
 * the idle tasks run and the task watchdog stays quiet.
 *
 * DEVIATION D-2 (module-wide): FreeRTOS names and types.
 */
#ifndef TILE_STRESS_H
#define TILE_STRESS_H

#include <stdbool.h>
#include <stdint.h>

/** Sets per setter (T-X-02 row: 1,000,000). */
#define TILE_STRESS_SETS 1000000u

/** Reads per reader (T-X-05 row: at least 1,000,000). */
#define TILE_STRESS_READS 1000000u

/** Passes between one-tick sleeps (plan choice). */
#define TILE_STRESS_YIELD_EVERY 1000u

/**
 * @brief  Start the stress tasks of the build flag that is set.
 * @return true when a stress build is selected and its tasks exist; false
 *         in the shipped firmware (no flag), so app_main starts the tile.
 */
bool tile_stress_start (void);

#endif /* TILE_STRESS_H */

/*** end of file ***/
