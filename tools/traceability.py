#!/usr/bin/env python3
"""Requirement to test traceability: list it, run it, or write it out.

Usage (from anywhere in the repository; gcc, make and Python 3 with pytest
and pillow, as for tools/run_all_tests.sh):
    python3 tools/traceability.py --list       each requirement and its tests
    python3 tools/traceability.py FR14 NFR5    run those requirements' tests
    python3 tools/traceability.py all          run every requirement's tests
    python3 tools/traceability.py --markdown   write TRACEABILITY.md

REQUIREMENTS below is the one source of the map. Each requirement (docs/
requirements.md) names its host test cases and its system tests on the wall
(ST-01 to ST-14, docs/testing/TEST_REPORT.md section 4). A host case is a
(suite, selector) pair:
  * a C suite (proto, tile, pico, pico_tap) and the start of the case name
    the suite prints, for example ("pico", "C18"); "C18" does not select
    "C18b", because the next character must not be a letter or digit;
  * "py" and a pytest node id, a test file or one test in it.

Running a requirement builds and runs each C suite it needs once (make run),
runs its pytest node ids, then prints every selected case with its verdict.
The exit status is 1 when a selected case failed or a selector matched no
case, so a renamed test cannot drop out of the map unnoticed. Wall tests are
listed with their recorded verdict; they need the hardware and are not run.
"""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile
from typing import Dict, List, Sequence, Tuple

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_cases  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

#: C suite name -> directory with its Makefile.
SUITES = {
    "proto": "common/proto/test",
    "tile": "panel_sync/test",
    "pico": "ledwall_sync/test",
    "pico_tap": "tools/pico_tap/test",
}

#: Wall test -> (what it tests, recorded verdict, record).
WALL = {
    "ST-01": ("video sync between tiles, 240 fps camera", "PASS",
              "docs/testing/evidence/ST-01/"),
    "ST-02": ("tap to hit feedback, 39 taps", "PASS",
              "docs/testing/evidence/ST-02/"),
    "ST-03": ("scoring, one tapper, 9 tiles", "PASS",
              "docs/testing/evidence/ST-03/"),
    "ST-04": ("13 rounds back to back, no Pico restart", "PASS",
              "docs/testing/evidence/ST-04/"),
    "ST-05": ("6 min of TAP bursts, no freeze", "PASS",
              "docs/testing/evidence/ST-05/"),
    "ST-06": ("Pico restart during playback, no operator", "PASS",
              "docs/testing/evidence/ST-06/"),
    "ST-07": ("time sync through a WiFi stall", "PASS",
              "docs/testing/evidence/ST-07/"),
    "ST-08": ("whole-wall rotation, 10 board and 10 tile turns", "PASS",
              "docs/testing/evidence/ST-08/"),
    "ST-09": ("upload of the packed asset to 9 tiles", "PASS",
              "docs/testing/evidence/ST-09/"),
    "ST-10": ("setup time from power-on", "PASS",
              "docs/testing/evidence/ST-10/"),
    "ST-11": ("continue policy, tile unplugged during video", "PASS",
              "docs/testing/evidence/ST-11/"),
    "ST-12": ("pause policy, tile unplugged during video", "FAIL (open)",
              "docs/testing/evidence/ST-12/"),
    "ST-13": ("tile unplugged mid-round", "PARTLY (open)",
              "docs/testing/evidence/ST-13/"),
    "ST-14": ("whole demo run from the website", "PASS",
              "docs/testing/evidence/ST-14/"),
}

WS = "pc/tests/test_website_backend.py::"
WEB = "pc/tests/test_web.py::"
DASH = "pc/tests/test_dashboard.py::"
LAY = "pc/tests/test_layout_any.py::"
ASSET = "pc/tests/test_asset_builder.py::"
UP = "pc/tests/test_uploader.py::"

Case = Tuple[str, str]


def _c(suite: str, *names: str) -> List[Case]:
    """C cases of one suite."""
    return [(suite, name) for name in names]


