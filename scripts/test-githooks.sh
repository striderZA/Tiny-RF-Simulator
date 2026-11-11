#!/usr/bin/env bash
# Fixture + end-to-end tests for the local git hooks in .githooks/.
# Run:  bash scripts/test-githooks.sh
set -u

ROOT="$(git rev-parse --show-toplevel)"
HOOK="$ROOT/.githooks/commit-msg"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

PASS=0
FAIL=0

# check <expected-exit> <label>  — message on stdin
check() {
  local expect="$1" label="$2" message
  message="$(cat)"
  printf '%s\n' "$message" >"$TMP/msg"
  local got=0
  if ! bash "$HOOK" "$TMP/msg" >/dev/null 2>&1; then
    got=1
  fi
  if [ "$got" = "$expect" ]; then
    PASS=$((PASS + 1))
  else
    FAIL=$((FAIL + 1))
    echo "FAIL: $label (expected exit $expect, got $got)"
  fi
}

check 0 "feat subject" <<'EOF'
feat: add 2:1 SPDT RF switch engine
EOF
check 0 "scoped subject" <<'EOF'
test(pfb): cover grid invariance
EOF
check 0 "breaking-change marker" <<'EOF'
refactor(api)!: drop legacy tone accessors
EOF
check 0 "body is allowed" <<'EOF'
fix: keep spectrum noise jitter off tone peaks

The jitter must not perturb deterministic tones.
EOF
check 0 "comment block stripped" <<'EOF'
fix: reject multi-input node on analyzer path

# Please enter the commit message for your changes. Lines starting
# with '#' will be ignored, and an empty message aborts the commit.
EOF
check 0 "verbose scissors section stripped" <<'EOF'
docs: state the network-analyzer path rule

# ------------------------ >8 ------------------------
diff --git a/a.cpp b/a.cpp
-docs: not a subject
EOF
check 0 "merge commit" <<'EOF'
Merge pull request #141 from striderZA/feat/rf-switch-spdt-2to1

Body of the generated merge message.
EOF
check 0 "revert commit" <<'EOF'
Revert "docs: add attenuator component design spec"
EOF
check 0 "autosquash commit" <<'EOF'
fixup! feat: add SPDT RF switch engine
EOF
check 0 "empty message left to git" <<'EOF'

EOF

check 1 "missing type" <<'EOF'
Update the switch symbol
EOF
check 1 "unknown type" <<'EOF'
feature: add SPDT RF switch engine
EOF
check 1 "uppercase type" <<'EOF'
Feat: add SPDT RF switch engine
EOF
check 1 "missing space after colon" <<'EOF'
fix:tone list
EOF
check 1 "missing colon" <<'EOF'
fix add SPDT RF switch engine
EOF
check 1 "empty subject text" <<'EOF'
fix:   
EOF
check 1 "uppercase scope" <<'EOF'
fix(PFB): cover grid invariance
EOF
check 1 "trailing-only-subject space" <<'EOF'
chore:
EOF
check 1 "subject too long" <<'EOF'
docs: record the two-input dirty-check promotion trigger where the prologue is documented
EOF

# A CRLF-authored message (Windows editor) must be measured without the CR: this
# subject is exactly 69 characters, so counting the CR would reject it.
printf 'feat: %s\r\n' "$(printf 'x%.0s' {1..63})" >"$TMP/crlf"
if bash "$HOOK" "$TMP/crlf" >/dev/null 2>&1; then
  PASS=$((PASS + 1))
else
  FAIL=$((FAIL + 1))
  echo "FAIL: CRLF 69-character subject was rejected"
fi

