#!/usr/bin/env bash
# Prepare a release locally, and tag it after merge. This is the standard
# local path described in CONTRIBUTING.md ("Release Process").
#
# Prepare order: read-only checks; configure build/ if it is missing; bump the
# version token in CMakeLists.txt; run the gates on the bumped tree; create the
# release branch; commit. A failure before the commit restores the state from
# before the bump (see on_exit).
set -euo pipefail

# Sibling scripts run with the interpreter that runs this one. The next-step
# commands name the command to type: on Windows that is `sh`, because a bare
# `bash` can resolve to WSL's launcher, which cannot run this script.
SHELL_BIN="${BASH:-bash}"
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) RUNNER="sh" ;;
  *) RUNNER="bash" ;;
esac

usage() {
  cat <<'EOF'
Prepare a release locally, and tag it after merge.

Usage:
  bash scripts/release.sh <X.Y.Z>              Prepare: bump, gate, branch, commit
  bash scripts/release.sh <X.Y.Z> --on-master  Prepare directly on master
  bash scripts/release.sh <X.Y.Z> --tag        After merge: tag and push

On Windows, run the script with sh instead of bash: sh scripts/release.sh <X.Y.Z>
(WSL's bash on PATH cannot run it).

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
# Anchored to the project line, so no other VERSION text can be read as the version.
CURRENT=$(sed -n 's/^project (RfSimulator VERSION \([0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\).*/\1/p' CMakeLists.txt | head -n1)
[ -n "$CURRENT" ] || die "could not read the current version from the project line in CMakeLists.txt"

# Prepare changes the repository only after its checks pass: first the version
# bump, then the release branch and the commit. If the script stops before the
# commit (a failed gate, a failed commit, or an interrupt), on_exit restores
# CMakeLists.txt and removes the release branch if this run created it.
# CHANGELOG.md is never reset or restored: it holds the user's release notes.
MUTATED=0
CREATED_BRANCH=0
PRE_COMMIT_HEAD=""
on_exit() {
  local rc=$?
  if [ "$rc" -eq 0 ] || [ "$MUTATED" -eq 0 ]; then
    return 0
  fi
  # A prepare commit that already landed (HEAD moved) is kept: rolling back
  # would delete its branch and orphan the commit.
  CUR_HEAD=$(git rev-parse HEAD 2>/dev/null || true)
  if [ -n "$PRE_COMMIT_HEAD" ] && [ "$CUR_HEAD" != "$PRE_COMMIT_HEAD" ]; then
    echo "release.sh: stopped after the prepare commit $(git rev-parse --short HEAD) landed; nothing was rolled back" >&2
    return 0
  fi
  echo "release.sh: prepare stopped before the commit; restoring the pre-prepare state" >&2
  git checkout -q -- CMakeLists.txt >/dev/null 2>&1 || true
  if [ "$CREATED_BRANCH" -eq 1 ]; then
    git switch -q master >/dev/null 2>&1 || true
    git branch -q -D "release/$TAG" >/dev/null 2>&1 || true
  fi
  if git diff --quiet -- CMakeLists.txt && [ -z "$(git branch --list "release/$TAG")" ]; then
    echo "release.sh: restored: on $(git rev-parse --abbrev-ref HEAD) at $(git rev-parse --short HEAD), CMakeLists.txt unchanged, nothing committed" >&2
  else
    echo "release.sh: ERROR: restore incomplete; run: git restore CMakeLists.txt && git switch master && git branch -D release/$TAG" >&2
  fi
}
trap on_exit EXIT

# --- tag-only mode: run after the release PR is merged ---------------------
if [ "$MODE" = "tag" ]; then
  [ "$CURRENT" = "$VERSION" ] || die "CMakeLists.txt is at $CURRENT but you asked to tag $VERSION; merge the release PR first"
  [ -z "$(git status --porcelain)" ] || die "working tree is not clean; commit or stash first"
  TAG_BRANCH=$(git rev-parse --abbrev-ref HEAD)
  [ "$TAG_BRANCH" = "master" ] || die "tags are cut on master only (currently on '$TAG_BRANCH'); check out master at the merged release commit"
  if git rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then
    die "tag $TAG already exists; read 'Failed release tag' in CONTRIBUTING.md before deleting it"
  fi
  "$SHELL_BIN" scripts/release-notes.sh "$VERSION" >/dev/null
  note "creating annotated tag $TAG on master ($(git rev-parse --short HEAD)) and pushing it"
  run git tag -a "$TAG" -m "Release $VERSION"
  run git push origin "$TAG"
  note "done; the release workflow will validate, build, package, and open a draft release."
  exit 0
fi

# --- prepare mode ----------------------------------------------------------

# CMakeLists.txt already has the requested version: report which state the
# repository is in and the next step, then stop.
explain_existing() {
  local branch sha discard
  branch=$(git rev-parse --abbrev-ref HEAD)
  sha=$(git rev-parse --short HEAD)
  if ! git diff --quiet -- CMakeLists.txt; then
    discard="git restore CMakeLists.txt"
    if [ "$branch" != "master" ]; then
      discard="$discard && git switch master && git branch -D release/$TAG"
    fi
    die "an earlier prepare stopped after the version bump: CMakeLists.txt is at $VERSION with an uncommitted edit on '$branch'. Nothing was committed. To discard it: $discard. Then re-run."
  fi
  case "$branch" in
    master)
      die "$VERSION is already committed on master ($sha); nothing to prepare. Next: push master if it is not pushed, then tag: $RUNNER scripts/release.sh $VERSION --tag" ;;
    "release/$TAG")
      die "$VERSION is already committed on $branch ($sha); nothing to prepare. Next: git push -u origin $branch && gh pr create --fill, merge the PR, then tag on master: $RUNNER scripts/release.sh $VERSION --tag" ;;
    *)
      die "CMakeLists.txt is already at $VERSION on '$branch' ($sha); prepare runs from master. Next: git switch master, then re-run." ;;
  esac
}

