#!/usr/bin/env bash
# Shared clang-format discovery for scripts/format.sh and .githooks/pre-commit.
# Sourced (not executed). Accepts any clang-format whose major version is at least
# CLANG_FORMAT_MIN_MAJOR; the pinned 18.1.8 pip wheel (scripts/install-clang-format.sh)
# is only the fallback when no such binary is on PATH.
# Test: bash scripts/test-format-version.sh

CLANG_FORMAT_MIN_MAJOR=18
CLANG_FORMAT_MAX_PROBE_MAJOR=30

# clang_format_major <binary>: print the major version reported by `<binary> --version`,
# or nothing when the binary cannot be run or reports no version.
clang_format_major() {
  local out
  out="$("$1" --version 2>/dev/null)" || return 0
  if [[ "$out" =~ version[[:space:]]+([0-9]+)\. ]]; then
    echo "${BASH_REMATCH[1]}"
  fi
}

# accepts_clang_format <name-or-path>: succeed when it resolves to a clang-format whose
# major version is at least CLANG_FORMAT_MIN_MAJOR.
accepts_clang_format() {
  local major
  command -v "$1" >/dev/null 2>&1 || return 1
  major="$(clang_format_major "$1")"
  [ -n "$major" ] && [ "$major" -ge "$CLANG_FORMAT_MIN_MAJOR" ]
}

# resolve_clang_format: print the clang-format command name or path to use.
# Probes PATH in order: clang-format-18 (CI parity), clang-format, then clang-format-19
# through clang-format-30; the first accepted binary wins. The probe is bounded on
# purpose: scanning every PATH directory (compgen -c) takes minutes on WSL's /mnt/c.
resolve_clang_format() {
  local name n
  for name in clang-format-18 clang-format; do
    if accepts_clang_format "$name"; then
      echo "$name"
      return 0
    fi
  done
  for ((n = CLANG_FORMAT_MIN_MAJOR + 1; n <= CLANG_FORMAT_MAX_PROBE_MAJOR; n++)); do
    if accepts_clang_format "clang-format-$n"; then
      echo "clang-format-$n"
      return 0
    fi
  done

  local pip_bin="$HOME/.cache/clang-format-18/clang_format/data/bin/clang-format"
  [ -f "${pip_bin}.exe" ] && pip_bin="${pip_bin}.exe"
  if [ -f "$pip_bin" ] && accepts_clang_format "$pip_bin"; then
    echo "$pip_bin"
    return 0
  fi
  return 1
}
