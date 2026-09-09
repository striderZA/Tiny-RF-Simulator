# .factory/ — operator-owned factory state

## Purpose
Governance and operator controls around the factory: holdout scenario specs
(`holdout/HOLDOUT.md`), the coverage floor ratchet (`locks/floor.json`), and
scheduling entry points (`loop.sh`, `monitor.py`) that remain UNUSED until a human
watches one full lap.

## Ownership
- `holdout/` and `locks/` are protected: builders may never edit them; the floor
  only rises via merge bookkeeping or human commits.
- `loop.sh`/`monitor.py` are template-installed; do not register them with any OS
  scheduler in this repo yet.

## Local Contracts
- No `schedule.json` exists by decision (scheduling off). Creating one requires an
  explicit human request after the first successful manual lap.
- The holdout Markdown is the scenario contract; any evaluator JSON derived from it
  and the runtime host connection files stay OUTSIDE the builder-accessible
  checkout (see `harness/runtime.inputs.json` description and FACTORY.md limits).
- `.factory/consumer.json` is local machine state (gitignored): the installed
  Archon source path and pinned revision.

## Verification
- `python factory/consumer.py doctor` reports the state of installation and locks.

## Child DOX Index
- (none)
