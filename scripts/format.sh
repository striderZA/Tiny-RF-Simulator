#!/usr/bin/env bash
# Format (or check) C++ sources with clang-format 18 or newer (CI uses 18).
#
# Usage:
#   scripts/format.sh              # reformat every changed file (working tree, vs. HEAD)
#   scripts/format.sh --check      # dry-run + --Werror on every changed file (CI-equivalent)
#   scripts/format.sh --all        # reformat every tracked file in the CI-scanned set, not just changed files
#   scripts/format.sh file1 file2  # operate on explicit files
#
# If no clang-format >= 18 is on PATH, run scripts/install-clang-format.sh first.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

# The checked directory set is defined once in scripts/format-dirs.sh, shared
# with .githooks/pre-commit and the release workflow's format job.
source scripts/format-dirs.sh
# clang-format discovery is shared with .githooks/pre-commit.
source scripts/clang-format-resolve.sh

CF="$(resolve_clang_format)" || {
  echo "format.sh: clang-format ${CLANG_FORMAT_MIN_MAJOR}+ not found. Run scripts/install-clang-format.sh first." >&2
  exit 1
}

MODE="reformat"
EXPLICIT_FILES=()
for arg in "$@"; do
  case "$arg" in
    --check) MODE="check" ;;
    --all) MODE="${MODE}-all" ;;
    *) EXPLICIT_FILES+=("$arg") ;;
  esac
done

if [ "${#EXPLICIT_FILES[@]}" -gt 0 ]; then
  FILES=("${EXPLICIT_FILES[@]}")
elif [[ "$MODE" == *-all ]]; then
  # Tracked files only: the view a clean CI checkout gets. Not `find`: on Git for
  # Windows a bare `find` is C:\Windows\system32\find, which fails on these args.
  mapfile -t FILES < <(git ls-files -- "${FORMAT_DIRS[@]}" | grep -E '\.(cpp|h)$' || true)
  # A full scan is never legitimately empty; an empty one means discovery broke.
  if [ "${#FILES[@]}" -eq 0 ]; then
    echo "format.sh: --all found no tracked C++ files under FORMAT_DIRS; refusing to pass an empty scan." >&2
    exit 1
  fi
else
  mapfile -t FILES < <(git diff --name-only --diff-filter=ACMR -- "${FORMAT_DIRS[@]}" \
    | grep -E '\.(cpp|h)$' || true)
fi

if [ "${#FILES[@]}" -eq 0 ]; then
  echo "format.sh: no matching files to process."
  exit 0
fi

if [[ "$MODE" == check* ]]; then
  BAD=()
  for f in "${FILES[@]}"; do
    [ -f "$f" ] || continue
    "$CF" --dry-run --Werror "$f" >/dev/null 2>&1 || BAD+=("$f")
  done
  if [ "${#BAD[@]}" -gt 0 ]; then
    echo "format.sh: ${#BAD[@]} file(s) need formatting:"
    printf '  %s\n' "${BAD[@]}"
    exit 1
  fi
  echo "format.sh: all ${#FILES[@]} file(s) clean."
else
  "$CF" -i "${FILES[@]}"
  echo "format.sh: reformatted ${#FILES[@]} file(s)."
fi
