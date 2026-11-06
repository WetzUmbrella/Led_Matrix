#!/usr/bin/env bash
# Runs build steps 1-7 of docs/plan/whack_a_mole_replan.md, one headless Claude run per step.
#
#   bash scripts/overnight.sh --preflight     checks only, starts no step
#   bash scripts/overnight.sh                 preflight, then steps 1 to 7
#   bash scripts/overnight.sh --from N        preflight, then steps N to 7
#
# Laptop version (Windows, Git Bash): make and gcc run in WSL ($WSL_DISTRO), Python and
# clang-format 18.1.8 (pip --user) on Windows, JSON through node because jq is not installed.
#
# Stops at the first step that ends with failing tests, a failed run, a dirty tree, a rule
# violation or any OpenRouter spend. Always writes the run block of $LOG/SUMMARY_laptop.md.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1

LOG=docs/plan/overnight_log
SUMMARY=$LOG/SUMMARY_laptop.md
BRANCH=overnight/whack-a-mole
BASELINE_COMMIT=c78f571        # origin/master holding the code the plan builds on
AUTHOR_EMAIL=$(git config user.email)
STEP_TIMEOUT=3h
QUOTA_USED_MAX=900             # at or above this: Claude only
QUOTA_LEFT_MIN=100             # at or below this: Claude only
MAKE_DIRS=(common/proto/test panel_sync/test ledwall_sync/test tools/pico_tap/test)
WSL_DISTRO=Ubuntu-20.04
OC_AUTH=~/.local/share/opencode/auth.json   # where opencode 1.19.5 keeps it on this laptop
BRIDGE_CFG=~/.opencode-mcp-bridge/config.json
PY=python

# ---------- options ----------
FROM=1
PREFLIGHT_ONLY=0
while [ $# -gt 0 ]; do
  case $1 in
    --preflight) PREFLIGHT_ONLY=1 ;;
    --from) shift; FROM=${1:-}
            [[ "$FROM" =~ ^[1-7]$ ]] || { echo "--from needs a step number 1 to 7" >&2; exit 2; } ;;
    *) echo "unknown option $1 (use --preflight or --from N)" >&2; exit 2 ;;
  esac
  shift
done

mkdir -p "$LOG"
QUOTA_FILE=$LOG/quota.txt
SWITCH_FILE=$LOG/.quota_switched   # sticky: once switched, every later step stays Claude only

die() { echo "PREFLIGHT FAILED: $*" >&2; exit 1; }

# clang-format 18.1.8 from pip --user is not on PATH by default; the headless runs inherit this.
CF_DIR=$($PY -c 'import clang_format, os; print(os.path.join(os.path.dirname(clang_format.__file__), "data", "bin"))' 2>/dev/null)
[ -n "$CF_DIR" ] && export PATH="$PATH:$(cygpath -u "$CF_DIR")"

# jq stand-in. $1 is a JavaScript expression over j, the JSON read from stdin. Prints the
# value (objects as compact JSON); exit 1 when it is null, undefined or the input is not JSON.
js() {
  node -e '
    let d = ""; process.stdin.on("data", (c) => (d += c)).on("end", () => {
      try {
        const v = new Function("j", "return (" + process.argv[1] + ");")(JSON.parse(d));
        if (v === undefined || v === null) process.exit(1);
        console.log(typeof v === "object" ? JSON.stringify(v) : String(v));
      } catch (e) { process.exit(1); }
    });' "$1"
}

# Arithmetic on two numbers: jsnum 'b - a' A B.
jsnum() { node -e 'const [a, b] = process.argv.slice(2).map(Number); console.log(new Function("a", "b", "return (" + process.argv[1] + ");")(a, b));' "$@"; }

