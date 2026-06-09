# RF Simulator - Agent Guide

## Build & Test

- **CMake + Ninja + MinGW-w64 g++** primary.
  - First configure: `cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc`
  - Then build: `cmake --build build`
  - Or reconfigure from scratch: `rm -rf build && cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc`
- Output: `build/bin/main` (Linux) or `build/bin/main.exe` (Windows)
- Tests: `cmake --build build && ctest --test-dir build` or run `build/bin/tests` / `build/bin/tests.exe` directly
- CI: `.github/workflows/build.yml` runs on `ubuntu-latest` with GCC 14
- Dependencies (ImGui docking, ImPlot, GLFW, Catch2 v3.4.0) auto-fetched via FetchContent
- `compile_commands.json` generated in `build/`; `.clangd` points there
- **Note**: MSVC (cl.exe) is not supported. The project uses MinGW-w64 g++ on Windows. If switching compilers, always delete `build/` first to clear cached compiler detection.
- **Portability note**: `uint64_t` requires explicit `#include <cstdint>`. MSVC headers may provide it transitively; g++ does not — always include `<cstdint>` when using fixed-width integer types.

## Architecture

- **C++20** modular library design. Entry: `src/main.cpp` → `RfSimulatorCore` (GLFW/ImGui loop) + `RfSimulatorApp` (orchestrator)
- Each module: `*_engine` (pure DSP, no UI deps) + `*_widget` (ImGui UI, holds `Engine&`). Engine owns a `SignalNode{input, output Spectrum, view_enabled}`.
- Widgets expose `draw(title, p_open)`. Only widget files `#include <imgui.h>` / `<implot.h>`.
- CMake targets use `simulator::*` aliases (e.g. `simulator::signal_generator_engine`)
- `common/` is header-only INTERFACE library; `logging_core` is singleton via `LoggerCore::instance()`; `LOG_INFO`/`LOG_WARN`/`LOG_ERROR` macros
- `ViewManager` tracks which `SignalNode*`s the spectrum analyzer observes (set `view_enabled=true` to include)
- Signal chain: `gen.output → amp.input` wired in `app/src/app.cpp`

## Code Style & Conventions

- `.clang-format`: LLVM-based, 4-space indent, 100 cols, `PointerAlignment: Right`, `AccessModifierOffset: -2`
- Format: `clang-format -i <file>`
- `utils::inputDouble(label, ref, min, max)` and `utils::inputFrequency(label, freq_Hz, ...)` for ImGui inputs (MHz conversion, clamping)
- `Spectrum::Tone` struct: `{double freq_Hz, power_dBm, phase_deg}`
- `view_enabled` flag on `SignalNode` controls spectrum analyzer visibility
- Commits: imperative mood, short (<70 char) subject, no body (conventional commit style)

## Git Guidelines

### Atomic Commits
- Each commit is one logical change: a feature, a fix, a refactor, or a doc update
- Never mix unrelated changes in a single commit
- Commit early, commit often — a commit should represent a working state
- Before committing: verify the project builds and tests pass

### Branching
- **Feature branches** for all new work. Never commit directly to `master`
- Branch naming: `feat/<short-description>` for features, `fix/<short-description>` for bug fixes, `docs/<short-description>` for documentation
- Examples: `feat/pfb-spectrum-display`, `fix/amplifier-noise-reconstruction`, `docs/api-reference`
- Branch from `master`, merge back via fast-forward when possible
- Delete the feature branch after merging

### Pull Requests
- Use the PR template at `.github/PULL_REQUEST_TEMPLATE.md`
- Ensure CI passes before requesting review
- Squash-merge or rebase-merge to keep history clean

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

When the user requests a durable behavior change, record it here or in the relevant child AGENTS.md

## Child DOX Index

| Path | Scope | Purpose |
|------|-------|---------|
| `tests/` | Test suite | Catch2 v3 unit tests + benchmarks for all DSP engines, node graph, touchstone parser, and app components. Owns test conventions, linking, and discovery. |
| `docs/` | Documentation | Reference resources, superpowers plans, and design specs. Owns documentation naming, structure, and lifecycle. |

### Owned by Root (no child doc needed)

The following directories follow the standard engine+widget pattern described in Architecture and Code Style sections above. They do not have their own AGENTS.md because their conventions are fully covered at this level:

- `core/` — Platform core (GLFW, ImGui lifecycle)
- `common/` — Header-only library (Spectrum, SignalNode, ViewManager)
- `app/` — Application orchestrator and component registry
- `src/` — Main entry point
- `signal_generator/` — Signal generator engine + widget
- `amplifier/` — Amplifier engine (basic gain/NF) + data files
- `spectrum_analyzer/` — Spectrum analyzer engine + ImPlot widget
- `node_graph/` — Node graph engine + imnodes widget
- `splitter/` — Splitter engine (1-in/2-out)
- `mixer/` — Mixer engine (frequency conversion)
- `s_parameter_amplifier/` — S-parameter amplifier engine
- `s_parameter_filter/` — S-parameter filter engine
- `touchstone/` — Touchstone .s2p/.s3p/.s4p parser
- `adc/` — RF ADC engine
- `pfb_channelizer/` — PFB channelizer engine + widget
- `iq_plot/` — IQ time-domain plot widget
- `icon_registry/` — Node icon texture manager
- `ideal_filter/` — Ideal filter engine
- `logging/` — Logger singleton + logging widget
- `test_engine/` — ImGui test engine UI tests
- `assets/` — Static assets (banner.png)
