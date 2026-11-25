"""Tests for tools/test_cases.py: case names and verdicts are read from C
suite logs (skipping the coordinator's own log lines) and from a verbose
pytest log, a failure makes the exit status 1, and a missing log is
reported. Run: pytest tools/tests"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import test_cases  # noqa: E402

PROTO_LOG = """\
make: Entering directory '/x/common/proto/test'
./test_proto
bits: put/get round trip, widths 1 to 64
  PASS
beacon: short buffer rejected
  PASS
Pass = 2 of 2
"""

PICO_LOG = """\
./test_coordinator
C1 T-B2-02: absolute 100 ms schedule
CL_EVENT t_ms=1200 STATE node=0 value=6
CL_STATS state=4 sent=12
    lateness 0 us
  PASS
C2 T-B2-02: a stall skips whole slots
LATE_HIST,1,0,0
  FAIL
Coordinator: Pass = 1 of 2
"""

PYTEST_LOG = """\
pc/tests/test_a.py::test_one PASSED                                  [ 33%]
pc/tests/test_a.py::test_two[1 2] PASSED                             [ 66%]
tools/tests/test_b.py::test_three SKIPPED (no plot)                  [100%]
"""


def write_logs(folder, pico_log=PICO_LOG):  # type: ignore[no-untyped-def]
    """The five logs test_cases.py reads."""
    logs = {"common_proto": PROTO_LOG, "panel_sync": PROTO_LOG,
            "ledwall_sync": pico_log, "tools_pico_tap": PROTO_LOG,
            "pytest": PYTEST_LOG}
    for name, text in logs.items():
        (folder / (name + ".log")).write_text(text)


def test_c_cases_skip_make_and_coordinator_lines(tmp_path):  # type: ignore
    write_logs(tmp_path)
    assert test_cases.c_cases(str(tmp_path / "common_proto.log")) == [
        ("bits: put/get round trip, widths 1 to 64", "PASS"),
        ("beacon: short buffer rejected", "PASS")]
    assert test_cases.c_cases(str(tmp_path / "ledwall_sync.log")) == [
        ("C1 T-B2-02: absolute 100 ms schedule", "PASS"),
        ("C2 T-B2-02: a stall skips whole slots", "FAIL")]


def test_pytest_ids_may_hold_spaces(tmp_path):  # type: ignore
    write_logs(tmp_path)
    assert test_cases.py_cases(str(tmp_path / "pytest.log")) == {
        "pc/tests/test_a.py": [("test_one", "PASSED"),
                               ("test_two[1 2]", "PASSED")],
        "tools/tests/test_b.py": [("test_three", "SKIPPED")]}


def test_a_failed_case_fails_the_run(tmp_path):  # type: ignore
    write_logs(tmp_path)
    assert test_cases.main(["test_cases.py", str(tmp_path)]) == 1
    text = (tmp_path / "CASES.md").read_text()
    assert "| 2 | C2 T-B2-02: a stall skips whole slots | FAIL |" in text
    assert "2 cases, 1 failed" in text


def test_all_passed_and_a_missing_log(tmp_path):  # type: ignore
    write_logs(tmp_path, pico_log=PROTO_LOG)
    assert test_cases.main(["test_cases.py", str(tmp_path)]) == 0
    assert "3 tests in 2 files" in (tmp_path / "CASES.md").read_text()
    (tmp_path / "pytest.log").unlink()
    assert test_cases.main(["test_cases.py", str(tmp_path)]) == 1
