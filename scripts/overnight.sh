#!/usr/bin/env bash
# Runs build steps 1-7 of docs/plan/whack_a_mole_replan.md, one headless Claude run per step.
#
#   bash scripts/overnight.sh --preflight   checks only, starts no step
#   bash scripts/overnight.sh               preflight, then the loop
#
# Stops at the first step that ends with failing tests, a failed run, a dirty tree, a rule
# violation or any OpenRouter spend. Always writes docs/plan/overnight_log/SUMMARY.md.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1

LOG=docs/plan/overnight_log
BRANCH=overnight/whack-a-mole
BASELINE_COMMIT=c78f571        # origin/master holding the code the plan builds on
AUTHOR_EMAIL=$(git config user.email)
STEP_TIMEOUT=3h
QUOTA_USED_MAX=900             # at or above this: Claude only
QUOTA_LEFT_MIN=100             # at or below this: Claude only
MAKE_DIRS=(common/proto/test panel_sync/test ledwall_sync/test tools/pico_tap/test)

mkdir -p "$LOG"
QUOTA_FILE=$LOG/quota.txt
SWITCH_FILE=$LOG/.quota_switched   # sticky: once switched, every later step stays Claude only

die() { echo "PREFLIGHT FAILED: $*" >&2; exit 1; }

# The key goes to curl on stdin, so it never appears in a process list or a log.
or_key_info() {
  local key=${OPENROUTER_API_KEY:-}
  [ -n "$key" ] || key=$(jq -r '.openrouter.key // empty' ~/.local/share/opencode/auth.json 2>/dev/null)
  [ -n "$key" ] || return 1
  printf 'header = "Authorization: Bearer %s"\n' "$key" |
    curl -sS -m 30 -K - https://openrouter.ai/api/v1/key |
    jq -ce '.data | {usage, free: .free_model_daily_requests} | select(.usage != null)'
}

run_tests() {
  local rc=0 d t
  for d in "${MAKE_DIRS[@]}"; do
    for t in run asan; do
      if make -C "$d" "$t" >/dev/null 2>&1; then echo "PASS make -C $d $t"
      else echo "FAIL make -C $d $t"; rc=1; fi
    done
    make -C "$d" clean >/dev/null 2>&1
  done
  if python3 tools/barr_check.py >/dev/null 2>&1; then echo "PASS tools/barr_check.py"
  else echo "FAIL tools/barr_check.py"; rc=1; fi
  local out
  out=$(python3 -m pytest -q tools/tests pc/tests 2>&1) || rc=1
  echo "pytest: $(tail -n 1 <<<"$out")"
  return $rc
}

# ---------- preflight ----------
for t in claude jq curl make gcc python3; do command -v "$t" >/dev/null || die "$t not found"; done
[ "$(git branch --show-current)" = "$BRANCH" ] || die "not on $BRANCH"
git merge-base --is-ancestor "$BASELINE_COMMIT" HEAD ||
  die "HEAD does not contain $BASELINE_COMMIT, so the plan's code is missing. Merge origin/master first."
python3 -c 'import pytest, PIL' 2>/dev/null || die "pytest or pillow missing. Run: sudo apt install -y python3-pytest python3-pil"
[ ! -e "$LOG/STOPPED.md" ] || die "$LOG/STOPPED.md exists"
secrets=$(find . -path ./.git -prune -o -type f -name 'wifi_secrets.h' -print)
[ -z "$secrets" ] || die "$secrets is in the tree and bridge reads are unrestricted. Move it out until the run ends."
P=~/.local/lib/node_modules/opencode-mcp-bridge/src
grep -q 'p !== "bash"' "$P/config.mjs" && grep -q 'cfg.writablePaths?.length' "$P/runs.mjs" ||
  die "bridge patch is missing, see ~/.opencode-mcp-bridge/README.md"
[ "$(jq '.writablePaths | length' ~/.opencode-mcp-bridge/config.json)" -gt 0 ] || die "writablePaths is empty"
info=$(or_key_info) || die "could not read OpenRouter usage, so spend cannot be verified"
USAGE_START=$(jq -r .usage <<<"$info")
echo "OpenRouter: spend so far \$$USAGE_START, free requests today $(jq -c .free <<<"$info")"
tests_out=$(run_tests) || { echo "$tests_out"; die "baseline tests fail"; }
echo "$tests_out"
BASE_STATUS=$(git status --porcelain)
echo "Preflight OK."
[ "${1:-}" = "--preflight" ] && exit 0

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

