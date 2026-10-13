#!/usr/bin/env bash
# Regression test for check-openwiki-boundary.sh. Uses throwaway git
# repositories; no network and no OpenWiki run.
#
# Usage, from any directory: bash scripts/test-openwiki-boundary.sh
set -uo pipefail

guard="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/check-openwiki-boundary.sh"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
failures=0

report() { # report <label> <1 = passed, 0 = failed>
  if [[ "$2" == 1 ]]; then
    echo "PASS: $1"
  else
    echo "FAIL: $1"
    failures=$((failures + 1))
  fi
}

cd "$work" || exit 1
git init -q
git config user.name test
git config user.email test@example.invalid
git config core.autocrlf false

base=('# Root' 'intro' '' '<!-- OPENWIKI:START -->' 'old block' '<!-- OPENWIKI:END -->' 'tail')
printf '%s\n' "${base[@]}" > AGENTS.md
git add AGENTS.md
git commit -qm base
expected_base="$(printf '%s\n' "${base[@]}")"

# write <lines...>: replaces the worktree AGENTS.md with the given lines.
write() { printf '%s\n' "$@" > AGENTS.md; }

# run_guard: stores the guard's exit status in code and its output in out.
run_guard() {
  out="$(bash "$guard" AGENTS.md 2>&1)"
  code=$?
}

# 1. Nothing changed.
git checkout -q -- AGENTS.md
run_guard
ok=0; [[ $code -eq 0 && $out != *"::warning"* && "$(<AGENTS.md)" == "$expected_base" ]] && ok=1
report "unchanged file exits 0 with no warning" "$ok"

# 2. Only the block changed.
git checkout -q -- AGENTS.md
write '# Root' 'intro' '' '<!-- OPENWIKI:START -->' 'new block' '<!-- OPENWIKI:END -->' 'tail'
run_guard
ok=0; [[ $code -eq 0 && $out != *"::warning"* && "$(<AGENTS.md)" == *"new block"* ]] && ok=1
report "block-only change is kept with no warning" "$ok"

# 3. Text outside the block changed, and so did the block.
git checkout -q -- AGENTS.md
write '# Root' 'intro edited' '' '<!-- OPENWIKI:START -->' 'new block' '<!-- OPENWIKI:END -->' 'tail'
run_guard
mapfile -t lines < AGENTS.md
ok=0; [[ $code -eq 0 && $out == *"::warning"* && ${lines[1]} == "intro" && ${lines[4]} == "new block" ]] && ok=1
report "outside edit is restored, block is kept, warning printed" "$ok"

# 4. A blank line above the block was removed.
git checkout -q -- AGENTS.md
write '# Root' 'intro' '<!-- OPENWIKI:START -->' 'old block' '<!-- OPENWIKI:END -->' 'tail'
run_guard
mapfile -t lines < AGENTS.md
ok=0; [[ $code -eq 0 && $out == *"::warning"* && ${#lines[@]} -eq 7 && ${lines[2]} == "" ]] && ok=1
report "blank line above the block is restored with a warning" "$ok"

# 5. End marker removed.
git checkout -q -- AGENTS.md
write '# Root' 'intro' '' '<!-- OPENWIKI:START -->' 'new block' 'tail'
run_guard
ok=0; [[ $code -ne 0 && $out == *"::error"* ]] && ok=1
report "missing end marker fails the run" "$ok"

# 6. Start marker duplicated.
git checkout -q -- AGENTS.md
write '# Root' 'intro' '' '<!-- OPENWIKI:START -->' 'old block' '<!-- OPENWIKI:START -->' '<!-- OPENWIKI:END -->' 'tail'
run_guard
ok=0; [[ $code -ne 0 ]] && ok=1
report "duplicated start marker fails the run" "$ok"

# 7. Markers misordered.
git checkout -q -- AGENTS.md
write '# Root' 'intro' '<!-- OPENWIKI:END -->' 'old block' '<!-- OPENWIKI:START -->' 'tail'
run_guard
ok=0; [[ $code -ne 0 ]] && ok=1
report "misordered markers fail the run" "$ok"

# 8. HEAD itself has no markers, so the boundary cannot be located.
printf '%s\n' '# Root' 'no markers' > AGENTS.md
git commit -qam "no markers"
write '# Root' 'no markers' '<!-- OPENWIKI:START -->' 'block' '<!-- OPENWIKI:END -->'
run_guard
ok=0; [[ $code -ne 0 && $out == *"HEAD copy"* ]] && ok=1
report "HEAD without markers fails the run" "$ok"

echo "failures: $failures"
if (( failures > 0 )); then
  exit 1
fi
