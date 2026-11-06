#!/usr/bin/env bash
# Pre-commit hook helpers, sourced by scripts/overnight.sh and
# scripts/test_commit_guard.sh.
#
# install_gate_hook HOOKS_DIR TOPLEVEL GATE_SCRIPT GATE_LOG
#   Writes HOOKS_DIR/pre-commit, which runs GATE_SCRIPT and refuses the
#   commit unless it exits 0. It acts only in the worktree TOPLEVEL, so
#   commits in other worktrees of the same repository (for example
#   ../Led_Matrix_hw) are not affected. Refuses to replace a hook it did
#   not write.
# remove_gate_hook HOOKS_DIR
#   Removes the hook if this file wrote it.

GATE_HOOK_MARK="# gate hook written by scripts/gate_hook.sh"

install_gate_hook() {
  local hooks=$1 top=$2 gate=$3 log=$4 hook=$1/pre-commit
  if [ -e "$hook" ] && ! grep -qF "$GATE_HOOK_MARK" "$hook"; then
    echo "a pre-commit hook not written by gate_hook.sh exists: $hook" >&2
    return 1
  fi
  mkdir -p "$hooks"
  cat > "$hook" <<EOF
#!/usr/bin/env bash
$GATE_HOOK_MARK
[ "\$(cd "\$(git rev-parse --show-toplevel)" && pwd -P)" = "$top" ] || exit 0
if bash "$gate" > "$log" 2>&1; then exit 0; fi
echo "pre-commit: the gate failed, commit refused. See $log" >&2
exit 1
EOF
  chmod +x "$hook"
}

remove_gate_hook() {
  local hook=$1/pre-commit
  [ -e "$hook" ] && grep -qF "$GATE_HOOK_MARK" "$hook" && rm -f "$hook"
  return 0
}