def _py(*ids: str) -> List[Case]:
    """pytest node ids."""
    return [("py", node) for node in ids]


#: (ID, short title, host cases, wall tests, note on what is not covered).
REQUIREMENTS: List[Tuple[str, str, List[Case], List[str], str]] = [
    ("FR1", "Grid of 1 to 9 rows and columns, up to 9 tiles in any cells",
     _c("proto", "session config")
     + _c("pico", "C9", "C19c", "C30", "C31")
     + _py(LAY + "test_grid_grows_to_nine_by_nine_with_tiles_kept",
           LAY + "test_bad_shapes_refused",
           LAY + "test_sparse_grid_cuts_each_tile_its_own_section",
           WEB + "test_grid_passes_rows_and_cols",
           WEB + "test_grid_bad_fields_are_400"),
     ["ST-14"], ""),
    ("FR2", "Tap-to-place; placing a placed tile moves it",
     _py(LAY + "test_tap_to_place_writes_the_board_number",
         LAY + "test_tap_to_place_moves_a_tile_already_in_the_map",
         LAY + "test_armed_slot_waits_for_a_tap_then_expires",
         LAY + "test_place_refused_outside_the_layout_and_while_playing",
         WEB + "test_place_arms_the_slot",
         DASH + "test_next_tap_returns_the_board_number_of_a_tap"),
     ["ST-14"], ""),
    ("FR3", "Switch a tile off and on, or remove it",
     _c("pico", "C32")
     + _py(LAY + "test_slot_off_remembers_the_board_and_on_writes_it_back",
           LAY + "test_last_tile_cannot_be_switched_off",
           WS + "test_remove_frees_the_cell_unlike_off",
           WS + "test_placing_a_switched_off_tile_elsewhere_clears_its_old_"
           "cell",
           WEB + "test_slot_on_and_off"),
     [], "Used on the wall (19 Nov) but not run as a written system test."),
    ("FR4", "Video or game mode, switched only while stopped",
     _c("pico", "C9b", "C20", "C28")
     + _py(LAY + "test_mode_changes_only_while_stopped",
           WEB + "test_mode_refused_while_playing_and_bad_names",
           DASH + "test_mode_switch_keeps_the_old_mode_when_not_"
           "acknowledged"),
     ["ST-14"], ""),
    ("FR5", "GIF to one 4-bit 60 fps asset per tile, CRC32, parallel upload",
     _c("tile", "Test A1", "Test A2", "Test A3", "Test A4", "Test A5",
        "Test A6")
     + _py(ASSET + "test_30ms_frames_give_2_2_2_2_1",
           ASSET + "test_10s_gif_gives_600_frames",
           ASSET + "test_600_frame_asset_is_57600_bytes_with_zlib_crc",
           ASSET + "test_partition_2x2_cuts_four_regions",
           LAY + "test_centre_crop_drops_the_top_and_bottom_of_a_strip",
           LAY + "test_each_node_gets_its_own_region",
           UP + "test_57_chunks_last_256",
           UP + "test_ten_uploads_all_acked_crc_equal",
           WS + "test_parallel_uploads_send_every_tile_at_the_same_time"),
     ["ST-09"], ""),
    ("FR6", "Only the Pico decides readiness (ASSET_READY, CRC32, "
     "SYNC_READY)",
     _c("pico", "C6", "C7", "C8") + _c("tile", "Test P2", "OD-10 SYNC_READY")
     + _py(DASH + "test_start_disabled_until_bitmap_complete",
           WS + "test_play_needs_video_mode_a_gif_and_every_tile_ready"),
     ["ST-14"], ""),
    ("FR7", "START_AT or RESUME_AT 600 ms ahead, 3 copies; frame k on "
     "tick k",
     _c("pico", "C11", "C12")
     + _c("tile", "Test P1", "Test P3", "Test P4", "Test P5", "Test D3")
     + _py(WS + "test_pause_resume_and_stop_follow_the_pico_state"),
     ["ST-01", "ST-14"], ""),
    ("FR8", "Beacon every 100 ms; offset and drift from the fastest of 16; "
     "holdover",
     _c("pico", "C1", "C2")
     + _c("tile", "Test 1", "Test 2", "Test 3", "Test 4", "Test P6",
          "R1-2 re-seed", "24 Nov drift limit", "24 Nov WiFi stall",
          "24 Nov Pico restart"),
     ["ST-01", "ST-07"], ""),
    ("FR9", "REGISTER, heartbeat every 0.5 s, offline after 2.5 s, one "
     "coordinator",
     _c("pico", "C3", "C4", "C23")
     + _c("tile", "Test L1", "Test L2", "Test L3", "Test L4"),
     ["ST-06", "ST-11"], ""),
    ("FR10", "A game round needs no video",
     _c("pico", "C28")
     + _py(DASH + "test_layout_without_a_gif_is_sent_for_a_game"),
     ["ST-14"], ""),
    ("FR11", "Round states: armed, start sprite, 3-2-1, 30 s, results, "
     "abort",
     _c("pico", "C21", "C24", "C25") + _c("tile", "G3", "G4")
     + _py(DASH + "test_arm_sends_the_level_and_threshold",
           DASH + "test_start_now_only_while_armed",
           DASH + "test_abort_only_while_a_round_runs"),
     ["ST-04", "ST-14"],
     "Abort is used on the wall; its 500 ms was never timed."),
    ("FR12", "One mole at a time, never the same tile twice, level speeds",
     _c("pico", "C17"), ["ST-04"], ""),
    ("FR13", "Tap detection at 1 kHz, 300 mg default, settable; TAP resent "
     "until ACKed",
     _c("tile", "Test K1", "Test K2", "Test K3", "Test K4", "Test K5",
        "Test K6", "Test K7", "Test K8", "G6")
     + _c("pico", "C16", "C22", "C33"),
     ["ST-02", "ST-05"], ""),
    ("FR14", "TAPs within 30 ms are one tap, highest peak wins; hit +1, "
     "wrong 0",
     _c("pico", "C18", "C18b", "C18c", "C18d"), ["ST-03"], ""),
    ("FR15", "Results: hits, missed, wrong, accuracy, reaction, stars, best",
     _c("pico", "C27")
     + _py(WS + "test_best_score_per_level_and_new_best",
           DASH + "test_stars_follow_the_pico_rule",
           DASH + "test_hits_do_not_count_a_mole_still_up",
           DASH + "test_last_score_keeps_the_final_score_after_the_round_"
           "ends"),
     ["ST-14"], ""),
    ("FR16", "Tile visuals: mole, burst, red X, holes, countdown and score",
     _c("tile", "G1", "G4", "G5") + _c("pico", "C19", "C19b", "C21"),
     ["ST-14"],
     "Hit, wrong and miss differ by shape by design; the greyscale photo "
     "check was never done."),
    ("FR17", "Picture upright at 0, 90, 180, 270 degrees after 26 readings",
     _c("tile", "Test O1", "Test O3", "Test D2", "Test D4", "Test D5",
        "Test D6"),
     ["ST-08"], ""),
    ("FR18", "Website: round state, score, tile status, tap test, tap log, "
     "/score",
     _c("pico", "C10", "C26", "C29")
     + _py(WEB + "test_state_reports_grid_map_pico_state_mode_and_score",
           WS + "test_tiles_show_online_cell_on_off_and_orientation",
           WS + "test_pump_reads_status_telemetry_and_tap_peaks",
           WS + "test_every_tap_is_logged_with_its_tile_time",
           WS + "test_scoreboard_page_is_served_with_no_cdn"),
     ["ST-14"],
     "The 500 ms and 3 s display times were not timed on the wall."),
    ("FR19", "One 32-bit fault word; event bits cleared only on the PC's "
     "ACK",
     _c("tile", "Test F1", "Test F2", "Test F3", "Test F4", "Test F5",
        "Test F6", "Test T1", "Test T2", "Test T3", "Test T4", "Test T5",
        "Test O2")
     + _py("pc/tests/test_telemetry_ack.py::test_drop_every_third",
           "pc/tests/test_telemetry_ack.py::test_held_condition_has_zero_"
           "transitions_with_drops",
           "pc/tests/test_video_layouts.py::test_fault_bits_split_into_"
           "conditions_and_events"),
     [], "Faults show on the website in every run; no written system test."),
    ("FR20", "A lost tile is skipped mid-round; a bad asset is NACKed, never "
     "ready",
     _c("pico", "C14") + _c("tile", "Test A1", "Test A3")
     + _py("pc/tests/test_upload_faults.py::test_nack_reasons",
           "pc/tests/test_cross_lang.py::test_c_decoder_refuses_the_115200_"
           "payload_by_length"),
     ["ST-11", "ST-13"], ""),
    ("FR21", "Recover from a frozen or restarted Pico with no operator",
     _c("tile", "G7", "Playback 24 Nov") + _c("pico", "C15")
     + _py(WS + "test_a_silent_pico_shows_no_status_and_gets_the_layout_"
           "again",
           WS + "test_a_pico_back_from_silence_gets_a_fresh_publish",
           WS + "test_a_pico_back_gets_the_mode_only_once_ready",
           DASH + "test_a_restarted_link_goes_on_above_the_last_seq"),
     ["ST-06"], ""),
    ("NFR1", "Video skew at most 5 ms, always under 16.67 ms",
     _c("tile", "Test P3", "Test D3"), ["ST-01"],
     "Measured on pairs of tiles (4 to 9) for minutes, not a full 10 min "
     "run of every pair."),
    ("NFR2", "60 fps, frame n on tick n, no missed ticks in steady play",
     _c("tile", "Test P3", "Test D3"), ["ST-01", "ST-14"], ""),
    ("NFR3", "Hit burst after a tap: under 300 ms in the build",
     _c("tile", "Test K1", "G5") + _c("pico", "C18b"), ["ST-02"], ""),
    ("NFR4", "Taps detected, false taps, each hit scored once",
     _c("tile", "Test K1", "Test K3", "Test K7")
     + _c("pico", "C18", "C18c", "C18d"),
     ["ST-03"],
     "The formal 50-tap cards (detection rate, false taps) were never run."),
    ("NFR5", "Every channel at most 8/255; assets packed at 4 bits",
     _c("tile", "Test D1", "Test A2")
     + _py(ASSET + "test_every_nibble_at_most_8",
           ASSET + "test_brightness_above_8_refused",
           ASSET + "test_600_frame_asset_is_57600_bytes_with_zlib_crc",
           "pc/tests/test_check_assets.py",
           DASH + "test_brightness_9_40_255_refused"),
     [], ""),
    ("NFR6", "A whole-wall video reaches every tile within 60 s",
     _py(UP + "test_a_fast_tile_does_not_wait_out_the_ack_timeout",
         WS + "test_parallel_uploads_send_every_tile_at_the_same_time"),
     ["ST-09"], ""),
    ("NFR7", "Power-on to the first armed round in at most 3 minutes",
     [], ["ST-10"], ""),
    ("NFR8", "10 rounds without a restart; a lost tile does not stop the "
     "round",
     _c("pico", "C14", "C15"), ["ST-04", "ST-06", "ST-11", "ST-13"], ""),
    ("NFR9", "Pico loop: one routine per pass within 1 ms, beacon at most "
     "5 ms late",
     _c("pico", "C1", "C2", "C5", "C10"), [],
     "Beacon lateness on the hardware was measured for the October build, "
     "not for the final one."),
    ("NFR10", "A lost tile is noticed within 3 s",
     _c("pico", "C4"), ["ST-11"], ""),
    ("NFR11", "Own 2.4 GHz router; game and control messages unicast with "
     "ACK",
     _c("pico", "C16", "C21", "C22") + _c("tile", "G2")
     + _py(DASH + "test_send_game_control_is_resent_with_its_seq_until_"
           "acked"),
     ["ST-14"], ""),
    ("NFR12", "Binary, little-endian, fewest bits; C and Python agree",
     _c("proto", "bits", "beacon", "messages", "le helpers")
     + _py("pc/tests/test_cross_lang.py", "pc/tests/test_layout_table.py",
           "pc/tests/test_proto_layout.py"),
     [], ""),
    ("NFR13", "No lost update, no half-written read",
     _c("tile", "Test 4", "Test F5", "G6"), [],
     "Every C suite also runs under AddressSanitizer and "
     "UndefinedBehaviorSanitizer (make asan). The torn-read stress on the "
     "hardware was never run."),
    ("NFR14", "Asset played from internal RAM; it fits with a 32 KB margin",
     _c("tile", "Test A2"), [],
     "The fit is checked by the tile at boot (asset_store_probe_heap); RAM "
     "figures in docs/tribunal1/change_requirement/final_25nov/."),
    ("NFR15", "Wall, Pico STATUS and website show the same final score",
     _c("pico", "C21", "C27")
     + _py(DASH + "test_last_score_keeps_the_final_score_after_the_round_"
           "ends"),
     ["ST-03", "ST-14"], ""),
    ("NFR16", "Barr-C with 0 findings; every test and build passes",
     _py("tools/tests/test_barr_check.py"), [],
     "CI runs barr_check, clang-format, every suite and both firmware "
     "builds on every push."),
]