# The key goes to curl on stdin, so it never appears in a process list or a log.
or_key_info() {
  local key=${OPENROUTER_API_KEY:-}
  [ -n "$key" ] || key=$(js 'j.openrouter && j.openrouter.key' < "$OC_AUTH" 2>/dev/null)
  [ -n "$key" ] || return 1
  printf 'header = "Authorization: Bearer %s"\n' "$key" |
    curl -sS -m 30 -K - https://openrouter.ai/api/v1/key |
    js 'j.data && j.data.usage != null ? {usage: j.data.usage, free: j.data.free_model_daily_requests} : null'
}

# All four make suites (run and asan) in one WSL call, one PASS/FAIL line each.
wsl_make_all() {
  wsl.exe -d "$WSL_DISTRO" -- bash -s <<EOF 2>&1 | tr -d '\0'
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
}

run_tests() {
  local rc=0 out files
  wsl_make_all || rc=1
  if out=$(wsl.exe -d "$WSL_DISTRO" -- python3 scripts/wsl_cross_lang.py 2>&1 | tr -d '\0'); then
    echo "PASS cross-language C/Python (WSL): $(tail -n 1 <<<"$out")"
  else echo "FAIL cross-language C/Python (WSL): $(tail -n 1 <<<"$out")"; rc=1; fi
  if $PY tools/barr_check.py >/dev/null 2>&1; then echo "PASS tools/barr_check.py"
  else echo "FAIL tools/barr_check.py"; rc=1; fi
  files=$(grep -v '^#' tools/barr_scope.txt | grep -v '^[[:space:]]*$')
  # shellcheck disable=SC2086  # one path per word on purpose
  if clang-format --dry-run --Werror $files >/dev/null 2>&1; then echo "PASS clang-format (barr_scope.txt)"
  else echo "FAIL clang-format (barr_scope.txt)"; rc=1; fi
  out=$($PY -m pytest -q tools/tests pc/tests 2>&1) || rc=1
  echo "pytest: $(tail -n 1 <<<"$out")"
  return $rc
}

# ---------- preflight ----------
for t in claude curl node "$PY" wsl.exe cygpath; do command -v "$t" >/dev/null || die "$t not found"; done
command -v clang-format >/dev/null || die "clang-format not found (pip install --user clang-format==18.1.8)"
clang-format --version | grep -q ' 18\.1\.8' || die "clang-format is not 18.1.8, the version CI pins"
wsl.exe -d "$WSL_DISTRO" -- bash -c 'command -v make && command -v gcc && command -v python3' >/dev/null 2>&1 ||
  die "make, gcc or python3 missing in WSL $WSL_DISTRO"
[ "$(git branch --show-current)" = "$BRANCH" ] || die "not on $BRANCH"
git merge-base --is-ancestor "$BASELINE_COMMIT" HEAD ||
  die "HEAD does not contain $BASELINE_COMMIT, so the plan's code is missing. Merge origin/master first."
$PY -c 'import pytest, PIL' 2>/dev/null || die "pytest or pillow missing. Run: python -m pip install --user pytest pillow"
[ ! -e "$LOG/STOPPED.md" ] || die "$LOG/STOPPED.md exists"
secrets=$(find . -path ./.git -prune -o -type f -name 'wifi_secrets.h' -print)
[ -z "$secrets" ] || die "$secrets is in the tree and bridge reads are unrestricted. Move it out until the run ends."
P="$(cygpath -u "$(npm root -g)")/opencode-mcp-bridge/src"
grep -q 'p !== "bash"' "$P/config.mjs" && grep -q 'cfg.writablePaths?.length' "$P/runs.mjs" ||
  die "bridge patch is missing, see ~/.opencode-mcp-bridge/README.md"
[ "$(js 'Array.isArray(j.writablePaths) ? j.writablePaths.length : 0' < "$BRIDGE_CFG")" -gt 0 ] || die "writablePaths is empty"
js 'j.model && /:free$/.test(j.model.modelID) && (j.lock || []).includes("model")' < "$BRIDGE_CFG" | grep -qx true ||
  die "bridge model is not a locked :free model"
