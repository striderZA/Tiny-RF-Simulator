# Git Hooks

## Purpose
- Define local commit and push gates.

## Ownership
- `.githooks/pre-commit`
- `.githooks/commit-msg`
- `.githooks/pre-push`

## Local Contracts
- Hooks run only for local Git operations. Enable them per clone with `git config core.hooksPath .githooks`.
- GitHub squash merges and server-side pushes bypass hooks; PR titles must use the commit subject format.
- `pre-commit` runs clang-format 18 over the `scripts/format-dirs.sh` set only when C++ files are staged.
- `commit-msg` requires `<type>[(<scope>)][!]: <summary>`, where type is `build|chore|ci|docs|feat|fix|perf|refactor|revert|style|test`; subjects must be under 70 characters. Git-generated subjects are exempt.
- `pre-push` rejects non-fast-forward updates to existing remote refs. Ref creation, deletion, and fast-forward updates pass; an undecidable tip is allowed with a notice.
- Escape hatches: `RFSIM_ALLOW_FORCE_PUSH=1 git push ...` for one invocation, or `git push --no-verify` to skip all hooks.
- Hook files must remain mode `100755` (`git update-index --chmod=+x <path>`).

## Work Guidance
- After any edit under `.githooks/`, run `scripts/test-githooks.sh`.

## Verification
- `scripts/test-githooks.sh` is the hook regression test (its header documents the invocation).

## Child DOX Index
- None.