write_summary() {
  local now spend="unverified, usage could not be read" n left=()
  now=$(or_key_info) && spend=$(jq -rn --argjson a "$USAGE_START" --argjson b "$(jq .usage <<<"$now")" '$b - $a')
  for n in 1 2 3 4 5 6 7; do [[ " ${DONE[*]:-} " == *" $n "* ]] || left+=("$n"); done
  {
    echo "# Overnight summary"
    echo
    echo "- Started: $STARTED. Finished: $(date -u +%FT%TZ)."
    echo "- Steps done: ${DONE[*]:-none}"
    echo "- Stop reason: $STOP_REASON"
    echo "- What's left: code steps ${left[*]:-none}, then the hardware parts of every step, plus plan steps 8 and 9"
    echo "- OpenRouter spend this run: \$$spend (must be 0)"
    echo "- OpenRouter free requests today at the end: $(jq -c .free <<<"${now:-null}" 2>/dev/null)"
    echo "- Quota switch: $SWITCH_LINE"
    echo "- Claude subagent calls used: $SUB_TOTAL"
    echo "- Bridge dispatches used: $BRIDGE_TOTAL"
    echo "- Commits: $(git rev-list --count "$START_HEAD"..HEAD) (nothing pushed by this script)"
    echo
    echo "## Tests passing (last gate)"
    echo
    echo '```'
    echo "$LAST_TESTS"
    echo '```'
    echo
    echo "## Commits"
    echo
    echo '```'
    git log --format='%h %an %s' "$START_HEAD"..HEAD
    echo '```'
  } > "$LOG/SUMMARY.md"
}
trap write_summary EXIT

# Writes quota.txt for the headless run and sets MODE. An unreadable count means Claude only.
check_quota() {
  local n=$1 log=$2 info used=unknown limit=unknown left=unknown why=""
  if info=$(or_key_info) && jq -e '.free.used != null' <<<"$info" >/dev/null; then
    used=$(jq -r .free.used <<<"$info"); limit=$(jq -r .free.limit <<<"$info"); left=$(jq -r .free.remaining <<<"$info")
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

# ---------- the loop ----------
for N in 1 2 3 4 5 6 7; do
  log=$LOG/step$N.md
  raw=$LOG/step$N.jsonl
  { echo "# Step $N"; echo; echo "Started $(date -u +%FT%TZ)."; } > "$log"
  check_quota "$N" "$log"

  # In Claude-only mode the tools are also taken away, so the switch does not rely on the prompt.
  extra=()
  [ "$MODE" = CLAUDE_ONLY ] && extra=(--disallowedTools mcp__opencode)

  head_before=$(git rev-parse HEAD)
  prompt="/delegate carry out docs/plan/whack_a_mole_replan.md build step $N only, code only, hardware steps excluded. Confirmations and decisions are pre-approved: pick your recommended option and log it. Before running any test, read the diff; if a delegate changed a Makefile, CMakeLists.txt, any .sh, or any file outside the spec, revert it and do that part yourself. Commit the step when its tests pass, authored as me only, no Co-Authored-By, session links or Generated-with lines. Never push, never touch .github/, only :free model ids. Before anything else, read $QUOTA_FILE, which was written just now with today's OpenRouter free-request count. If it says MODE=CLAUDE_ONLY, do this whole step with Claude only (Tier 3 or yourself), make no bridge calls, and state the switch at the top of your final report. If it says MODE=BRIDGE_OK, route as usual. In either mode never use a paid OpenRouter model."

  timeout "$STEP_TIMEOUT" claude -p "$prompt" --permission-mode acceptEdits \
    --output-format stream-json --verbose "${extra[@]}" > "$raw" 2>> "$log"
  rc=$?

  count_tools() {
    jq -R 'fromjson? | select(.type == "assistant") | .message.content[]? | select(.type == "tool_use") | .name' "$raw" |
      grep -cE "$1"
  }
  subagents=$(count_tools '^"(Task|Agent)"$')
  bridge=$(count_tools '^"mcp__opencode__opencode_dispatch"$')
  SUB_TOTAL=$((SUB_TOTAL + subagents))
  BRIDGE_TOTAL=$((BRIDGE_TOTAL + bridge))
  {
    echo; echo "## Report"; echo
    jq -Rr 'fromjson? | select(.type == "result") | .result // "(no result text)"' "$raw"
    echo; echo "## Checks"; echo
    echo "Claude exit code $rc. Claude subagent calls $subagents. Bridge dispatches $bridge."
  } >> "$log"

  [ "$rc" -eq 0 ] || stop "$N" "claude exited with code $rc" "$log"
  [ "$(jq -R 'fromjson? | select(.type == "result") | .is_error' "$raw" | tail -n 1)" = "false" ] ||
    stop "$N" "the run ended in an error" "$log"
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
  spent=$(jq -rn --argjson a "$USAGE_START" --argjson b "$(jq .usage <<<"$now")" '$b - $a')
  echo "OpenRouter spend so far: \$$spent." >> "$log"
  jq -en --argjson s "$spent" '$s > 0' >/dev/null && stop "$N" "OpenRouter spend is \$$spent" "$log"

  commits=$(git rev-list --count "$head_before"..HEAD)
  [ "$commits" -gt 0 ] || echo "No commit in this step (it may be hardware only). Check the report above." >> "$log"
  echo "Finished $(date -u +%FT%TZ) with $commits commit(s)." >> "$log"
  DONE+=("$N")
done
