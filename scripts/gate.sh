#!/usr/bin/env bash
# The test gate, shared by scripts/overnight.sh and the pre-commit hook it
# installs. One PASS/FAIL line per check; exit 0 only when every check passes.
#
#   bash scripts/gate.sh
#
# Laptop layout: make and gcc run in WSL ($WSL_DISTRO), Python and
# clang-format 18.1.8 (pip --user) on Windows.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1

WSL_DISTRO=${WSL_DISTRO:-Ubuntu-20.04}
PY=${PY:-python}
MAKE_DIRS=(common/proto/test panel_sync/test ledwall_sync/test tools/pico_tap/test)

# clang-format from pip --user is not on PATH by default.
CF_DIR=$($PY -c 'import clang_format, os; print(os.path.join(os.path.dirname(clang_format.__file__), "data", "bin"))' 2>/dev/null)
[ -n "$CF_DIR" ] && export PATH="$PATH:$(cygpath -u "$CF_DIR")"

rc=0
out=$(wsl.exe -d "$WSL_DISTRO" -- bash -s <<EOF 2>&1 | tr -d '\0'
rc=0
for d in ${MAKE_DIRS[*]}; do
  for t in run asan; do
    if make -C "\$d" "\$t" >/dev/null 2>&1; then echo "PASS make -C \$d \$t"
    else echo "FAIL make -C \$d \$t"; rc=1; fi
  done
  make -C "\$d" clean >/dev/null 2>&1
done
exit \$rc
EOF
) || rc=1
echo "$out"
grep -q '^FAIL' <<<"$out" && rc=1
[ "$(grep -c '^PASS make' <<<"$out")" -eq $(( ${#MAKE_DIRS[@]} * 2 )) ] || rc=1

if out=$(wsl.exe -d "$WSL_DISTRO" -- python3 scripts/wsl_cross_lang.py 2>&1 | tr -d '\0'); then
  echo "PASS cross-language C/Python (WSL): $(tail -n 1 <<<"$out")"
else echo "FAIL cross-language C/Python (WSL): $(tail -n 1 <<<"$out")"; rc=1; fi

if $PY tools/barr_check.py >/dev/null 2>&1; then echo "PASS tools/barr_check.py"
else echo "FAIL tools/barr_check.py"; rc=1; fi

files=$(grep -v '^#' tools/barr_scope.txt | grep -v '^[[:space:]]*$')
# shellcheck disable=SC2086  # one path per word on purpose
if command -v clang-format >/dev/null && clang-format --dry-run --Werror $files >/dev/null 2>&1; then
  echo "PASS clang-format (barr_scope.txt)"
else echo "FAIL clang-format (barr_scope.txt)"; rc=1; fi

if out=$($PY -m pytest -q tools/tests pc/tests 2>&1); then echo "PASS pytest: $(tail -n 1 <<<"$out")"
else echo "FAIL pytest: $(tail -n 1 <<<"$out")"; rc=1; fi

exit $rc
