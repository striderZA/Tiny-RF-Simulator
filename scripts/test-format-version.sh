#!/usr/bin/env bash
# Tests for the clang-format version floor shared by scripts/format.sh and
# .githooks/pre-commit (scripts/clang-format-resolve.sh). Each case runs with an
# isolated PATH and HOME so the machine's own clang-format never leaks in.
# Run:  bash scripts/test-format-version.sh
set -u

ROOT="$(git rev-parse --show-toplevel)"
RESOLVER="$ROOT/scripts/clang-format-resolve.sh"
BASH_BIN="$(command -v bash)"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

PASS=0
FAIL=0

# make_fake <bin-dir> <name> <version-line>: an executable that reports <version-line>.
make_fake() {
  mkdir -p "$1"
  printf '#!%s\necho "%s"\n' "$BASH_BIN" "$3" >"$1/$2"
  chmod +x "$1/$2"
}

# new_bin <dir>: an empty PATH dir holding only the tools the scripts need. `type -P`
# (not `command -v`) gives the real file, never an alias name.
new_bin() {
  mkdir -p "$1"
  ln -s "$(type -P grep)" "$1/grep"
  ln -s "$(type -P sort)" "$1/sort"
  ln -s "$(type -P git)" "$1/git"
}

# resolve_in <home> <path>: run the resolver in a clean environment.
resolve_in() {
  env -i HOME="$1" PATH="$2" "$BASH_BIN" -c 'source "$1"; resolve_clang_format' _ "$RESOLVER"
}
expect() {
  local out rc
  out="$(resolve_in "$4" "$5")"
  rc=$?
  if [ "$rc" = "$2" ] && [ "$out" = "$3" ]; then
    PASS=$((PASS + 1))
  else
    FAIL=$((FAIL + 1))
    echo "FAIL: $1 (expected rc=$2 out='$3'; got rc=$rc out='$out')"
  fi
}

EMPTY_HOME="$TMP/home-empty"
mkdir -p "$EMPTY_HOME"

# Accepts 18 exactly.
B="$TMP/accept18"
new_bin "$B"
make_fake "$B" clang-format-18 "clang-format version 18.1.8"
expect "accepts clang-format-18" 0 "clang-format-18" "$EMPTY_HOME" "$B"

# Accepts a newer major, both versioned and plain names (Ubuntu banner format).
B="$TMP/accept19"
new_bin "$B"
make_fake "$B" clang-format-19 "clang-format version 19.1.7"
expect "accepts clang-format-19" 0 "clang-format-19" "$EMPTY_HOME" "$B"

B="$TMP/accept21"
new_bin "$B"
make_fake "$B" clang-format "Ubuntu clang-format version 21.1.8 (6ubuntu1)"
expect "accepts plain clang-format 21" 0 "clang-format" "$EMPTY_HOME" "$B"

# Picks the lowest accepted major, so an installed 18 wins over a newer one.
B="$TMP/prefer18"
new_bin "$B"
make_fake "$B" clang-format "Ubuntu clang-format version 21.1.8 (6ubuntu1)"
make_fake "$B" clang-format-18 "clang-format version 18.1.8"
expect "prefers lowest accepted major" 0 "clang-format-18" "$EMPTY_HOME" "$B"

# A too-old plain clang-format must not stop the probe before a versioned 19.
B="$TMP/plain17-and-19"
new_bin "$B"
make_fake "$B" clang-format "clang-format version 17.0.6"
make_fake "$B" clang-format-19 "clang-format version 19.1.7"
expect "skips plain 17 for installed 19" 0 "clang-format-19" "$EMPTY_HOME" "$B"

# Rejects a major below 18, under both names.
B="$TMP/reject17"
new_bin "$B"
make_fake "$B" clang-format-17 "clang-format version 17.0.6"
expect "rejects clang-format-17" 1 "" "$EMPTY_HOME" "$B"

B="$TMP/reject17plain"
new_bin "$B"
make_fake "$B" clang-format "clang-format version 17.0.6"
expect "rejects plain clang-format 17" 1 "" "$EMPTY_HOME" "$B"

# A too-old binary on PATH must not block a newer one that is also installed.
B="$TMP/old-and-new"
new_bin "$B"
make_fake "$B" clang-format-17 "clang-format version 17.0.6"
make_fake "$B" clang-format-19 "clang-format version 19.1.7"
expect "skips 17 for installed 19" 0 "clang-format-19" "$EMPTY_HOME" "$B"

# Nothing on PATH: falls back to the pinned pip install, which is version-checked.
B="$TMP/empty-path"
new_bin "$B"
PIP_HOME="$TMP/home-pip18"
PIP_BIN="$PIP_HOME/.cache/clang-format-18/clang_format/data/bin"
make_fake "$PIP_BIN" clang-format "clang-format version 18.1.8"
expect "uses pip 18.1.8 fallback" 0 "$PIP_BIN/clang-format" "$PIP_HOME" "$B"

OLD_HOME="$TMP/home-pip17"
OLD_BIN="$OLD_HOME/.cache/clang-format-18/clang_format/data/bin"
make_fake "$OLD_BIN" clang-format "clang-format version 17.0.6"
expect "rejects pip fallback below 18" 1 "" "$OLD_HOME" "$B"

# A too-old PATH binary still lets the pip 18 fallback win.
B="$TMP/old-path-pip18"
new_bin "$B"
make_fake "$B" clang-format-17 "clang-format version 17.0.6"
expect "pip 18 fallback after rejected PATH 17" 0 "$PIP_BIN/clang-format" "$PIP_HOME" "$B"

# format.sh end-to-end: a too-old binary is refused with a non-zero exit.
FORMAT_SH="$ROOT/scripts/format.sh"
run_format() { # run_format <home> <path>
  (cd "$ROOT" && env -i HOME="$1" PATH="$2" "$BASH_BIN" "$FORMAT_SH" --check scripts/format-dirs.sh 2>&1)
}
B="$TMP/format-old"
new_bin "$B"
make_fake "$B" clang-format-17 "clang-format version 17.0.6"
out="$(run_format "$EMPTY_HOME" "$B")"
rc=$?
if [ "$rc" = "1" ] && [[ "$out" == *"not found"* ]]; then
  PASS=$((PASS + 1))
else
  FAIL=$((FAIL + 1))
  echo "FAIL: format.sh refuses clang-format 17 (rc=$rc out='$out')"
fi

# format.sh end-to-end: a newer major is accepted and the check runs with it.
B="$TMP/format-new"
new_bin "$B"
make_fake "$B" clang-format-19 "clang-format version 19.1.7"
out="$(run_format "$EMPTY_HOME" "$B")"
rc=$?
if [ "$rc" = "0" ] && [[ "$out" == *"clean"* ]]; then
  PASS=$((PASS + 1))
else
  FAIL=$((FAIL + 1))
  echo "FAIL: format.sh accepts clang-format 19 (rc=$rc out='$out')"
fi

echo "test-format-version: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
