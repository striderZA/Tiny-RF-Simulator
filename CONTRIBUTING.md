# Contributing to RF Simulator

Thanks for your interest in contributing! This guide covers everything you need to develop, test, and submit changes.

## Development Setup

### Prerequisites

| Dependency | Minimum | Notes |
|------------|---------|-------|
| **Compiler** | C++20 | GCC 14, Clang 18 (CI); MinGW-w64 (Windows) |
| **CMake** | ≥ 3.20 | [cmake.org/download](https://cmake.org/download) |
| **Ninja** | ≥ 1.10 | [github.com/ninja-build/ninja](https://github.com/ninja-build/ninja) |
| **OpenGL** | 2.1+ | System-provided on all platforms |
| **Git** | — | Required for CMake FetchContent |

> **Windows:** MSVC (cl.exe) is not supported. Use MinGW-w64 g++.
> If switching compilers on Windows, delete `build/` first to clear cached compiler detection.

### Clone & Build

```bash
git clone https://github.com/striderZA/Tiny-RF-Simulator.git
cd Tiny-RF-Simulator
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc
cmake --build build
```

The first build takes 60-90s while FetchContent clones dependencies. Subsequent builds are fast.

**Where dependency sources live:** all FetchContent dependencies (imgui, implot, catch2, imgui_test_engine, …) are fetched into `build/_deps/<name>-src/` inside the active build directory. A fresh git worktree has **no** `build/` yet — run `cmake -B build -G Ninja` once (~90 s) to fetch them there, or browse the canonical checkout's copy. Never search the filesystem for headers like `imgui_internal.h` — a `find /`-style scan hangs far past any agent timeout on Windows. Read `CMakeLists.txt`'s `FetchContent_Declare` block for pinned versions.

**One-time setup — enable the format pre-commit hook** (blocks commits that would fail the release workflow's `format` job):

```bash
git config core.hooksPath .githooks
```

If `clang-format-18` isn't available via your system package manager (e.g. Windows, older macOS), install a pinned copy: `scripts/install-clang-format.sh` (requires Python/pip; installs to `~/.cache/clang-format-18`, auto-detected by the hook and `scripts/format.sh`).

> **See also:** [Build & Operations guide](openwiki/operations/build-runbook.md) for release builds, clean builds, CI/CD, and troubleshooting.

## Testing

```bash
ctest --test-dir build --output-on-failure
```

Test sources live in `tests/` (Catch2 unit + benchmark suites) and `test_engine/` (ImGui UI tests). The main `tests` binary compiles `TEST_SOURCES` from `tests/CMakeLists.txt` (plus `test_session_state.cpp` on Windows); coverage that must run on every platform — or would push past the MinGW-w64 registration ceiling — goes in standalone executables registered there via `add_standalone_test` (`tests/CMakeLists.txt` is the authoritative inventory). The release workflow runs these under Xvfb for UI tests and with AddressSanitizer for memory safety.

For per-engine dirty/clean benchmarks:

```bash
build/bin/tests [bench]       # Linux/macOS
build/bin/tests.exe [bench]   # Windows
```

> **See also:** [Testing Guide](openwiki/testing/guidance.md) for test structure, patterns, benchmarks, and UI tests.

## CI Pipeline

Pull requests run **no automated pipeline**. All CI validation happens when a release tag is pushed (`.github/workflows/release.yml`). Every release tag, patch included, runs:

- **Format check** — clang-format 18 enforces code style across all source modules
- **AddressSanitizer** — Linux Debug build and tests with ASan (UI tests excluded)
- **Packaging builds** — optimized Release Linux and Windows binaries (the artifacts attached to the GitHub release); each package leg runs the test suite against the build it ships (UI tests excluded on Windows)

On top of that:

- **Patch release tags** (`vX.Y.Z` where `Z > 0`) run a **Linux GCC 14 Debug build + full test suite** (Xvfb for UI tests).
- **Minor and major release tags** (`vX.Y.0`, including `vX.0.0`) run the stricter **validation matrix** instead: Linux GCC Debug/Release, Linux Clang 18, and Windows MinGW-w64 builds with unit + UI tests where supported.

Because nothing is checked automatically before merge, run the local gates on every branch before opening a PR:

```bash
scripts/format.sh --check   # CI-equivalent formatting (also enforced by the pre-commit hook)
cmake --build build
ctest --test-dir build --output-on-failure
```

### Release Process

Every release is prepared through a pull request; the tag is created only on the merged `master` commit:

1. Branch `release/vX.Y.Z` from `master`. Update `CMakeLists.txt` and add the matching `## [X.Y.Z]` section at the top of `CHANGELOG.md`.
2. Open a PR against `master`, pass the local gates above, and merge (squash or rebase) after review.
3. On the merged `master` commit, create and push the annotated tag: `git tag -a vX.Y.Z -m "Release X.Y.Z" && git push origin vX.Y.Z`.
4. The release workflow validates the tag against `CMakeLists.txt` and the changelog section, runs the applicable build/test matrix, and packages Linux and Windows binaries.
5. The workflow extracts the matching `CHANGELOG.md` section as the draft release description. `CHANGELOG.md` is the release-note source of truth; `cliff.toml` is for standalone git-cliff generation only.
6. Review and publish the generated GitHub draft after the workflow succeeds.

## Architecture (Quick Reference)

- **C++20** modular library.
- **Engine + Widget pattern:** each module has an `*_engine` (pure DSP, no UI deps) and an optional `*_widget` (ImGui UI). The engine owns a `SignalNode {input, output Spectrum, view_enabled}`.
- **Only widget files** may `#include <imgui.h>` / `<implot.h>`.
- **CMake targets** use `simulator::*` aliases (e.g. `simulator::signal_generator_engine`).
- **Signal wiring** is explicit: the app queries `NodeGraphEngine` to route `Spectrum` data between engines each frame.
- For full architecture details, see the [Architecture Overview](openwiki/architecture/overview.md).

## Code Style

- **Format:** LLVM-based via [`.clang-format`](.clang-format) (4-space indent, 100 cols, `PointerAlignment: Right`).
  Run `scripts/format.sh` to reformat changed files, or `scripts/format.sh --check` to dry-run (CI-equivalent). CI will reject PRs with formatting violations.
  Enable `git config core.hooksPath .githooks` once per clone to catch this automatically on `git commit` — see Clone & Build above.
- **DSP engine helpers** for ImGui inputs: `utils::inputDouble(label, ref, min, max)` and `utils::inputFrequency(label, freq_Hz, ...)`.
- **Spectrum tone struct:** `{double freq_Hz, power_dBm, phase_deg}`.
- **Test float comparisons** with `Catch::Approx` from `<catch2/catch_approx.hpp>`.
- **Portability:** `uint64_t` requires explicit `#include <cstdint>` (g++ does not provide it transitively).

> **See also:** [Build & Operations guide](openwiki/operations/build-runbook.md#code-style) for full code style conventions.

## Git Workflow

### Branching

- **Feature branches** for all new work. Never commit directly to `master`.
- Branch naming:
  - `feat/<short-description>` for new features
  - `fix/<short-description>` for bug fixes
  - `docs/<short-description>` for documentation
  - `release/vX.Y.Z` for release preparation (see Release Process)
- Branch from `master`, delete the feature branch after merging.

### Commits

- **Atomic commits:** one logical change per commit (feature, fix, refactor, or docs).
- **Commit early, commit often** — a commit should represent a working state.
- **Verify the build and tests pass** before committing.
- **Imperative mood** subject line, <70 chars, no body.

### Pull Requests

- Use the [PR template](.github/PULL_REQUEST_TEMPLATE.md).
- No CI runs on pull requests — verify build, tests, and formatting locally before requesting review (see CI Pipeline).
- Squash-merge or rebase-merge to keep history clean.

## Where to Get Help

- **Bug reports:** [open a bug issue](../../issues/new?template=bug_report.md)
- **Feature requests:** [open a feature issue](../../issues/new?template=feature_request.md)
- **Build questions:** check [README.md](README.md) first, then open an issue.

## License

By contributing, you agree that your contributions will be licensed under the [Apache-2.0 License](LICENSE).
