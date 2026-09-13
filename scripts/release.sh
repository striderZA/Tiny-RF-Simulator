#!/usr/bin/env bash
# Prepare a release locally, and tag it after merge. This is the standard
# local path described in CONTRIBUTING.md ("Release Process").
set -euo pipefail

usage() {
  cat <<'EOF'
Prepare a release locally, and tag it after merge.

Usage:
  bash scripts/release.sh <X.Y.Z>              Prepare: branch, bump, gate, commit
  bash scripts/release.sh <X.Y.Z> --on-master  Prepare directly on master
  bash scripts/release.sh <X.Y.Z> --tag        After merge: tag and push

Options:
  --on-master    Commit the version bump on master instead of a release branch.
  --tag          Tag-only mode: verify HEAD is at <X.Y.Z>, create the annotated
                 tag, and push it.
  --skip-gates   Skip the format/build/test gates during prepare.
  --dry-run      Print planned actions without changing or committing anything.
  -h, --help     Show this help.

The CHANGELOG.md section for <X.Y.Z> must already exist with at least one
bullet; the script prints the exact template if it does not.
EOF
}

die() { echo "release.sh: ERROR: $*" >&2; exit 1; }
note() { printf 'release.sh: %s\n' "$*"; }
run() {
  if [ "$DRY_RUN" -eq 1 ]; then
    printf 'release.sh: [dry-run] %s\n' "$*"
  else
    "$@"
  fi
}

VERSION=""
MODE="prepare"
ON_MASTER=0
SKIP_GATES=0
DRY_RUN=0

for arg in "$@"; do
  case "$arg" in
    --on-master) ON_MASTER=1 ;;
    --tag) MODE="tag" ;;
    --skip-gates) SKIP_GATES=1 ;;
    --dry-run) DRY_RUN=1 ;;
    -h|--help) usage; exit 0 ;;
    -*) die "unknown option: $arg (try --help)" ;;
    *)
      [ -z "$VERSION" ] || die "unexpected extra argument: $arg"
      VERSION="$arg"
      ;;
  esac
done

[ -n "$VERSION" ] || { usage >&2; exit 2; }
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "version must be semantic X.Y.Z (got '$VERSION')"

cd "$(git rev-parse --show-toplevel)"

TAG="v$VERSION"
CURRENT=$(sed -n 's/.*VERSION \([0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\).*/\1/p' CMakeLists.txt | head -n1)
[ -n "$CURRENT" ] || die "could not read the current version from CMakeLists.txt"

# --- tag-only mode: run after the release PR is merged ---------------------
if [ "$MODE" = "tag" ]; then
  [ "$CURRENT" = "$VERSION" ] || die "CMakeLists.txt is at $CURRENT but you asked to tag $VERSION; merge the release PR first"
  [ -z "$(git status --porcelain)" ] || die "working tree is not clean; commit or stash first"
  TAG_BRANCH=$(git rev-parse --abbrev-ref HEAD)
  [ "$TAG_BRANCH" = "master" ] || die "tags are cut on master only (currently on '$TAG_BRANCH'); check out master at the merged release commit"
  if git rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then
    die "tag $TAG already exists"
  fi
  bash scripts/release-notes.sh "$VERSION" >/dev/null
  note "creating annotated tag $TAG on master ($(git rev-parse --short HEAD)) and pushing it"
  run git tag -a "$TAG" -m "Release $VERSION"
  run git push origin "$TAG"
  note "done; the release workflow will validate, build, package, and open a draft release."
  exit 0
fi

# --- prepare mode ----------------------------------------------------------
if [ "$VERSION" = "$CURRENT" ]; then
  die "CMakeLists.txt is already at $VERSION; nothing to prepare"
fi
if [ "$(printf '%s\n%s\n' "$CURRENT" "$VERSION" | sort -V | tail -n1)" != "$VERSION" ]; then
  die "requested $VERSION is not greater than current $CURRENT"
fi
if git rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then
  die "tag $TAG already exists"
fi

# Only an uncommitted CHANGELOG.md edit is tolerated; everything else must be clean.
DIRTY=$(git status --porcelain)
if [ -n "$DIRTY" ]; then
  UNEXPECTED=$(printf '%s\n' "$DIRTY" | grep -vE '^.{2} CHANGELOG\.md$' || true)
  [ -z "$UNEXPECTED" ] || die "working tree has changes other than CHANGELOG.md; commit or stash first"
fi

BRANCH=$(git rev-parse --abbrev-ref HEAD)
[ "$BRANCH" = "master" ] || die "prepare from master (currently on '$BRANCH')"

# The changelog section must exist before anything is modified.
if ! bash scripts/release-notes.sh "$VERSION" >/dev/null 2>&1; then
  cat >&2 <<EOF
release.sh: CHANGELOG.md has no usable section for [$VERSION].

Add this to CHANGELOG.md (above the newest existing section), fill in at least
one bullet, then re-run:

## [$VERSION] - $(date -u +%F)

### Added

- ...

### Changed

### Fixed

### Testing
EOF
  exit 1
fi

HEADING=$(grep -m1 -F "## [$VERSION]" CHANGELOG.md || true)
case "$HEADING" in
  "## [$VERSION] - "*) ;;
  *) die "CHANGELOG.md heading must read '## [$VERSION] - YYYY-MM-DD' (found: '$HEADING')" ;;
esac
TODAY=$(date -u +%F)
CHANGELOG_DATE="${HEADING##*] - }"
[ "$CHANGELOG_DATE" = "$TODAY" ] || note "warning: changelog date is $CHANGELOG_DATE but today is $TODAY"

if [ "$ON_MASTER" -eq 0 ]; then
  note "creating branch release/v$VERSION from master"
  run git switch -c "release/v$VERSION"
fi

note "bumping CMakeLists.txt: $CURRENT -> $VERSION"
if [ "$DRY_RUN" -eq 0 ]; then
  sed -i "s/^project (RfSimulator VERSION .*/project (RfSimulator VERSION $VERSION)/" CMakeLists.txt
  grep -q "project (RfSimulator VERSION $VERSION)" CMakeLists.txt \
    || die "failed to bump the version in CMakeLists.txt"
else
  note "[dry-run] sed -i 's/^project (RfSimulator VERSION .*/project (RfSimulator VERSION $VERSION)/' CMakeLists.txt"
fi

if [ "$SKIP_GATES" -eq 0 ]; then
  note "running local gates (format check, build, tests)"
  run bash scripts/format.sh --check --all
  run cmake --build build
  run ctest --test-dir build --output-on-failure
else
  note "skipping local gates (--skip-gates)"
fi

note "committing release preparation"
run git add CMakeLists.txt CHANGELOG.md
run git commit -m "chore: prepare release $TAG"

echo
note "next steps:"
if [ "$ON_MASTER" -eq 1 ]; then
  note "  1. push master:  git push origin master"
  note "  2. after the tag workflow succeeds, review the draft release on GitHub"
  note "  3. tag the release:  bash scripts/release.sh $VERSION --tag"
else
  note "  1. push and open a PR:  git push -u origin release/v$VERSION && gh pr create --fill"
  note "  2. merge the PR"
  note "  3. tag the merged master commit:  bash scripts/release.sh $VERSION --tag"
fi
