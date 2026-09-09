# harness/ — project checks and journey specs

## Purpose
Ordinary static/unit gates (`ci.py` + `check.py`), the runtime scenario specs
(`END-TO-END.md`), and shared-workflow input data (`runtime.inputs.json`). This is
part of the product's judge.

## Ownership
- Protected by the factory: a PR that removes or loosens an assertion or gate here
  is auto-rejected. Humans add scope via commits.

## Local Contracts
- `ci.py` runs each command as a single shell-free argv with a 600 s ceiling;
  compound `&&` commands and WSL-visible `bash` are unavailable here — that is why
  `check.py` exists as the deterministic wrapper (clang-format-18 full-set static
  check; cmake+Ninja configure/build; full ctest incl. `test_ui`).
- `check.py` must work in a fresh git worktree: configure creates `build/` and
  FetchContent dependencies from scratch.
- `END-TO-END.md` documents journeys the product performs TODAY, in plain English;
  runtime execution and evidence are owned by the shared Archon workflow, not by
  scripts in this directory (`agentcheck.py` is retired and executes nothing).

## Verification
- `python harness/ci.py` prints STATIC_OK, UNIT_PASSED tests=N, CHECKS_OK.
- The unit count must match `.factory/locks/floor.json` ratchet expectations.

## Child DOX Index
- (none)
