# AGENTS.md

AI coding agent guidance for the RF Simulator codebase.

All project conventions — development setup, build commands, architecture, code style, and git workflow — are documented in [CONTRIBUTING.md](CONTRIBUTING.md). Read that file before making changes.

Additional references:

- [README.md](README.md) — project overview, features, quick start
- [Architecture overview](openwiki/architecture/overview.md) — full architecture deep-dive (engine+widget pattern, signal chain, subcircuit groups, dirty-flag caching)
- [`.clang-format`](.clang-format) — code style rules
- [`.github/workflows/release.yml`](.github/workflows/release.yml) — release-tag validation and packaging configuration (pull requests run no CI pipeline)

# DOX framework

- DOX is highly performant AGENTS.md hierarchy installed here
- Agent must follow DOX instructions across any edits

## Core Contract

- AGENTS.md files are binding work contracts for their subtrees
- Work products, source materials, instructions, records, assets, and durable docs must stay understandable from the nearest applicable AGENTS.md plus every parent AGENTS.md above it

## Read Before Editing

1. Read the root AGENTS.md
2. Identify every file or folder you expect to touch
3. Walk from the repository root to each target path
4. Read every AGENTS.md found along each route
5. If a parent AGENTS.md lists a child AGENTS.md whose scope contains the path, read that child and continue from there
6. Use the nearest AGENTS.md as the local contract and parent docs for repo-wide rules
7. If docs conflict, the closer doc controls local work details, but no child doc may weaken DOX

Do not rely on memory. Re-read the applicable DOX chain in the current session before editing.

## Update After Editing

Every meaningful change requires a DOX pass before the task is done.

Update the closest owning AGENTS.md when a change affects:

- purpose, scope, ownership, or responsibilities
- durable structure, contracts, workflows, or operating rules
- required inputs, outputs, permissions, constraints, side effects, or artifacts
- user preferences about behavior, communication, process, organization, or quality
- AGENTS.md creation, deletion, move, rename, or index contents

Update parent docs when parent-level structure, ownership, workflow, or child index changes. Update child docs when parent changes alter local rules. Remove stale or contradictory text immediately. Small edits that do not change behavior or contracts may leave docs unchanged, but the DOX pass still must happen.

## Hierarchy

- Root AGENTS.md is the DOX rail: project-wide instructions, global preferences, durable workflow rules, and the top-level Child DOX Index
- Child AGENTS.md files own domain-specific instructions and their own Child DOX Index
- Each parent explains what its direct children cover and what stays owned by the parent
- The closer a doc is to the work, the more specific and practical it must be

## Child Doc Shape

- Create a child AGENTS.md when a folder becomes a durable boundary with its own purpose, rules, responsibilities, workflow, materials, or quality standards
- Work Guidance must reflect the current standards of the project or user instructions; if there are no specific standards or instructions yet, leave it empty
- Verification must reflect an existing check; if no verification framework exists yet, leave it empty and update it when one exists

Default section order:
- Purpose
- Ownership
- Local Contracts
- Work Guidance
- Verification
- Child DOX Index

## Style

- Keep docs concise, current, and operational
- Document stable contracts, not diary entries
- Put broad rules in parent docs and concrete details in child docs
- Prefer direct bullets with explicit names
- Do not duplicate rules across many files unless each scope needs a local version
- Delete stale notes instead of explaining history
- Trim obvious statements, repeated rules, misplaced detail, and warnings for risks that no longer exist

## Closeout

1. Re-check changed paths against the DOX chain
2. Update nearest owning docs and any affected parents or children
3. Refresh every affected Child DOX Index
4. Remove stale or contradictory text
5. Run existing verification when relevant
6. Report any docs intentionally left unchanged and why
## User Preferences

- The PFB channelizer is physically downstream of an RF ADC only; direct RF-chain-to-PFB links must be rejected.