[ -f "$OC_AUTH" ] || die "no opencode auth at $OC_AUTH (run opencode auth login)"
info=$(or_key_info) || die "could not read OpenRouter usage, so spend cannot be verified"
USAGE_START=$(js 'j.usage' <<<"$info")
echo "OpenRouter: spend so far \$$USAGE_START, free requests today $(js 'j.free' <<<"$info")"
tests_out=$(run_tests) || { echo "$tests_out"; die "baseline tests fail"; }
echo "$tests_out"
echo "Preflight OK."
[ "$PREFLIGHT_ONLY" = 1 ] && exit 0

# Logs stay out of commits and out of the delegate's scope check.
grep -qxF "$LOG/" .git/info/exclude 2>/dev/null || echo "$LOG/" >> .git/info/exclude
BASE_STATUS=$(git status --porcelain)
rm -f "$SWITCH_FILE"

# ---------- state and summary ----------
START_HEAD=$(git rev-parse HEAD)
STARTED=$(date -u +%FT%TZ)
DONE=()
STOP_REASON="none, all steps ran"
LAST_TESTS="not run after baseline"
SUB_TOTAL=0
BRIDGE_TOTAL=0
SWITCH_LINE="no switch, the bridge stayed available"
MARK_START="<!-- overnight.sh run summary: start -->"
MARK_END="<!-- overnight.sh run summary: end -->"

# Replaces only the block between the markers, so the decision log around it is kept.
write_summary() {
  local now spend="unverified, usage could not be read" n left=() block tmp
  now=$(or_key_info) && spend=$(jsnum 'b - a' "$USAGE_START" "$(js 'j.usage' <<<"$now")")
  for n in 1 2 3 4 5 6 7; do
    [ "$n" -lt "$FROM" ] && continue
    [[ " ${DONE[*]:-} " == *" $n "* ]] || left+=("$n")
  done
  block=$(
    echo "$MARK_START"
    echo "## Script run summary"
    echo
    echo "- Started: $STARTED. Finished: $(date -u +%FT%TZ). Steps run from $FROM."
    echo "- Steps done: ${DONE[*]:-none}"
    echo "- Stop reason: $STOP_REASON"
    echo "- What's left: code steps ${left[*]:-none}, then the hardware parts of every step, plus plan steps 8 and 9"
    echo "- OpenRouter spend this run: \$$spend (must be 0)"
    echo "- OpenRouter free requests today at the end: $(js 'j.free' <<<"${now:-null}" 2>/dev/null)"
    echo "- Quota switch: $SWITCH_LINE"
    echo "- Claude subagent calls used: $SUB_TOTAL"
    echo "- Bridge dispatches used: $BRIDGE_TOTAL"
    echo "- Commits: $(git rev-list --count "$START_HEAD"..HEAD) (nothing pushed by this script)"
    echo
    echo "### Tests (last gate)"
    echo
    echo '```'
    echo "$LAST_TESTS"
    echo '```'
    echo
    echo "### Commits"
    echo
    echo '```'
    git log --format='%h %an %s' "$START_HEAD"..HEAD
    echo '```'
    echo "$MARK_END"
  )
  tmp=$(mktemp)
  [ -f "$SUMMARY" ] && awk -v s="$MARK_START" -v e="$MARK_END" \
    '$0 == s {skip = 1} !skip {print} $0 == e {skip = 0}' "$SUMMARY" > "$tmp"
  { cat "$tmp"; echo; echo "$block"; } > "$SUMMARY"
  rm -f "$tmp"
}
trap write_summary EXIT