def requirement(req_id: str) -> Tuple[str, str, List[Case], List[str], str]:
    """The row of one requirement, case-insensitive."""
    for row in REQUIREMENTS:
        if row[0].lower() == req_id.lower():
            return row
    raise KeyError(req_id)


def selects(name: str, selector: str) -> bool:
    """True when a C case name starts with the selector as a whole token."""
    if not name.startswith(selector):
        return False
    rest = name[len(selector):]
    return not rest or not rest[0].isalnum()


def run_c(suite: str) -> List[Tuple[str, str]]:
    """Build and run one C suite; (case name, verdict) for every case."""
    result = subprocess.run(["make", "-C", SUITES[suite], "run"], cwd=ROOT,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, check=False)
    with tempfile.NamedTemporaryFile("w", suffix=".log", delete=False,
                                     encoding="utf-8") as handle:
        handle.write(result.stdout)
        path = handle.name
    try:
        return test_cases.c_cases(path)
    finally:
        os.remove(path)


def run_py(node_ids: Sequence[str]) -> Dict[str, str]:
    """Run pytest node ids; test id (file::name) -> verdict."""
    if not node_ids:
        return {}
    result = subprocess.run([sys.executable, "-m", "pytest", "-v", "-p",
                             "no:cacheprovider"] + list(node_ids), cwd=ROOT,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, check=False)
    verdicts: Dict[str, str] = {}
    for line in result.stdout.splitlines():
        match = test_cases.PYTEST.match(line)
        if match:
            verdicts[match.group(1) + "::" + match.group(2)] = match.group(3)
    return verdicts


