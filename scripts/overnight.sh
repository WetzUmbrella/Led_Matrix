#!/usr/bin/env bash
# Runs build steps 1-7 of docs/plan/whack_a_mole_replan.md, one headless Claude run per step.
#
#   bash scripts/overnight.sh --preflight             checks only, starts no step
#   bash scripts/overnight.sh [--from N] [--branch B] preflight, then steps N (default 1) to 7
#
# --branch defaults to the current branch; the run refuses to start on any other.
#
# Commit rule: the headless step never commits (git commit is a disallowed tool for it, and
# any commit it still makes is undone with a soft reset). This script commits a step only
# after scripts/gate.sh passes, and a pre-commit hook that runs the same gate is installed
# for the length of the run, so no route commits with failures (scripts/test_commit_guard.sh).
#
# Free models only: every OpenRouter model the run can reach (the bridge's locked model and
# opencode's model and small_model, forced through OPENCODE_CONFIG_CONTENT) must end in
# :free, or the run does not start. At 900 free requests used today the run stops; there is
# no switch to Claude. A watchdog checks OpenRouter every 2 minutes during a step and stops
# the run if spend rises above the start value or free requests used pass 900.
#
# Laptop version (Windows, Git Bash): make and gcc run in WSL, Python and clang-format on
# Windows, JSON through node because jq is not installed.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1

LOG=docs/plan/overnight_log
SUMMARY=$LOG/SUMMARY_laptop.md
BASELINE_COMMIT=c78f571        # origin/master holding the code the plan builds on
AUTHOR_EMAIL=$(git config user.email)
STEP_TIMEOUT=3h
QUOTA_USED_MAX=900             # free requests used today: at or above this, stop
FREE_MODEL=openrouter/nvidia/nemotron-3.5-super-vl:free
OC_PORT=4097                   # the bridge's opencode server port (bridge config.json)
WSL_DISTRO=Ubuntu-20.04
OC_AUTH=~/.local/share/opencode/auth.json   # where opencode 1.19.5 keeps it on this laptop
BRIDGE_CFG=~/.opencode-mcp-bridge/config.json
PY=python
WATCH_EVERY_S=120

# ---------- options ----------
FROM=1
PREFLIGHT_ONLY=0
BRANCH=$(git branch --show-current)
while [ $# -gt 0 ]; do
  case $1 in
    --preflight) PREFLIGHT_ONLY=1 ;;
    --from) shift; FROM=${1:-}
            [[ "$FROM" =~ ^[1-7]$ ]] || { echo "--from needs a step number 1 to 7" >&2; exit 2; } ;;
    --branch) shift; BRANCH=${1:-}
              [ -n "$BRANCH" ] || { echo "--branch needs a branch name" >&2; exit 2; } ;;
    *) echo "unknown option $1 (use --preflight, --from N, --branch B)" >&2; exit 2 ;;
  esac
  shift
done

mkdir -p "$LOG"
QUOTA_FILE=$LOG/quota.txt
STOP_FLAG=$LOG/.watchdog_stop

die() { echo "PREFLIGHT FAILED: $*" >&2; exit 1; }

# opencode's own model choices, forced to the free model for every server this run starts.
export OPENCODE_CONFIG_CONTENT="{\"model\":\"$FREE_MODEL\",\"small_model\":\"$FREE_MODEL\"}"

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

# Every OpenRouter model the run can reach must end in :free.
check_free_models() {
  local m bad=""
  m=$(js 'j.model ? j.model.providerID + "/" + j.model.modelID : "(unset)"' < "$BRIDGE_CFG")
  [[ "$m" == *:free ]] || bad+=" bridge model $m;"
  js '(j.lock || []).includes("model")' < "$BRIDGE_CFG" | grep -qx true || bad+=" bridge model not locked;"
  m=$(timeout 60 opencode debug config 2>/dev/null |
      js '[j.model || "(unset)", j.small_model || "(unset)"].concat(Object.values(j.agent || {}).map((a) => (a && a.model) || "(agent default)")).join(" ")')
  [ -n "$m" ] || bad+=" opencode config unreadable;"
  for x in $m; do
    [[ "$x" == *:free || "$x" == "(agent default)" ]] || bad+=" opencode model $x;"
  done
  [ -z "$bad" ] || { echo "$bad"; return 1; }
}

