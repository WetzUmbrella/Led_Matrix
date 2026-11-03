# Final soak, 5 tiles for 30 min (T-X-09)

Owner: Cheston (Buddy 3), all buddies support. Plan row T-X-09: "Five tiles play a PC-built GIF for 30 min (A2 duration) in a partial 3x3 layout with the unassigned positions blank (OD-21); logs skew (one analyser channel per tile), beacon loss, heartbeat loss, reconnects, missed deadlines and faults." Pass: "Maximum skew over all tile pairs at most 5 ms; 0 missed deadlines; 0 false drop-outs. Pass = 3 of 3." Target: Final. Nothing has run yet.

## Before the run

- Builds: tiles and Pico from the same commit; write the hash here. **One Pico on the router** (`docs/week10/skew_2tile/bench_log.md`).
- Layout: 3x3 with five Node IDs and four positions blank, decided with Jolyn and Benjamin under OD-21; record the map here.
- Analyser: one channel per tile test pin (GPIO4, PROVISIONAL), all grounds joined, 1 MS/s, 30 min (check the analyser can hold 1,800 s; otherwise run three 10 min captures back to back and say so).
- Logs: the Pico log, five tile logs, the dashboard log (`telemetry` rows), each with `tee`.

## Run

1. Dashboard: `config`, `build`, `upload`, `status` until Start is enabled, `start`.
2. Start the capture and the clock; do not touch the bench for 30 min.
3. `stop` at 30 min.

## Results

| Measure | Source | Value | Pass |
|---|---|---|---|
| Maximum skew over all 10 tile pairs | `skew_analyse.py --marker` per pair | | at most 5 ms |
| Missed deadlines | `DE ... missed=` change on each tile | | 0 |
| False drop-outs | Pico `CL_EVENT ... LEAVE` with the tile still powered | | 0 |
| Beacon loss | tile `TN_STATS lost=` change | | recorded |
| Heartbeat loss | Pico `CL_STATS` | | recorded |
| Reconnects | tile `TN_COMMS RECONNECTING` | | recorded |
| Faults | dashboard telemetry fault names | | recorded |
