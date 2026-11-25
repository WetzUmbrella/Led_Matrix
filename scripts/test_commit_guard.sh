#!/usr/bin/env bash
# Check that a failing gate produces no commit, by both routes the runner
# allows: the runner's own commit_step (gate first, then commit) and a direct
# `git commit` (the pre-commit hook). Runs in a throwaway repository, so the
# real gate is replaced by a script that passes or fails on demand.
#
#   bash scripts/test_commit_guard.sh      exit 0 when every check passes
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd -P)
# shellcheck source=scripts/gate_hook.sh
. "$HERE/gate_hook.sh"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
repo=$tmp/repo
mkdir -p "$repo"
cd "$repo" || exit 1
git init -q .
git config user.email guard@test.invalid
git config user.name guard-test
git config commit.gpgsign false
echo base > f.txt && git add f.txt && git commit -q --no-verify -m base

gate=$tmp/gate.sh
printf '#!/usr/bin/env bash\nexit "${FAKE_GATE_RC:-1}"\n' > "$gate"
top=$(pwd -P)
install_gate_hook "$(git rev-parse --git-common-dir)/hooks" "$top" "$gate" "$tmp/gate.log" || exit 1

# The runner's route, as in scripts/overnight.sh: gate first, then commit.
commit_step() {
  bash "$gate" || return 1
  git add -A && git commit -q -m "$1"
}

fails=0
check() { if [ "$1" = "$2" ]; then echo "PASS $3"; else echo "FAIL $3 (got $1, want $2)"; fails=$((fails + 1)); fi; }
count() { git rev-list --count HEAD; }

echo change1 >> f.txt
FAKE_GATE_RC=1 commit_step "should not land" 2>/dev/null; check "$(count)" 1 "runner route: failing gate, no commit"
git add -A; FAKE_GATE_RC=1 git commit -q -m "direct, should not land" 2>/dev/null
check "$(count)" 1 "direct git commit: failing gate, hook refuses"
FAKE_GATE_RC=0 commit_step "lands"; check "$(count)" 2 "runner route: passing gate, one commit"
echo change2 >> f.txt; git add -A; FAKE_GATE_RC=0 git commit -q -m "direct, lands"
check "$(count)" 3 "direct git commit: passing gate, hook allows"

remove_gate_hook "$(git rev-parse --git-common-dir)/hooks"
[ ! -e "$(git rev-parse --git-common-dir)/hooks/pre-commit" ]; check $? 0 "hook removed"

[ "$fails" -eq 0 ]