- ADC DDC decimation is configurable as 1/2/4/8 and NCO tuning is stored as a normalized factor of the ADC input sample rate; legacy ADC state defaults to decimation 2 and NCO +0.25×Fs.
- PFB channelizers default to critical sampling (1x) and support a persisted 2x oversampling ratio; channel output `fs_Hz` is `ratio * input Fs / M`, with channel centers unchanged and usable channel bandwidth scaled by the ratio.
- Spectrum-display noise jitter is a cosmetic effect on the noise floor only; it must never perturb deterministic signal tones, so tone peaks stay fixed frame to frame.
- The spectrum analyzer's RBW and VBW controls span 1 kHz – 100 MHz and are labelled in kHz, so the 1 kHz floor is typeable without fractional MHz values. The range lives at the widget's `utils::inputFrequency()` call (its trailing `displayUnit_Hz` picks the unit); the engine setters stay unclamped so tests and API callers can drive any value.
- When the user requests a durable behavior change, record it here or in the relevant child AGENTS.md
- Superpowers plan/spec documents are working materials and must not be committed.
- Never run filesystem-wide searches (`find /`, `dir /s`, global greps from the drive root) — they hang headless agent runs past their watchdog. A fresh git worktree has no local `build/`; dependency *sources* are browsable in the canonical checkout's `build/_deps/<name>-src`, and a fresh checkout can configure deps itself with `cmake -B build -G Ninja` (~90 s). Read `CMakeLists.txt` `FetchContent_Declare` pins for versions.

## Release Contract

