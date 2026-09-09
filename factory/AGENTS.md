# factory/ — AI software factory runtime

## Purpose
The pinned Archon SDLC consumer: installs, invokes, and reports on shared factory
workflows (`factory/consumer.py`), and owns the optional app runtime host.

## Ownership
- Everything here is installed from the `ai-software-factory` template; upgrades are
  done by re-running `python <clone>/ai-software-factory/bin/factory.py init`, never
  by hand-editing. `factory/pack.json` pins the Archon source revision.
- Workflow behavior (agent prompts, gates, stage order) belongs to Archon's shared
  SDLC pack upstream — never fork copies here.

## Local Contracts
- Every AI step runs through `python factory/consumer.py run <archon-workflow>`.
  No direct coding-agent subprocesses and no factory-local workflow copies.
- Scheduling is OFF for this repo until one full lap has been watched by a human:
  no `.factory/schedule.json`, no `.factory/loop.sh` in any scheduler, no timer
  service.
- Archon provider configuration lives in the user's native `~/.archon/config.yaml`
  (provider: pi; tiers small/medium/large) — not in this directory.

## Verification
- `python factory/consumer.py doctor` and `python factory/consumer.py list`.
- `python factory/_selftest.py` for the installed modules.

## Child DOX Index
- (none)
