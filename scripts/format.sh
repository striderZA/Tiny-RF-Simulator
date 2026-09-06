#!/usr/bin/env bash
# Format (or check) C++ sources with the same clang-format-18 CI uses.
#
# Usage:
#   scripts/format.sh              # reformat every changed file (working tree, vs. HEAD)
#   scripts/format.sh --check      # dry-run + --Werror on every changed file (CI-equivalent)
#   scripts/format.sh --all        # reformat the full CI-scanned file set, not just changed files
#   scripts/format.sh file1 file2  # operate on explicit files
#
# If clang-format-18 isn't on PATH, run scripts/install-clang-format.sh first.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

# Must mirror the `format` job in .github/workflows/ci.yml and the DIRS list in
# .githooks/pre-commit.
DIRS=(src app core common tests test_engine signal_generator amplifier spectrum_analyzer equalizer node_graph splitter mixer adc coax pfb_channelizer iq_plot network_analyzer ideal_filter attenuator combiner touchstone help layout tutorial logging)

resolve_clang_format() {
  for candidate in clang-format-18 clang-format; do
    if command -v "$candidate" >/dev/null 2>&1; then
      if "$candidate" --version 2>/dev/null | grep -q "version 18\."; then
        echo "$candidate"
        return 0
      fi
    fi
  done
  local pip_bin="$HOME/.cache/clang-format-18/clang_format/data/bin/clang-format"
  [ -f "${pip_bin}.exe" ] && pip_bin="${pip_bin}.exe"
  if [ -x "$pip_bin" ] || [ -f "$pip_bin" ]; then
    echo "$pip_bin"
    return 0
  fi
  return 1
}

CF="$(resolve_clang_format)" || {
  echo "format.sh: clang-format 18 not found. Run scripts/install-clang-format.sh first." >&2
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
  mapfile -t FILES < <(find "${DIRS[@]}" -name '*.cpp' -o -name '*.h' 2>/dev/null)
else
  mapfile -t FILES < <(git diff --name-only --diff-filter=ACMR -- "${DIRS[@]}" \
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