- Releases are prepared by default on a `release/vX.Y.Z` branch merged to `master` via pull request; `scripts/release.sh --on-master` is the sanctioned direct-to-master alternative. Either way the annotated `vX.Y.Z` tag is created only on `master`, after the version bump has landed.
- `scripts/release.sh <X.Y.Z>` is the standard local path: it prepares (branch, version bump, gates, commit) and, with `--tag`, cuts and pushes the annotated tag on `master`. The `CHANGELOG.md` section for the version must already exist with at least one bullet, or it refuses to run.
- Invoke that script as `sh scripts/release.sh …`, not `bash`: on this Windows workstation `bash` on `PATH` is WSL's (`C:\Windows\System32\bash.exe`), whose `PATH` has no `cmake`/`ctest`, so the build gate dies at `release.sh` line 34 — while `sh` is Git for Windows' MSYS bash, where the toolchain resolves. That MSYS `sed -i` also rewrites `CMakeLists.txt` — mixed-EOL, most lines CRLF but the `project (RfSimulator VERSION …)` line bare LF — as LF-only, turning the one-line version bump into a ~270-line diff. Read the committed bytes with `git cat-file blob HEAD~1:CMakeLists.txt` — from the prep commit, `HEAD` is the `sed`-rewritten commit and `HEAD~1` is `master` — since a text pattern that assumes either ending alone misses the bump line, and require `git diff HEAD~1 --stat -- CMakeLists.txt` to print `CMakeLists.txt | 2 +-` (the bad `HEAD` prints ~270). If it does not, `git checkout HEAD~1 -- CMakeLists.txt`, re-apply the bump by byte-replacing that whole line, and `git commit --amend --no-edit` before pushing.
- Pull requests run no CI pipeline (`.github/workflows/ci.yml` was removed); all automated validation runs in `.github/workflows/release.yml` at tag time: format check and AddressSanitizer on every tag, plus a `strict-build` matrix whose leg list `classify-release` selects from the tag (Linux GCC Debug only on patch tags; the full four-way matrix on minor/major tags).
- `.github/workflows/release.yml` validates tag versions and requires a matching changelog section before running the release matrix or creating a draft. `scripts/release-notes.sh <X.Y.Z>` is the single changelog-section extractor used by the workflow and the local script; it rejects an empty section.
- The `package` job builds the Linux/Windows artifacts attached to a GitHub release with `CMAKE_BUILD_TYPE=Release` and validates that Release build configuration with CTest (non-UI, plus the MinGW `TEST_CASE` registration floor on Windows, set once as the workflow's `MINGW_TEST_CASE_FLOOR`); it runs on every tag, so the shipped configuration is always exercised. CI does not launch the packaged GUI executable itself. Debug builds are validation-only and are never shipped.
- The clang-format file set is defined once in `scripts/format-dirs.sh` (shared by `scripts/format.sh`, `.githooks/pre-commit`, and the workflow's `format` job).
- `CHANGELOG.md` is the source of truth for GitHub release descriptions.

## Git Hooks

- `.githooks/` holds the local commit and push gates, enabled per clone with `git config core.hooksPath .githooks`. They run for local git operations only; GitHub-side squash merges and server-side pushes bypass them, so PR titles must carry the same subject format.
- `.githooks/pre-commit` rejects staged C++ that fails clang-format 18 over the `scripts/format-dirs.sh` directory set; `.githooks/commit-msg` rejects a subject that is not `<type>[(<scope>)][!]: <summary>` under 70 characters, with `type` from `build|chore|ci|docs|feat|fix|perf|refactor|revert|style|test`. Git-generated `Merge …`, `Revert …`, `fixup! …`, and `squash! …` subjects are exempt, as is a merge in progress; an optional body is allowed.
- `.githooks/pre-push` rejects any push that would update an existing remote ref with a non-fast-forward — the force-push case — since that history is what open PRs and other clones point at. Creating, deleting, and fast-forwarding a ref pass, and a tip whose ancestry cannot be decided locally (shallow or partial clone) is allowed with a notice rather than guessed at. The escape hatch is per invocation: `RFSIM_ALLOW_FORCE_PUSH=1 git push …`, or `git push --no-verify` to skip every hook.
- `CONTRIBUTING.md` (Clone & Build, Git Workflow > Commits and Pushing) is the human-facing statement of these contracts. `scripts/test-githooks.sh` is the hook regression test; run it after editing anything under `.githooks/`.
- Hook files must stay mode `100755` in git (`git update-index --chmod=+x <path>`): git skips a non-executable hook on POSIX clones without any error.

## Child DOX Index

- [common/AGENTS.md](common/AGENTS.md) — Header-only data model shared by all modules (`SignalNode`, `Spectrum`, `IComponentEngine`, `Group`, `GroupBoundaryPin`, etc.)
- [node_graph/AGENTS.md](node_graph/AGENTS.md) — Topology-only graph engine + ImNodes editor widget, schematic symbols, and the shared `rewireComponentInputs()` DSP pass
- [power_meter/AGENTS.md](power_meter/AGENTS.md) — UI-independent total-power measurement engine and singleton observer panel
- [app/AGENTS.md](app/AGENTS.md) — Application orchestrator (`RfSimulatorApp`, `ComponentRegistry`, `InspectorPanel`)
- [tests/AGENTS.md](tests/AGENTS.md) — Catch2 unit + benchmark tests
- [help/AGENTS.md](help/AGENTS.md) — Help window widget with data-driven quick reference content
- [layout/AGENTS.md](layout/AGENTS.md) — Exe-relative ImGui layout persistence (default + named presets)
- [tutorial/AGENTS.md](tutorial/AGENTS.md) — Guided first-run walkthrough with panel highlighting and exe-relative completion marker
- [test_flow/AGENTS.md](test_flow/AGENTS.md) — GUI-free test-flow (ATP) harness: flow-file schema, parameter sweeps, metric capture, JSON result export

<!-- OPENWIKI:START -->

## OpenWiki

This repository has a generated `openwiki/` evidence index. It is optional just-in-time context, not required startup reading.

- Treat source code and tests as authoritative. A brief's unknowns and review items are verification gaps, not automatic requirements.
- Prefer the narrowest quiet validation that proves the changed behavior. Preserve complete failure output.

The scheduled OpenWiki GitHub Actions workflow refreshes the repository wiki. Do not hand-edit generated OpenWiki pages unless explicitly asked; prefer updating source code/docs and letting OpenWiki regenerate.

<!-- OPENWIKI:END -->