# Stop a process and its children (Windows process tree).
kill_tree() { local winpid; winpid=$(cat "/proc/$1/winpid" 2>/dev/null); [ -n "$winpid" ] && taskkill //F //T //PID "$winpid" >/dev/null 2>&1; kill "$1" 2>/dev/null; }

# ---------- preflight ----------
for t in claude curl node "$PY" wsl.exe cygpath opencode taskkill; do command -v "$t" >/dev/null || die "$t not found"; done
[ "$(git branch --show-current)" = "$BRANCH" ] || die "not on $BRANCH (current: $(git branch --show-current))"
[ "$BRANCH" != master ] || die "the runner never runs on master"
git merge-base --is-ancestor "$BASELINE_COMMIT" HEAD ||
  die "HEAD does not contain $BASELINE_COMMIT, so the plan's code is missing. Merge origin/master first."
$PY -c 'import pytest, PIL, clang_format' 2>/dev/null || die "pytest, pillow or clang-format missing (python -m pip install --user ...)"
[ ! -e "$LOG/STOPPED.md" ] || die "$LOG/STOPPED.md exists"
secrets=$(find . -path ./.git -prune -o -type f -name 'wifi_secrets.h' -print)
[ -z "$secrets" ] || die "$secrets is in the tree and bridge reads are unrestricted. Move it out until the run ends."
P="$(cygpath -u "$(npm root -g)")/opencode-mcp-bridge/src"
grep -q 'p !== "bash"' "$P/config.mjs" && grep -q 'cfg.writablePaths?.length' "$P/runs.mjs" ||
  die "bridge patch is missing, see ~/.opencode-mcp-bridge/README.md"
[ "$(js 'Array.isArray(j.writablePaths) ? j.writablePaths.length : 0' < "$BRIDGE_CFG")" -gt 0 ] || die "writablePaths is empty"
[ "$(js 'j.port' < "$BRIDGE_CFG")" = "$OC_PORT" ] || die "bridge port is not $OC_PORT"
bad=$(check_free_models) || die "a model the run can reach is not :free:$bad"
curl -s -m 3 "http://127.0.0.1:$OC_PORT/" >/dev/null 2>&1 &&
  die "an opencode server already runs on port $OC_PORT; it may not have the :free lock. Stop it first."
[ -f "$OC_AUTH" ] || die "no opencode auth at $OC_AUTH (run opencode auth login)"
info=$(or_key_info) || die "could not read OpenRouter usage, so spend cannot be verified"
USAGE_START=$(js 'j.usage' <<<"$info")
used=$(js 'j.free && j.free.used' <<<"$info") || die "free-request count unreadable"
[ "$used" -lt "$QUOTA_USED_MAX" ] || die "$used free requests already used today (stop at $QUOTA_USED_MAX)"
echo "OpenRouter: spend so far \$$USAGE_START, free requests today $(js 'j.free' <<<"$info")"
bash scripts/test_commit_guard.sh >/dev/null 2>&1 || die "scripts/test_commit_guard.sh fails"
echo "PASS commit guard (failing gate, no commit)"
tests_out=$(bash scripts/gate.sh) || { echo "$tests_out"; die "baseline tests fail"; }
echo "$tests_out"
echo "Preflight OK."
[ "$PREFLIGHT_ONLY" = 1 ] && exit 0

# Logs stay out of commits and out of the delegate's scope check.
grep -qxF "$LOG/" .git/info/exclude 2>/dev/null || echo "$LOG/" >> .git/info/exclude
BASE_STATUS=$(git status --porcelain)
[ -z "$BASE_STATUS" ] || die "the tree is not clean"
rm -f "$STOP_FLAG"

# The pre-commit hook: the same gate, for every commit in this worktree while the run lasts.
# shellcheck source=scripts/gate_hook.sh
. scripts/gate_hook.sh
HOOKS=$(cd "$(git rev-parse --git-common-dir)" && pwd -P)/hooks
TOP=$(pwd -P)
install_gate_hook "$HOOKS" "$TOP" "$TOP/scripts/gate.sh" "$TOP/$LOG/precommit_gate.log" ||
  die "could not install the pre-commit hook"

# ---------- state and summary ----------
START_HEAD=$(git rev-parse HEAD)
STARTED=$(date -u +%FT%TZ)
DONE=()
STOP_REASON="none, all steps ran"
LAST_TESTS="not run after baseline"
SUB_TOTAL=0
BRIDGE_TOTAL=0
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
    echo "- Started: $STARTED. Finished: $(date -u +%FT%TZ). Branch $BRANCH, steps run from $FROM."
    echo "- Steps done: ${DONE[*]:-none}"
    echo "- Stop reason: $STOP_REASON"
    echo "- What's left: code steps ${left[*]:-none}, then the hardware parts of every step, plus plan steps 8 and 9"
    echo "- OpenRouter spend this run: \$$spend (must be 0)"
    echo "- OpenRouter free requests today at the end: $(js 'j.free' <<<"${now:-null}" 2>/dev/null)"
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
WATCH_PID=""
on_exit() { [ -n "${WATCH_PID:-}" ] && kill "$WATCH_PID" 2>/dev/null; remove_gate_hook "$HOOKS"; write_summary; }
trap on_exit EXIT

# Writes quota.txt for the headless run. Stops the run at QUOTA_USED_MAX or an unreadable count.
check_quota() {
  local n=$1 log=$2 info used limit left
  info=$(or_key_info) && used=$(js 'j.free && j.free.used' <<<"$info") ||
    stop "$n" "the free-request count could not be read" "$log"
  limit=$(js 'j.free.limit' <<<"$info"); left=$(js 'j.free.remaining' <<<"$info")
  [ "$used" -lt "$QUOTA_USED_MAX" ] ||
    stop "$n" "$used free requests used today (stop at $QUOTA_USED_MAX, limit $limit)" "$log"
  printf 'MODE=BRIDGE_OK\nUSED_TODAY=%s\nLIMIT=%s\nREMAINING=%s\nCHECKED_UTC=%s\n' \
    "$used" "$limit" "$left" "$(date -u +%FT%TZ)" > "$QUOTA_FILE"
  echo "Quota before step: used $used, limit $limit, remaining $left." >> "$log"
}

stop() { STOP_REASON="step $1: $2"; echo "**STOPPED: $2**" >> "$3"; exit 1; }

# Background check during a step: spend above the start value, or free requests used at
# QUOTA_USED_MAX or more, writes the reason to STOP_FLAG and stops the headless run.
watchdog() {
  local target=$1 now spent used
  while kill -0 "$target" 2>/dev/null; do
    sleep "$WATCH_EVERY_S"
    now=$(or_key_info) || continue
    spent=$(jsnum 'b - a' "$USAGE_START" "$(js 'j.usage' <<<"$now")")
    used=$(js 'j.free && j.free.used' <<<"$now") || used=0
    if [ "$(jsnum 'b > 0 ? 1 : 0' 0 "$spent")" = 1 ]; then
      echo "OpenRouter spend rose by \$$spent during the step" > "$STOP_FLAG"
    elif [ "$used" -ge "$QUOTA_USED_MAX" ]; then
      echo "$used free requests used today (stop at $QUOTA_USED_MAX)" > "$STOP_FLAG"
    fi
    [ -e "$STOP_FLAG" ] && { kill_tree "$target"; return; }
  done
}

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
  msg=$LOG/commit_msg_step$N.txt
  { echo "# Step $N"; echo; echo "Started $(date -u +%FT%TZ) on $BRANCH."; } > "$log"
  rm -f "$msg"
  check_quota "$N" "$log"

  head_before=$(git rev-parse HEAD)
  prompt="/anthropic-skills:delegate carry out docs/plan/whack_a_mole_replan.md build step $N only, code only, hardware steps excluded. Confirmations and decisions are pre-approved: pick your recommended option, and log each decision with its reason (for a protocol step, also every new or changed message's field names and bit widths) by appending a '## Step $N' section to $SUMMARY; never rewrite other sections of that file. Route as much as possible to Tier 1 (free OpenRouter models through the bridge): implementation, tests, and the first draft of your step report. The bridge model is locked to $FREE_MODEL; never pass any other model. If a question only the user can answer comes up, add it under '## Questions waiting for you' at the top of $SUMMARY and carry on with the work that does not depend on it. Barr-C standard; every new C file goes into tools/barr_scope.txt. Master is the baseline; the b3/showcase-5nov branch is inspiration only, never copy from it. Never create panel_sync/main/wifi_secrets.h. Before running any test, read every delegate diff; if a delegate changed a Makefile, CMakeLists.txt, any .sh, or any file outside the spec, revert it and do that part yourself. Do NOT commit: git commit is not available to you. Leave the step's changes uncommitted and write the commit message (a subject line, a blank line, then a short body; no Co-Authored-By, session links or Generated-with lines) to $msg. This runner runs the full gate (bash scripts/gate.sh) after you finish and commits only if it passes, so run the gate yourself first and fix what it finds: host tests as 'wsl.exe -d $WSL_DISTRO -- make -C <dir> run' and '... asan' for common/proto/test panel_sync/test ledwall_sync/test tools/pico_tap/test; the C/Python cross check as 'wsl.exe -d $WSL_DISTRO -- python3 scripts/wsl_cross_lang.py'; Python tests as 'python -m pytest -q tools/tests pc/tests'; style as 'python tools/barr_check.py' and 'clang-format --dry-run --Werror <files>' on every file in tools/barr_scope.txt. Never push, never touch .github/, only :free model ids. Read $QUOTA_FILE first for today's free-request count."

  claude -p "$prompt" --permission-mode acceptEdits \
    --disallowedTools "Bash(git commit:*)" "Bash(git push:*)" "Bash(git reset:*)" "Bash(git rebase:*)" \
    --output-format stream-json --verbose > "$raw" 2>> "$log" &
  CLAUDE_PID=$!
  watchdog "$CLAUDE_PID" &
  WATCH_PID=$!
  # Step timeout, done here because `timeout` would hide the claude PID from the watchdog.
  ( sleep "$STEP_TIMEOUT"; echo "step timeout ($STEP_TIMEOUT)" > "$STOP_FLAG"; kill_tree "$CLAUDE_PID" ) &
  TIMER_PID=$!
  wait "$CLAUDE_PID"; rc=$?
  kill "$WATCH_PID" "$TIMER_PID" 2>/dev/null; WATCH_PID=""

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

  [ -e "$STOP_FLAG" ] && stop "$N" "watchdog: $(cat "$STOP_FLAG")" "$log"
  [ "$rc" -eq 0 ] || stop "$N" "claude exited with code $rc" "$log"
  [ "$(result_field "$raw" is_error)" = "false" ] || stop "$N" "the run ended in an error" "$log"

  # A commit made by the headless run is undone (its changes stay in the tree), so only the
  # gate below decides what lands.
  if [ "$(git rev-parse HEAD)" != "$head_before" ]; then
    echo "The headless run committed despite the rule; soft-reset to $head_before so the gate decides." >> "$log"
    git reset -q --soft "$head_before"
  fi

  LAST_TESTS=$(bash scripts/gate.sh); tests_rc=$?
  { echo '```'; echo "$LAST_TESTS"; echo '```'; } >> "$log"
  [ "$tests_rc" -eq 0 ] || stop "$N" "tests fail, nothing committed" "$log"

  [ -z "$(git status --porcelain -- .github)" ] || stop "$N" ".github/ was changed, nothing committed" "$log"
  now=$(or_key_info) || stop "$N" "spend could not be verified, nothing committed" "$log"
  spent=$(jsnum 'b - a' "$USAGE_START" "$(js 'j.usage' <<<"$now")")
  echo "OpenRouter spend so far: \$$spent." >> "$log"
  [ "$(jsnum 'b > 0 ? 1 : 0' 0 "$spent")" = 1 ] && stop "$N" "OpenRouter spend is \$$spent, nothing committed" "$log"

  if [ -n "$(git status --porcelain)" ]; then
    [ -s "$msg" ] || printf 'Build step %s of the whack-a-mole re-plan\n' "$N" > "$msg"
    if grep -qiE 'co-authored-by|generated with|claude-session|claude\.ai/code' "$msg"; then
      stop "$N" "the commit message has an attribution line, nothing committed" "$log"
    fi
    # The pre-commit hook runs the gate again; a failure there also leaves no commit.
    git add -A && git commit -q -F "$msg" ||
      stop "$N" "git commit refused (pre-commit gate), nothing committed" "$log"
  fi
  if git log --format='%ae' "$head_before"..HEAD | grep -vqxF "$AUTHOR_EMAIL"; then
    stop "$N" "a commit has another author" "$log"
  fi

  commits=$(git rev-list --count "$head_before"..HEAD)
  [ "$commits" -gt 0 ] || echo "No commit in this step (no changes)." >> "$log"
  echo "Finished $(date -u +%FT%TZ) with $commits commit(s)." >> "$log"
  DONE+=("$N")
done