# End-to-end: the hook must actually fire from `git commit` when it is the active
# commit-msg hook. It is copied to a scratch hooksPath so the sibling pre-commit
# hook (which resolves paths against the repository it runs in) stays out of the way.
E2E="$TMP/e2e"
mkdir -p "$TMP/hooks"
cp "$HOOK" "$TMP/hooks/commit-msg"
git init -q "$E2E"
git -C "$E2E" config core.hooksPath "$TMP/hooks"
git -C "$E2E" config commit.gpgsign false
git -C "$E2E" config user.name "Hook Test"
git -C "$E2E" config user.email "hook@test.invalid"
echo scratch >"$E2E/file.txt"
git -C "$E2E" add file.txt

if git -C "$E2E" commit -q -m "not a conventional subject" 2>/dev/null; then
  FAIL=$((FAIL + 1))
  echo "FAIL: e2e bad message was accepted"
else
  PASS=$((PASS + 1))
fi

# ...and it must have aborted the commit, not merely printed a complaint.
if [ "$(git -C "$E2E" rev-list --count --all)" = "0" ]; then
  PASS=$((PASS + 1))
else
  FAIL=$((FAIL + 1))
  echo "FAIL: e2e rejected message still produced a commit"
fi

if git -C "$E2E" commit -q -m "feat: accept a conventional subject"; then
  PASS=$((PASS + 1))
else
  FAIL=$((FAIL + 1))
  echo "FAIL: e2e conventional message was rejected"
fi

# A merge in progress is exempt even when its message is author-supplied.
printf 'Merge branch master into the feature branch\n' >"$TMP/merge-msg"
git -C "$E2E" rev-parse HEAD >"$E2E/.git/MERGE_HEAD"
if (cd "$E2E" && bash "$HOOK" "$TMP/merge-msg") >/dev/null 2>&1; then
  PASS=$((PASS + 1))
else
  FAIL=$((FAIL + 1))
  echo "FAIL: in-progress merge was blocked"
fi
rm -f "$E2E/.git/MERGE_HEAD"

# --- pre-push: the force-push guard -------------------------------------------
# Rewriting an existing remote ref (a non-fast-forward update) must be rejected;
# creating a ref, deleting a ref, and fast-forwarding must not be. The fixture is a
# real repo with a real bare remote, so the ancestry checks run on real objects, and
# the hook is run from that repo's working directory exactly as git runs it.
PUSH_HOOK="$ROOT/.githooks/pre-push"
PUSH="$TMP/push"
PUSH_HOOKS="$TMP/hooks-push"
ZERO_SHA=0000000000000000000000000000000000000000

check_push() {
  local expect="$1" label="$2"
  shift 2
  local got=0
  if ! printf '%s\n' "$@" | (cd "$PUSH/work" && bash "$PUSH_HOOK" origin) >/dev/null 2>&1; then
    got=1
  fi
  if [ "$got" = "$expect" ]; then
    PASS=$((PASS + 1))
  else
    FAIL=$((FAIL + 1))
    echo "FAIL: pre-push ${label} (expected exit ${expect}, got ${got})"
  fi
}

git init -q --bare "$PUSH/remote.git"
git init -q "$PUSH/work"
git -C "$PUSH/work" config user.name "Hook Test"
git -C "$PUSH/work" config user.email "hook@test.invalid"
git -C "$PUSH/work" config commit.gpgsign false
echo one >"$PUSH/work/a.txt"
git -C "$PUSH/work" add a.txt
git -C "$PUSH/work" commit -q -m "feat: first commit"
git -C "$PUSH/work" remote add origin "$PUSH/remote.git"
git -C "$PUSH/work" push -q origin HEAD:refs/heads/main

WORK_BRANCH="$(git -C "$PUSH/work" symbolic-ref --short HEAD)"
BASE="$(git -C "$PUSH/work" rev-parse HEAD)"

# A descendant of the published tip (fast-forward).
echo two >>"$PUSH/work/a.txt"
git -C "$PUSH/work" add a.txt
git -C "$PUSH/work" commit -q -m "feat: second commit"
FF="$(git -C "$PUSH/work" rev-parse HEAD)"

