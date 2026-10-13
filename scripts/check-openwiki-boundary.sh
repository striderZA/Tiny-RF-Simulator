#!/usr/bin/env bash
# Keep a scheduled OpenWiki run inside the OPENWIKI block of one file.
#
# Usage, from the repo root: bash check-openwiki-boundary.sh <file>
#
# Text outside the OPENWIKI markers must match HEAD. When it does not, the
# HEAD text is restored there, the regenerated block is kept, and a GitHub
# warning is printed. Missing, duplicated, or misordered markers fail the run,
# because the boundary cannot be located.
set -euo pipefail

file="${1:?usage: check-openwiki-boundary.sh <file>}"
start='<!-- OPENWIKI:START -->'
end='<!-- OPENWIKI:END -->'

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
git show "HEAD:$file" > "$work/head"
cp "$file" "$work/worktree"

# Succeeds only when the file has exactly one start marker before exactly one end marker.
markers_ok() {
  awk -v s="$start" -v e="$end" '
    { line = $0; sub(/\r$/, "", line) }
    line == s { ns++; if (ns == 1) sl = NR }
    line == e { ne++; if (ne == 1) el = NR }
    END { if (ns == 1 && ne == 1 && sl < el) exit 0; exit 1 }
  ' "$1"
}

# Prints every line outside the marker pair. The markers and the lines between them are the block.
outside_block() {
  awk -v s="$start" -v e="$end" '
    { line = $0; sub(/\r$/, "", line) }
    line == s { inside = 1 }
    !inside { print }
    line == e && inside { inside = 0 }
  ' "$1"
}

if ! markers_ok "$work/head"; then
  echo "::error file=$file::HEAD copy has no single OPENWIKI start/end marker pair" >&2
  exit 1
fi
if ! markers_ok "$work/worktree"; then
  echo "::error file=$file::OpenWiki run left the OPENWIKI markers missing, duplicated, or misordered" >&2
  exit 1
fi

if cmp -s <(outside_block "$work/head") <(outside_block "$work/worktree"); then
  echo "OpenWiki boundary: text outside the OPENWIKI block in $file is unchanged"
  exit 0
fi

{
  awk -v s="$start" '{ l = $0; sub(/\r$/, "", l) } l == s { exit } { print }' "$work/head"
  awk -v s="$start" -v e="$end" '{ l = $0; sub(/\r$/, "", l) } l == s { on = 1 } on { print } l == e && on { exit }' "$work/worktree"
  awk -v e="$end" '{ l = $0; sub(/\r$/, "", l) } on { print } l == e { on = 1 }' "$work/head"
} > "$work/restored"

cp "$work/restored" "$file"
echo "::warning file=$file::OpenWiki run changed text outside the OPENWIKI block; restored the committed text there"
