#!/usr/bin/env bash
# Run every host test suite of the repository and keep the full output as
# evidence: the four C suites (plain and with AddressSanitizer and
# UndefinedBehaviorSanitizer) and pytest (verbose, one line per test).
#
# Usage (Linux or WSL, from anywhere in the repo; gcc, make and Python 3
# with pytest and pillow, as in CI):
#   ./tools/run_all_tests.sh                       # docs/testing/results/<today>
#   ./tools/run_all_tests.sh docs/testing/results/2026-11-27
#
# Each suite writes <suite>.log into the output folder, SUMMARY.md lists
# the command, the exit code and the pass count of each, and CASES.md
# (tools/test_cases.py) lists every case with its verdict. The script exits
# non-zero when any suite fails, after running all of them.
set -uo pipefail

ROOT="$(git rev-parse --show-toplevel)"
OUT="${1:-$ROOT/docs/testing/results/$(date +%F)}"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
# Relative to the repo root, where every suite runs.
PY=".venv/bin/python"
[ -x "$ROOT/$PY" ] || PY="python3"

COMMIT="$(git -C "$ROOT" rev-parse --short HEAD)"
DIRTY="$(git -C "$ROOT" status --porcelain --untracked-files=no | wc -l)"
SUMMARY="$OUT/SUMMARY.md"
FAILED=0

{
    echo "# Host test run"
    echo
    echo "- Date: $(date '+%F %T %Z')"
    echo "- Commit: \`$COMMIT\` ($DIRTY tracked files changed and not committed)"
    echo "- Host: $(uname -srm); $(gcc --version | head -1); $(cd "$ROOT" && "$PY" --version 2>&1)"
    echo
    echo "| Suite | Command | Exit | Result |"
    echo "|---|---|---|---|"
} > "$SUMMARY"

# run_suite NAME COMMAND...: run from the repo root, log, add a summary row.
run_suite () {
    local name="$1"
    shift
    local log="$OUT/$name.log"
    local code result

    echo "== $name: $*"
    (cd "$ROOT" && "$@") > "$log" 2>&1
    code=$?

    # The C suites end each group with "Pass = N of M"; pytest ends with
    # "N passed".
    result="$(grep -E 'Pass = [0-9]+ of [0-9]+' "$log" | sed 's/^ *//' |
              paste -sd ';' - | sed 's/;/; /g')"
    if [ -z "$result" ]; then
        result="$(grep -E '[0-9]+ (passed|failed)' "$log" | tail -1 |
                  sed 's/=//g; s/^ *//; s/ *$//')"
    fi

    if [ "$code" -ne 0 ]; then
        FAILED=1
        result="**FAIL** $result"
    fi
    echo "| $name | \`$*\` | $code | $result |" >> "$SUMMARY"
    echo "   exit $code: $result"
}

for suite in common/proto/test panel_sync/test ledwall_sync/test \
             tools/pico_tap/test; do
    tag="$(echo "$suite" | sed 's#/test$##; s#/#_#g')"
    run_suite "$tag" make -C "$suite" run
    run_suite "${tag}_asan" make -C "$suite" asan
done

run_suite pytest "$PY" -m pytest -v tools/tests pc/tests

# One row per case, from the logs above.
(cd "$ROOT" && "$PY" tools/test_cases.py "$OUT") || FAILED=1

echo >> "$SUMMARY"
echo "Every case with its verdict: [CASES.md](CASES.md)." >> "$SUMMARY"
echo >> "$SUMMARY"
if [ "$FAILED" -eq 0 ]; then
    echo "All suites passed." >> "$SUMMARY"
else
    echo "**At least one suite failed**: see its log." >> "$SUMMARY"
fi

echo "summary: $SUMMARY"
exit "$FAILED"