# A rewrite of the published tip: same parent as BASE, so BASE is unreachable from it.
git -C "$PUSH/work" checkout -q -b rewritten "$BASE"
git -C "$PUSH/work" commit -q --amend -m "feat: amended first commit"
REW="$(git -C "$PUSH/work" rev-parse HEAD)"
git -C "$PUSH/work" checkout -q "$WORK_BRANCH"

check_push 0 "fast-forward update" "refs/heads/main ${FF} refs/heads/main ${BASE}"
check_push 0 "new remote branch" "refs/heads/topic ${FF} refs/heads/topic ${ZERO_SHA}"
check_push 0 "remote ref deletion" "refs/heads/main ${ZERO_SHA} refs/heads/main ${BASE}"
check_push 0 "unchanged tip" "refs/heads/main ${BASE} refs/heads/main ${BASE}"
check_push 0 "unverifiable tip (not a guess)" "refs/heads/main ${FF} refs/heads/main 1111111111111111111111111111111111111111"
check_push 1 "force push" "refs/heads/main ${REW} refs/heads/main ${BASE}"
check_push 1 "force push mixed with a fast-forward" \
  "refs/heads/main ${FF} refs/heads/main ${BASE}" \
  "refs/heads/topic ${REW} refs/heads/topic ${BASE}"

# The documented per-push override must let the same update through.
if printf '%s\n' "refs/heads/main ${REW} refs/heads/main ${BASE}" |
  (cd "$PUSH/work" && RFSIM_ALLOW_FORCE_PUSH=1 bash "$PUSH_HOOK" origin) >/dev/null 2>&1; then
  PASS=$((PASS + 1))
else
  FAIL=$((FAIL + 1))
  echo "FAIL: pre-push override did not allow the rewrite"
fi

# End-to-end through real git pushes, with the hook active via core.hooksPath.
mkdir -p "$PUSH_HOOKS"
cp "$PUSH_HOOK" "$PUSH_HOOKS/pre-push"
git -C "$PUSH/work" config core.hooksPath "$PUSH_HOOKS"

if git -C "$PUSH/work" push -q origin "refs/heads/${WORK_BRANCH}:refs/heads/main"; then
  PASS=$((PASS + 1))
else
  FAIL=$((FAIL + 1))
  echo "FAIL: pre-push blocked a fast-forward push"
fi

if git -C "$PUSH/work" push -q --force origin "refs/heads/rewritten:refs/heads/main" 2>/dev/null; then
  FAIL=$((FAIL + 1))
  echo "FAIL: pre-push allowed a force push"
else
  PASS=$((PASS + 1))
fi

if [ "$(git -C "$PUSH/remote.git" rev-parse refs/heads/main)" = "$FF" ]; then
  PASS=$((PASS + 1))
else
  FAIL=$((FAIL + 1))
  echo "FAIL: a rejected force push still moved the remote ref"
fi

if RFSIM_ALLOW_FORCE_PUSH=1 git -C "$PUSH/work" push -q --force origin "refs/heads/rewritten:refs/heads/main"; then
  PASS=$((PASS + 1))
else
  FAIL=$((FAIL + 1))
  echo "FAIL: pre-push override did not let the force push through"
fi

if [ "$(git -C "$PUSH/remote.git" rev-parse refs/heads/main)" = "$REW" ]; then
  PASS=$((PASS + 1))
else
  FAIL=$((FAIL + 1))
  echo "FAIL: an overridden force push did not move the remote ref"
fi

# --- pre-commit: staged size cap ----------------------------------------------
# The cap counts insertions + deletions of the staged diff; binary files are excluded and a deleted
# file counts as one line. Warn above 100 lines, block above 200.
# Each case runs the real hook, as shipped, from a scratch repository.
SIZE_HOOKS="$TMP/hooks-size"
mkdir -p "$SIZE_HOOKS"
cp "$ROOT/.githooks/pre-commit" "$SIZE_HOOKS/pre-commit"

