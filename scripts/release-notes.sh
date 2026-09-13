#!/usr/bin/env bash
# Print the CHANGELOG.md section for a release version, for use as GitHub
# release notes.
#
#   bash scripts/release-notes.sh 0.24.0 > release-notes.md
#
# Exits non-zero if the section is missing or has no bullet content, so an
# empty release-note section fails CI instead of publishing a bare heading.
# Used by the release workflow's validate-version and release jobs.
set -euo pipefail

VERSION="${1:-}"
if [ -z "$VERSION" ]; then
  echo "usage: release-notes.sh <X.Y.Z>" >&2
  exit 2
fi

cd "$(git rev-parse --show-toplevel)"

[ -f CHANGELOG.md ] || {
  echo "ERROR: CHANGELOG.md not found" >&2
  exit 1
}

NOTES=$(awk -v version="$VERSION" '
  /^## \[/ {
    if (found) exit
    if (index($0, "## [" version "]") == 1) found=1
  }
  found { print }
' CHANGELOG.md)

if [ -z "$NOTES" ]; then
  echo "ERROR: CHANGELOG.md has no section for [$VERSION]" >&2
  exit 1
fi

if ! printf '%s\n' "$NOTES" | grep -q '^- '; then
  echo "ERROR: CHANGELOG.md section [$VERSION] has no bullet entries" >&2
  exit 1
fi

printf '%s\n' "$NOTES"