# Writes quota.txt for the headless run and sets MODE. An unreadable count means Claude only.
check_quota() {
  local n=$1 log=$2 info used=unknown limit=unknown left=unknown why=""
  if info=$(or_key_info) && js 'j.free && j.free.used' <<<"$info" >/dev/null; then
    used=$(js 'j.free.used' <<<"$info"); limit=$(js 'j.free.limit' <<<"$info"); left=$(js 'j.free.remaining' <<<"$info")
    [ "$used" -ge "$QUOTA_USED_MAX" ] && why="$used free requests used today (limit $limit, $left left)"
    [ "$left" -le "$QUOTA_LEFT_MIN" ] && why="$used free requests used today (limit $limit, $left left)"
  else
    why="the free-request count could not be read"
  fi
  MODE=BRIDGE_OK
  if [ -e "$SWITCH_FILE" ]; then
    MODE=CLAUDE_ONLY
  elif [ -n "$why" ]; then
    MODE=CLAUDE_ONLY
    echo "step $n: $why" > "$SWITCH_FILE"
    SWITCH_LINE="switched to Claude only at step $n and for every later step: $why"
    echo "**Quota switch: from this step on, Claude only and no bridge calls, because $why.**" >> "$log"
  fi
  printf 'MODE=%s\nUSED_TODAY=%s\nLIMIT=%s\nREMAINING=%s\nCHECKED_UTC=%s\n' \
    "$MODE" "$used" "$limit" "$left" "$(date -u +%FT%TZ)" > "$QUOTA_FILE"
  echo "Quota before step: mode $MODE, used $used, limit $limit, remaining $left." >> "$log"
}

stop() { STOP_REASON="step $1: $2"; echo "**STOPPED: $2**" >> "$3"; exit 1; }

# stream-json helpers (jq stand-ins). Each reads the step's .jsonl.
count_tools() {
  node -e '
    const fs = require("fs"); const re = new RegExp(process.argv[2]); let n = 0;
    for (const l of fs.readFileSync(process.argv[1], "utf8").split("\n")) {
      let j; try { j = JSON.parse(l); } catch { continue; }
      if (j.type !== "assistant") continue;
      for (const c of j.message?.content ?? []) if (c.type === "tool_use" && re.test(c.name)) n++;
    }
    console.log(n);' "$1" "$2"
}
result_field() {
  node -e '
    const fs = require("fs"); let last = null;
    for (const l of fs.readFileSync(process.argv[1], "utf8").split("\n")) {
      let j; try { j = JSON.parse(l); } catch { continue; }
      if (j.type === "result") last = j;
    }
    const v = last ? last[process.argv[2]] : undefined;
    console.log(v === undefined || v === null ? (process.argv[2] === "result" ? "(no result text)" : "") : String(v));' "$1" "$2"
}