def run(req_ids: Sequence[str]) -> int:
    """Run the host tests of the requirements; 1 when any failed."""
    rows = [requirement(req_id) for req_id in req_ids]
    suites = sorted({suite for row in rows for suite, _ in row[2]
                     if suite != "py"})
    c_results = {suite: run_c(suite) for suite in suites}
    py_ids = sorted({node for row in rows for suite, node in row[2]
                     if suite == "py"})
    py_results = run_py(py_ids)
    failed = 0

    for req_id, title, cases, wall, note in rows:
        print("%s  %s" % (req_id, title))
        passed = total = 0
        for suite, selector in cases:
            if suite == "py":
                found = [(test, verdict) for test, verdict
                         in py_results.items()
                         if test == selector
                         or test.startswith((selector + "::",
                                             selector + "["))]
            else:
                found = [(name, verdict) for name, verdict
                         in c_results[suite] if selects(name, selector)]
            if not found:
                print("  MISSING  %s: no case matches %r" % (suite, selector))
                failed += 1
                continue
            for name, verdict in found:
                total += 1
                good = verdict in ("PASS", "PASSED")
                passed += 1 if good else 0
                failed += 0 if good or verdict == "SKIPPED" else 1
                print("  %-7s  %s: %s" % (verdict, suite, name))
        for test in wall:
            what, verdict, record = WALL[test]
            print("  wall     %s %s: %s (%s)" % (test, what, verdict, record))
        if note:
            print("  note     %s" % note)
        print("  %d of %d host cases pass\n" % (passed, total))

    print("%s" % ("all selected cases pass" if not failed
                  else "%d failed or missing" % failed))
    return 1 if failed else 0


