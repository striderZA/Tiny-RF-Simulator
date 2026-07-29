#!/usr/bin/env bash
# Portable install of clang-format 18.1.8 for contributors/CI-parity checks when the
# system package manager doesn't offer clang-format-18 (e.g. Windows, older macOS).
# Installs via pip into a fixed cache dir so scripts/format.sh and .githooks/pre-commit
# can find it without any extra configuration.
set -euo pipefail

DEST="$HOME/.cache/clang-format-18"

PY=""
for candidate in python3 python; do
  if command -v "$candidate" >/dev/null 2>&1; then
    PY="$candidate"
    break
  fi
done
if [ -z "$PY" ]; then
  echo "install-clang-format: no python3/python found on PATH" >&2
  exit 1
fi

"$PY" -m pip install --target "$DEST" clang-format==18.1.8

BIN="$DEST/clang_format/data/bin/clang-format"
[ -f "${BIN}.exe" ] && BIN="${BIN}.exe"
if [ ! -x "$BIN" ] && [ ! -f "$BIN" ]; then
  echo "install-clang-format: install succeeded but binary not found at $BIN" >&2
  exit 1
fi

echo "clang-format 18 installed at: $BIN"
"$BIN" --version