# size_repo <name>: scratch repo with a committed 1200-line file. The format check
# sources two scripts, so the scratch repo carries copies of them.
size_repo() {
  local dir="$TMP/size-$1"
  git init -q "$dir"
  git -C "$dir" config user.name "Hook Test"
  git -C "$dir" config user.email "hook@test.invalid"
  git -C "$dir" config commit.gpgsign false
  mkdir -p "$dir/scripts"
  cp "$ROOT/scripts/format-dirs.sh" "$ROOT/scripts/clang-format-resolve.sh" "$dir/scripts/"
  seq 1 1200 >"$dir/legacy.txt"
  git -C "$dir" add legacy.txt
  git -C "$dir" commit -q --no-verify -m "feat: seed fixture"
  git -C "$dir" config core.hooksPath "$SIZE_HOOKS"
  echo "$dir"
}

# size_case <pass|warn|block> <label> <name> <setup>: runs the hook on the index <setup> stages.
size_case() {
  local expect="$1" label="$2" dir out rc got
  dir="$(size_repo "$3")"
  (cd "$dir" && eval "$4")
  out="$(cd "$dir" && bash "$SIZE_HOOKS/pre-commit" 2>&1)"
  rc=$?
  if [ "$rc" = 0 ] && printf '%s\n' "$out" | grep -q 'warning:'; then
    got=warn
  elif [ "$rc" = 0 ]; then
    got=pass
  elif printf '%s\n' "$out" | grep -q 'limit is'; then
    got=block
  else
    got="error (exit $rc)"
  fi
  if [ "$got" = "$expect" ]; then
    PASS=$((PASS + 1))
  else
    FAIL=$((FAIL + 1))
    echo "FAIL: pre-commit size cap: ${label} (expected ${expect}, got ${got})"
  fi
}

size_case pass "100 lines passes silently" small 'seq 1 100 >small.txt && git add small.txt'
size_case warn "101 lines warns" warn-low 'seq 1 101 >warn-low.txt && git add warn-low.txt'
size_case warn "exactly the block limit warns" at-cap 'seq 1 200 >at-cap.txt && git add at-cap.txt'
size_case block "one line over the block limit is blocked" over-cap 'seq 1 201 >over-cap.txt && git add over-cap.txt'
size_case block "deletions count toward the cap" deletions ': >legacy.txt && git add legacy.txt'
size_case pass "deleting a whole file counts as one line" file-delete 'git rm -q legacy.txt'
size_case pass "binary files are not counted" binary '{ printf "\0"; seq 1 2000; } >blob.bin && git add blob.bin'
size_case pass "merge in progress is exempt" merge 'seq 1 1001 >merged.txt && git add merged.txt && git rev-parse HEAD >.git/MERGE_HEAD'

# End-to-end: a real `git commit` over the cap aborts without a commit; the documented
# override lets the same commit through.
E2E_SIZE="$(size_repo e2e)"
seq 1 1001 >"$E2E_SIZE/e2e-big.txt"
git -C "$E2E_SIZE" add e2e-big.txt
before="$(git -C "$E2E_SIZE" rev-parse HEAD)"
e2e_out="$(git -C "$E2E_SIZE" commit -q -m "feat: add big file" 2>&1)"
if printf '%s\n' "$e2e_out" | grep -q 'limit is' && [ "$(git -C "$E2E_SIZE" rev-parse HEAD)" = "$before" ]; then
  PASS=$((PASS + 1))
else
  FAIL=$((FAIL + 1))
  echo "FAIL: e2e over-cap commit was not aborted by the size cap"
fi

if RFSIM_ALLOW_LARGE_COMMIT=1 git -C "$E2E_SIZE" commit -q -m "feat: add big file" >/dev/null 2>&1; then
  PASS=$((PASS + 1))
else
  FAIL=$((FAIL + 1))
  echo "FAIL: RFSIM_ALLOW_LARGE_COMMIT did not let the over-cap commit through"
fi


echo "test-githooks: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