if [ "$VERSION" = "$CURRENT" ]; then
  explain_existing
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
  if [ -n "$UNEXPECTED" ]; then
    printf '%s\n' "$UNEXPECTED" >&2
    die "working tree has changes other than CHANGELOG.md (listed above); commit, stash, or remove them first"
  fi
fi

BRANCH=$(git rev-parse --abbrev-ref HEAD)
[ "$BRANCH" = "master" ] || die "prepare from master (currently on '$BRANCH')"

if [ "$ON_MASTER" -eq 0 ] && git rev-parse -q --verify "refs/heads/release/$TAG" >/dev/null; then
  BRANCH_SHA=$(git rev-parse --short "refs/heads/release/$TAG")
  BRANCH_CMAKE=$(git show "refs/heads/release/$TAG:CMakeLists.txt" 2>/dev/null || true)
  if [[ "$BRANCH_CMAKE" == *"project (RfSimulator VERSION $VERSION)"* ]]; then
    die "branch release/$TAG already holds the prepared release ($BRANCH_SHA); nothing to prepare. Next: git switch release/$TAG, push it, open the PR, merge it, then tag on master."
  fi
  die "branch release/$TAG already exists at $BRANCH_SHA and does not hold the $VERSION bump. If it is a leftover, delete it with: git branch -D release/$TAG; then re-run."
fi

# The changelog section must exist before anything is modified.
if ! "$SHELL_BIN" scripts/release-notes.sh "$VERSION" >/dev/null 2>&1; then
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

# Configure build/ before anything is modified, so a fresh worktree fails here
# rather than after the bump.
CONFIGURE_CMD=(cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc)
if [ "$SKIP_GATES" -eq 0 ] && [ ! -f build/CMakeCache.txt ]; then
  note "build/ is not configured in this worktree; configuring it (about 90 s the first time)"
  if [ "$DRY_RUN" -eq 1 ]; then
    printf 'release.sh: [dry-run] %s\n' "${CONFIGURE_CMD[*]}"
  elif ! "${CONFIGURE_CMD[@]}"; then
    die "configuring build/ failed; nothing was changed. Fix the CMake error, or run '${CONFIGURE_CMD[*]}' by hand, then re-run."
  fi
