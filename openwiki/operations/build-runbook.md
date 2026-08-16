---
type: Runbook
title: Build & Operations
description: Build system setup, CI/CD pipelines, debugging tips, and operational notes for the RF Simulator project.
tags: [build, ci, operations, runbook]
---

# Build & Operations

Build system, CI/CD, debugging tips, and operational notes for the RF Simulator project. **Current version: v0.19.1**.

---

## Prerequisites

## Prerequisites

| Dependency | Minimum | Notes |
|---|---|---|
| C++ Compiler | C++20 | GCC 11+, Clang 14+, or MinGW-w64 g++ |
| CMake | >= 3.20 | [cmake.org/download](https://cmake.org/download) |
| Ninja | >= 1.10 | [ninja-build.org](https://ninja-build.org) |
| OpenGL | 2.1+ | System-provided |
| Git | Any | Required for FetchContent |

> **Windows:** MSVC is NOT supported. Use MinGW-w64 (winlibs.com or MSYS2). If switching compilers, delete `build/` first.

---

## Build Commands

### Standard Build

```bash
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc
cmake --build build
```

### Release Build

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc
cmake --build build
```

### Incremental Build (after code changes)

```bash
cmake --build build
```

Only changed files recompile. If you add/remove source files, reconfigure:

```bash
cmake -B build
cmake --build build
```

### Clean Build

```bash
rm -rf build
cmake -B build -G Ninja ...
```

---

## Running

```powershell
# Windows
build/bin/tiny-rf-simulator.exe

# Linux/macOS
build/bin/tiny-rf-simulator
```

---

## Tests

```bash
# All tests (excluding benchmarks)
ctest --test-dir build --output-on-failure

# Benchmarks
build/bin/tests [bench]

# Subset by tag
build/bin/tests [sparam]
build/bin/tests [filter]
build/bin/tests [edge]

# UI tests (requires display)
build/bin/test_ui
```

---

## CI/CD

### Build & Sanity (`ci.yml`)

Runs on pull requests to `master` (docs-only changes are skipped via `paths-ignore`):

1. `format` — clang-format 18 dry-run over the module list hardcoded in `ci.yml` (`src app core common tests test_engine` + the DSP/UI modules). **Note:** `network_analyzer/`, `help/`, `layout/`, and `tutorial/` are not in that list — new files there are not format-checked by CI, so run `clang-format -i` manually before committing.
2. `build` — Linux GCC 14 Debug configure + build.

### Release (`release.yml`)

Runs on `v*` tags. `classify-release` splits tags into patch vs minor/major:

- **Minor/major tags** (`vX.Y.0`, `vX.0.0`): strict 4-way build matrix (Linux GCC Debug/Release, Linux Clang, Windows MinGW) with `ctest`, plus an AddressSanitizer job.
- **Patch tags** (`vX.Y.Z`, `Z > 0`): Linux + Windows package builds only; the strict validation matrix is skipped.
- `validate-version` enforces the tag matches `CMakeLists.txt`'s `project(... VERSION ...)`.
- The Windows strict-build job also verifies the MinGW TEST_CASE registration floor (`tests.exe --list-tests` ≥ 223) so silently dropped TEST_CASEs fail CI instead of shipping unrun tests.
- Release artifacts are published as a draft GitHub release.

> **CHANGELOG.md lags the codebase:** the latest entry is 0.11.0 while `CMakeLists.txt` declares 0.19.1. Treat the CMake version and the [quickstart milestones](../quickstart.md) (grounded in source) as authoritative; `CHANGELOG.md` has not been updated for v0.12.0–v0.19.1.

### OpenWiki Update (`openwiki-update.yml`)

Scheduled weekly (Sundays 08:00 UTC), also supports `workflow_dispatch`. Uses OpenWiki CLI to regenerate documentation and creates a PR.

---

## Bug Pattern: NaN from `log10(0)`

The single most common bug class in the codebase is NaN propagation from `log10(0)`. Every DSP engine that computes frequency-dependent gain must guard against this.

**Detection:** If a test fails with `NaN` in output power or noise, check every `log10()` call in the engine's `update()`. Zero-frequency tones, out-of-band frequencies, or clamping at 0 can all trigger this.

**Common sources:**
- `equalizer_engine.cpp` — `log10(f / refFreq)` when refFreq = 0
- `coax_cable_engine.cpp` — `sqrt(f)` for loss computation when f = 0
- `touchstone_parser.cpp` — interpolation when all frequencies are zero
- Any engine computing gain in dB where the linear gain is zero

**Fix pattern:** Always `std::max(input, minValid)` before calling `log10()`, where `minValid` is a small positive value (e.g., `1.0` for frequencies). Add a test with zero-frequency input.

## Development Workflow

### Git Conventions

- **Feature branches** from `master`: `feat/`, `fix/`, `docs/` prefixes.
- **Atomic commits:** one logical change per commit, imperative mood, <70 char subject. Example: `feat(amplifier): add S-parameter mode`.
- **Squash-merge or rebase-merge** to keep history clean.

### Git Worktree Workflow

The repository currently lives on a single `master` branch whose history was squashed into one commit; there is no active `git worktree` setup (`git worktree list` shows only the main checkout). The worktree pattern below is **optional** guidance for developers who want parallel feature branches with separate build directories — it is not an enforced or currently active repository layout:

```bash
# Example pattern (not present in the repo today):
git worktree add ../rf-sim-adc fix/adc-dead-params
cd ../rf-sim-adc
mkdir build-adc && cmake -S . -B build-adc -G Ninja
```

Each worktree gets its own `build-<name>/` directory for parallel compilation without cache conflicts. The earlier per-module worktree listing (`fix/adc-dead-params`, `feat/equalizer`, etc.) reflected a historical snapshot of long-merged feature branches and is no longer accurate.

### Design-First Methodology

Major features follow a design → implementation → test → docs lifecycle:

1. **Design doc** written before starting work
2. **Feature branch** created from `master`
3. **Implementation** with atomic commits
4. **Tests** added alongside implementation
5. **Bugfix branch** for issues found in testing/review
6. **Merge** back to `master` with squash-merge or rebase-merge

### Code Style

- **Clang-format:** LLVM-based (`PointerAlignment: Right`, 4-space indent, 100 cols).
- Run `clang-format -i <file>` on every changed file before committing. Enforced by the `.githooks/pre-commit` hook (clang-format-18 dry-run, install via `scripts/install-clang-format.sh`) and the CI `format` job.
- Only widget `.cpp`/`.h` files may include `<imgui.h>` or `<implot.h>`.
- Float comparisons in tests: `Catch::Approx` from `<catch2/catch_approx.hpp>`.

---

## Debugging

### LSP / clangd

`build/compile_commands.json` is generated automatically. `.clangd` points to it.

### Logging

The application has a built-in log viewer (`LoggerCore` singleton + `LoggingWidget`):

```cpp
LOG_INFO("Component updated: gain = %f dB", gain_dB);
LOG_WARN("Out-of-band frequency: %f Hz", freq);
LOG_ERROR("Failed to load file: %s", path.c_str());
```

Log output appears in the "Log" ImGui window and can be filtered by severity.

### Common Issues

| Issue | Likely Cause | Fix |
|---|---|---|
| Build fails on Windows | Using MSVC | Use MinGW-w64 g++, delete `build/` |
| FetchContent slow | First build | Wait 60-90s; subsequent builds are fast |
| Link errors after adding files | CMake list not updated | Add file to module's `CMakeLists.txt`, reconfigure |
| Test fails with NaN | `log10(0)` in some DSP path | Check for zero-frequency tones passing through equalizer or mixer |
| UI tests hang/fail | No display server | Use `xvfb-run build/bin/test_ui` on headless Linux |

---

## Project Configuration

### CMake Options

| Option | Default | Description |
|---|---|---|
| `CMAKE_BUILD_TYPE` | Debug | Release for optimized builds |
| `GLFW_BUILD_WAYLAND` | (varies) | Set to OFF on Linux for X11 compatibility |

All library dependencies are fetched automatically via `FetchContent`:
- Dear ImGui (docking branch)
- ImPlot, GLFW, imnodes, Catch2, imgui_test_engine
- portable-file-dialogs, kissfft, stb

### Directory Layout

```
build/
├── bin/            # Executables (tiny-rf-simulator, tests, test_ui)
├── lib/            # Static libraries
└── _deps/          # FetchContent downloads
```

---

## File Structure Guidelines

### Adding a New Component

Since the v0.16.0 [ComponentTypeRegistry unification](../architecture/overview.md#componentlibrary) the menu, add, duplicate, save/load, inspector, and authoring-form paths all dispatch through one registry row — the old flow of hand-editing `RfSimulatorApp`/`node_graph_widget.cpp` context menus is gone:

1. Create directory `new_component/` with:
   - `CMakeLists.txt` — define `simulator::new_component_engine` target.
   - `include/new_component_engine.h` — engine class inheriting `IComponentEngine`.
   - `src/new_component_engine.cpp` — DSP implementation.
   - `include/new_component_widget.h` (optional) — ImGui widget.
   - `src/new_component_widget.cpp` (optional) — UI rendering.
2. Add `add_subdirectory("new_component")` to root `CMakeLists.txt`.
3. Add a `ComponentTypeDescriptor` row in `app/src/component_type_registry.cpp` (`type`, `project_type`, `menu_label`, `label_prefix`, `kind`, `create`, `draw_inspector`).
4. Add the `NodeKind` enum value + schematic symbol + label→kind mapping in `node_graph/` (`node_graph_engine.h`, `schematic_symbols.cpp`, `node_graph_widget.cpp`).
5. Add a property drawer entry in `app/src/inspector_panel.cpp`'s `drawerMap()` (missing drawers are logged at startup and caught by the registry/drawer consistency test in `test_component_dispatch.cpp`).
6. Add tests in `tests/test_new_component.cpp` (as a standalone executable if the main `tests` binary is near the MinGW registration ceiling).

See existing components (amplifier, attenuator, filter) as reference, and the [RF Components](../domains/rf-components.md) page for per-component patterns. Instruments that are **not** graph components (Spectrum Analyzer, Network Analyzer) skip steps 3–5 and instead follow the singleton-panel pattern owned by `RfSimulatorApp`.

---

## Operations Checklist

| Task | Command |
|---|---|
| Build | `cmake --build build` |
| Run tests | `ctest --test-dir build --output-on-failure` |
| Run benchmarks | `build/bin/tests [bench]` |
| Run UI tests | `build/bin/test_ui` |
| Format code | `clang-format -i path/to/file.cpp` |
| Full clean rebuild | `rm -rf build && cmake -B build -G Ninja ... && cmake --build build` |