# ---------- the loop ----------
for N in $(seq "$FROM" 7); do
  log=$LOG/step$N.md
  raw=$LOG/step$N.jsonl
  { echo "# Step $N"; echo; echo "Started $(date -u +%FT%TZ)."; } > "$log"
  check_quota "$N" "$log"

  # In Claude-only mode the tools are also taken away, so the switch does not rely on the prompt.
  extra=()
  [ "$MODE" = CLAUDE_ONLY ] && extra=(--disallowedTools mcp__opencode)

  head_before=$(git rev-parse HEAD)
  prompt="/anthropic-skills:delegate carry out docs/plan/whack_a_mole_replan.md build step $N only, code only, hardware steps excluded. Confirmations and decisions are pre-approved: pick your recommended option, and log each decision with its reason (for a protocol step, also every new or changed message's field names and bit widths) by appending a '## Step $N' section to $SUMMARY; never rewrite other sections of that file. Barr-C standard; every new C file goes into tools/barr_scope.txt. Master is the baseline; the b3/showcase-5nov branch is inspiration only, never copy from it. Never create panel_sync/main/wifi_secrets.h. Before running any test, read every delegate diff; if a delegate changed a Makefile, CMakeLists.txt, any .sh, or any file outside the spec, revert it and do that part yourself. This is a Windows laptop: make and gcc exist only in WSL. Run host tests as 'wsl.exe -d $WSL_DISTRO -- make -C <dir> run' and '... asan' for ${MAKE_DIRS[*]}; the C/Python cross check as 'wsl.exe -d $WSL_DISTRO -- python3 scripts/wsl_cross_lang.py'; Python tests as 'python -m pytest -q tools/tests pc/tests'; style as 'python tools/barr_check.py' and 'clang-format --dry-run --Werror <files>' on every file in tools/barr_scope.txt. The wsl.exe commands may be denied by your permissions; if so, say so in the report, compile-check new C code by reading it carefully instead, and rely on this runner, which runs the WSL suites right after you finish and stops the night if any fail. Commit the step when every check you could run passes, authored as me only, no Co-Authored-By, session links or Generated-with lines, and leave no uncommitted changes outside $LOG/. Never push, never touch .github/, only :free model ids. Before anything else, read $QUOTA_FILE, which was written just now with today's OpenRouter free-request count. If it says MODE=CLAUDE_ONLY, do this whole step with Claude only (Tier 3 or yourself), make no bridge calls, and state the switch at the top of your final report. If it says MODE=BRIDGE_OK, route as usual. In either mode never use a paid OpenRouter model."

  timeout "$STEP_TIMEOUT" claude -p "$prompt" --permission-mode acceptEdits \
    --output-format stream-json --verbose "${extra[@]}" > "$raw" 2>> "$log"
  rc=$?

  subagents=$(count_tools "$raw" '^(Task|Agent)$')
  bridge=$(count_tools "$raw" '^mcp__opencode__opencode_dispatch$')
  SUB_TOTAL=$((SUB_TOTAL + subagents))
  BRIDGE_TOTAL=$((BRIDGE_TOTAL + bridge))
  {
    echo; echo "## Report"; echo
    result_field "$raw" result
    echo; echo "## Checks"; echo
    echo "Claude exit code $rc. Claude subagent calls $subagents. Bridge dispatches $bridge."
  } >> "$log"

  [ "$rc" -eq 0 ] || stop "$N" "claude exited with code $rc" "$log"
  [ "$(result_field "$raw" is_error)" = "false" ] || stop "$N" "the run ended in an error" "$log"
  [ "$MODE" = BRIDGE_OK ] || [ "$bridge" -eq 0 ] || stop "$N" "bridge used after the quota switch" "$log"

  LAST_TESTS=$(run_tests); tests_rc=$?
  { echo '```'; echo "$LAST_TESTS"; echo '```'; } >> "$log"
  [ "$tests_rc" -eq 0 ] || stop "$N" "tests fail" "$log"

  [ "$(git status --porcelain)" = "$BASE_STATUS" ] || stop "$N" "uncommitted changes left in the tree" "$log"
  [ -z "$(git diff --name-only "$head_before" HEAD -- .github)" ] || stop "$N" ".github/ was changed" "$log"
  if git log --format='%ae' "$head_before"..HEAD | grep -vqxF "$AUTHOR_EMAIL"; then
    stop "$N" "a commit has another author" "$log"
  fi
  if git log --format='%B' "$head_before"..HEAD | grep -qiE 'co-authored-by|generated with|claude-session|claude\.ai/code'; then
    stop "$N" "a commit has an attribution line" "$log"
  fi

  now=$(or_key_info) || stop "$N" "spend could not be verified" "$log"
  spent=$(jsnum 'b - a' "$USAGE_START" "$(js 'j.usage' <<<"$now")")
  echo "OpenRouter spend so far: \$$spent." >> "$log"
  [ "$(jsnum 'b > 0 ? 1 : 0' 0 "$spent")" = 1 ] && stop "$N" "OpenRouter spend is \$$spent" "$log"

  commits=$(git rev-list --count "$head_before"..HEAD)
  [ "$commits" -gt 0 ] || echo "No commit in this step (it may be hardware only). Check the report above." >> "$log"
  echo "Finished $(date -u +%FT%TZ) with $commits commit(s)." >> "$log"
  DONE+=("$N")
done