def describe(case: Case) -> str:
    """One host case as a short label."""
    suite, selector = case
    if suite == "py":
        return selector.replace("pc/tests/", "").replace("tools/tests/", "")
    return "%s %s" % (suite, selector.replace("Test ", ""))


def listing() -> str:
    """Plain-text map, one block per requirement."""
    lines = []
    for req_id, title, cases, wall, note in REQUIREMENTS:
        lines.append("%-6s %s" % (req_id, title))
        lines.append("       host: %s" % (
            ", ".join(describe(case) for case in cases) or "none"))
        lines.append("       wall: %s" % (", ".join(wall) or "none"))
        if note:
            lines.append("       note: %s" % note)
    return "\n".join(lines)


def markdown() -> str:
    """docs/testing/TRACEABILITY.md."""
    lines = ["# Requirement to test traceability", "",
             "Generated by `python3 tools/traceability.py --markdown` from "
             "the map in that file; do not edit by hand. Requirements: "
             "[`docs/requirements.md`](../requirements.md). Host cases run "
             "on the PC (the real C sources with fake clocks and networks, "
             "and pytest); wall tests are the system tests of "
             "[`TEST_REPORT.md`](TEST_REPORT.md) section 4.", "",
             "Run the host tests of one requirement, for example FR14:", "",
             "```", "python3 tools/traceability.py FR14", "```", "",
             "| Req | Requirement | Host test cases | Wall tests | Not "
             "covered |", "|---|---|---|---|---|"]
    for req_id, title, cases, wall, note in REQUIREMENTS:
        host = ", ".join("`%s`" % describe(case) for case in cases) or "none"
        walls = ", ".join("%s (%s)" % (test, WALL[test][1])
                          for test in wall) or "none"
        lines.append("| %s | %s | %s | %s | %s |" % (
            req_id, title, host, walls, note or ""))
    lines += ["", "## Wall tests", "",
              "| ID | What | Verdict | Record |", "|---|---|---|---|"]
    lines += ["| %s | %s | %s | `%s` |" % (test, what, verdict, record)
              for test, (what, verdict, record) in WALL.items()]
    lines.append("")
    return "\n".join(lines)


def main(argv: List[str]) -> int:
    """Command line; see the module docstring."""
    if not argv or argv[0] in ("-h", "--help"):
        print("\n".join(__doc__.strip().splitlines()[2:8]))
        return 0 if argv else 2
    if argv[0] == "--list":
        print(listing())
        return 0
    if argv[0] == "--markdown":
        path = os.path.join(ROOT, "docs", "testing", "TRACEABILITY.md")
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(markdown())
        print("wrote %s" % os.path.relpath(path, ROOT))
        return 0
    ids = [row[0] for row in REQUIREMENTS] if argv == ["all"] else argv
    try:
        return run(ids)
    except KeyError as error:
        print("unknown requirement %s; see --list" % error, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
