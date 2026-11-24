#!/usr/bin/env bash
# Tests for scripts/verify-release-archive.py. Each case builds a throwaway
# archive fixture and checks the verifier's exit status and message.
# Run:  bash scripts/test-verify-release-archive.sh
set -u

ROOT="$(git rev-parse --show-toplevel)"
VERIFY="$ROOT/scripts/verify-release-archive.py"
PYTHON="$(command -v python3)"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

PASS=0
FAIL=0

expect() {
  local desc="$1" want_rc="$2" want_text="$3"
  shift 3
  local out rc
  out="$("$@" 2>&1)"
  rc=$?
  if [ "$rc" -eq "$want_rc" ] && [[ "$out" == *"$want_text"* ]]; then
    PASS=$((PASS + 1))
  else
    FAIL=$((FAIL + 1))
    echo "FAIL: $desc (expected rc=$want_rc text='$want_text'; got rc=$rc out='$out')"
  fi
}

# make_library <root> <part-number>: write one valid definition plus its data file.
make_library() {
  local root="$1" part="$2"
  mkdir -p "$root/amplifiers/test"
  cat >"$root/amplifiers/test/$part.json" <<JSON
{"schema_version":2,"type":"amplifier","part_number":"$part",
 "manufacturer":"Test","parameters":{"gain_dB":10.0},
 "data_files":[{"type":"s_parameters","path":"$part.s2p"}]}
JSON
  : >"$root/amplifiers/test/$part.s2p"
}

make_tar() { # make_tar <source-dir> <archive>
  if [ -d "$1/component_data" ]; then
    tar -czf "$2" -C "$1" component_data
  else
    tar -czf "$2" -C "$1" .
  fi
}

make_zip() { # make_zip <source-dir> <archive>
  "$PYTHON" - "$1" "$2" <<'PY'
import os
import sys
import zipfile

source, archive = sys.argv[1:3]
with zipfile.ZipFile(archive, "w") as zf:
    for directory, _, files in os.walk(source):
        for name in files:
            full = os.path.join(directory, name)
            zf.write(full, os.path.relpath(full, source))
PY
}

verify() { # verify <archive> <library-root> [extra...]
  "$PYTHON" "$VERIFY" "$@"
}

# A valid Linux-layout tarball lists its part.
make_library "$TMP/good/component_data/library" TEST-AMP
make_tar "$TMP/good" "$TMP/good.tar.gz"
expect "accepts a tar archive with the library" 0 "TEST-AMP" \
  verify "$TMP/good.tar.gz" --library-root component_data/library --min-parts 1

# The same layout in a zip (the Windows archive nests it under its folder).
make_library "$TMP/win/rf-simulator-windows/component_data/library" WIN-AMP
make_zip "$TMP/win" "$TMP/win.zip"
expect "accepts the Windows archive layout" 0 "WIN-AMP" \
  verify "$TMP/win.zip" --library-root rf-simulator-windows/component_data/library \
  --min-parts 1

# A tar archive without component_data/library is refused.
mkdir -p "$TMP/missing"
echo "no library here" >"$TMP/missing/README.md"
make_tar "$TMP/missing" "$TMP/missing.tar.gz"
expect "rejects an archive without the library" 1 "component_data/library" \
  verify "$TMP/missing.tar.gz" --library-root component_data/library

# A malformed definition is refused instead of being listed.
make_library "$TMP/bad-json/component_data/library" BAD-AMP
printf '{not json\n' >"$TMP/bad-json/component_data/library/amplifiers/test/BAD-AMP.json"
make_tar "$TMP/bad-json" "$TMP/bad-json.tar.gz"
expect "rejects malformed definition JSON" 1 "not valid JSON" \
  verify "$TMP/bad-json.tar.gz" --library-root component_data/library

# A definition that names a missing S-parameter file is refused.
make_library "$TMP/missing-data/component_data/library" DATA-AMP
rm "$TMP/missing-data/component_data/library/amplifiers/test/DATA-AMP.s2p"
make_tar "$TMP/missing-data" "$TMP/missing-data.tar.gz"
expect "rejects a missing declared data file" 1 "missing data file" \
  verify "$TMP/missing-data.tar.gz" --library-root component_data/library

# The minimum count catches a truncated copy.
expect "rejects fewer parts than required" 1 "at least 2" \
  verify "$TMP/good.tar.gz" --library-root component_data/library --min-parts 2

echo "test-verify-release-archive: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
