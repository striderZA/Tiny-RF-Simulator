# Factory operations

This application consumes a pinned complete Archon SDLC source. See
[factory/MIGRATION.md](factory/MIGRATION.md) for source installation and data mapping.

Shared workflow gates own decisions. The old autonomy dial and local receipts
cannot authorize merges. Inspect native run identity and status before responding
to a declared gate. Standing intake and unattended operation require separate
producer integration and live verification.

Project runtime scenarios: `harness/END-TO-END.md`.
Holdout scenarios: `.factory/holdout/HOLDOUT.md`.
Ordinary checks: `python harness/ci.py`.

## Installation record

- Source: `C:\Users\Jaco\.cache\factory\archon\2dad1f3ae6a438daec6155df47038ad2608dbe6f`
  (repository `coleam00/Archon`, integration revision `2dad1f3a` — supervised
  integration branch `cleanup/sdlc-workflows-only`, not merged upstream).
- Doctor: PASS — 18+ shared workflows available (see `factory/pack.json` entries).
- Provider: Archon native `~/.archon/config.yaml` → `defaultAssistant: pi`; tiers
  `small: opencode-go/glm-5.3-flash`, `medium: opencode-go/qwen3.8-flash`,
  `large: opencode-go/qwen3.8-max`, all through the Pi provider (pi CLI v0.85.1,
  authenticated via opencode-go).
- GitHub: `gh` authenticated as `striderZA`; git push via SSH remote
  `github.com:striderZA/Tiny-RF-Simulator.git`.
- Scheduling: OFF by decision. No `.factory/schedule.json`; `loop.sh` and the
  systemd example are unregistered. First lap must be watched manually
  (`archon-ship` on a small issue, merge approval enabled, discovery publication
  in preview).

## Unresolved integration limits

- **Live agent run not yet proven.** Doctor validates installation and workflow
  availability; it does not prove a Pi-agent run signs in and completes a workflow
  end to end. First lap closes this.
- **GUI runtime host not wired.** `harness/runtime.inputs.json` carries the journey
  and holdout specs, but no adapter drives `tiny-rf-simulator.exe` / `test_ui.exe`
  under `factory/runtime_host.py` yet (its `command`/`setup` slots expect
  Python/Node argv; the app is a compiled GUI binary). Until wired,
  `archon-lifecycle` runtime verification for this project runs the ordinary
  checks (`harness/ci.py`) only; scenarios are verified through the imgui_test_engine
  suite inside ctest. Wiring the host is scheduled after the first observed lap.
- **Holdout isolation is partial.** `HOLDOUT.md` ships inside the repo the builder
  checks out; treat it as a published contract. True hidden holdout requires
  evaluator JSON kept outside the builder worktree when the runtime host is wired.
- **Windows quirks.** `bash` on PATH is WSL (`$HOME=/home/...`), so harness steps
  never call bash — `harness/check.py` reimplements the CI-equivalent commands in
  Python. `test_ui.exe` needs an interactive desktop session (GLFW window).
- Mutation defects (`harness/mutations/defects.json`) remain template examples;
  calibrate real defects alongside the runtime host wiring.

Record this application's tested source SHA, candidate, native run IDs, coverage,
fresh-environment evidence and unresolved integration limits here after testing.