fi

# --- mutation: from here on, a failure before the commit is rolled back -----
if [ "$DRY_RUN" -eq 0 ]; then MUTATED=1; fi
note "bumping CMakeLists.txt: $CURRENT -> $VERSION (only the version token changes)"
# GNU sed on Windows (Git for Windows, MSYS, Cygwin) rewrites CRLF as LF unless
# --binary is set, which would change every line of CMakeLists.txt.
SED_INPLACE=(-i)
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) SED_INPLACE=(-i -b) ;; esac
if [ "$DRY_RUN" -eq 0 ]; then
  CR_BEFORE=$(tr -cd '\r' < CMakeLists.txt | wc -c | tr -d ' ')
  sed "${SED_INPLACE[@]}" -E "s/^(project \(RfSimulator VERSION )[0-9]+\.[0-9]+\.[0-9]+/\1$VERSION/" CMakeLists.txt
  grep -qE "^project \(RfSimulator VERSION $VERSION\)" CMakeLists.txt \
    || die "failed to bump the version in CMakeLists.txt"
  [ "$(tr -cd '\r' < CMakeLists.txt | wc -c | tr -d ' ')" = "$CR_BEFORE" ] \
    || die "the version bump changed CMakeLists.txt line endings"
  ADDED=""
  REMOVED=""
  NUMSTAT=$(git diff --numstat -- CMakeLists.txt)
  read -r ADDED REMOVED _ <<<"$NUMSTAT" || true
  [ "$ADDED $REMOVED" = "1 1" ] \
    || die "expected exactly one replaced line in CMakeLists.txt (git numstat: ${NUMSTAT:-none})"
else
  note "[dry-run] replace the version token on the project line of CMakeLists.txt"
fi

# Run one gate. A failure exits, and on_exit restores the bump.
gate() {
  local label="$1"
  shift
  note "gate: $label"
  if [ "$DRY_RUN" -eq 1 ]; then
    printf 'release.sh: [dry-run] %s\n' "$*"
  elif ! "$@"; then
    die "the $label gate failed; fix it and re-run"
  fi
}

if [ "$SKIP_GATES" -eq 0 ]; then
  PRE_GATE_DIFF=$(git diff --name-only)
  note "running local gates on the bumped tree (format check, build, tests)"
  gate "format check" "$SHELL_BIN" scripts/format.sh --check --all
  gate "build" cmake --build build
  gate "tests" ctest --test-dir build --output-on-failure
  if [ "$DRY_RUN" -eq 0 ] && [ "$(git diff --name-only)" != "$PRE_GATE_DIFF" ]; then
    die "the gates changed tracked files; review them with git status"
  fi
else
  note "skipping local gates (--skip-gates)"
fi

if [ "$ON_MASTER" -eq 0 ]; then
  note "creating branch release/$TAG from master"
  run git switch -c "release/$TAG"
  if [ "$DRY_RUN" -eq 0 ]; then CREATED_BRANCH=1; fi
fi

note "committing release preparation"
if [ "$DRY_RUN" -eq 0 ]; then PRE_COMMIT_HEAD=$(git rev-parse HEAD); fi
run git commit -m "chore: prepare release $TAG" -- CMakeLists.txt CHANGELOG.md

echo
if [ "$DRY_RUN" -eq 0 ]; then
  note "prepared $TAG on $(git rev-parse --abbrev-ref HEAD) at $(git rev-parse --short HEAD)"
fi
note "next steps:"
if [ "$ON_MASTER" -eq 1 ]; then
  note "  1. push master:  git push origin master"
  note "  2. tag the release:  $RUNNER scripts/release.sh $VERSION --tag"
  note "  3. after the tag workflow succeeds, review the draft release on GitHub"
else
  note "  1. push and open a PR:  git push -u origin release/$TAG && gh pr create --fill"
  note "  2. merge the PR"
  note "  3. tag the merged master commit:  $RUNNER scripts/release.sh $VERSION --tag"
  note "  4. after the tag workflow succeeds, review the draft release on GitHub"
fi
