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

echo "test-githooks: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
