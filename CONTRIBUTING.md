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

**One-time setup — enable the format pre-commit hook** (blocks commits that would fail CI's `format` job):

```bash
git config core.hooksPath .githooks
```

If `clang-format-18` isn't available via your system package manager (e.g. Windows, older macOS), install a pinned copy: `scripts/install-clang-format.sh` (requires Python/pip; installs to `~/.cache/clang-format-18`, auto-detected by the hook and `scripts/format.sh`).

> **See also:** [Build & Operations guide](openwiki/operations/build-runbook.md) for release builds, clean builds, CI/CD, and troubleshooting.

## Testing

```bash
ctest --test-dir build --output-on-failure
```

22+ test source files (including benchmark suites) cover all DSP engines, the node graph, touchstone parser, PFB channelizer, amplifier nonlinear model, and UI. CI runs these with Xvfb for UI tests, AddressSanitizer for memory safety, and tracks coverage via gcov/lcov (uploaded to Codecov).

For per-engine dirty/clean benchmarks:

```bash
build/bin/tests [bench]       # Linux/macOS
build/bin/tests.exe [bench]   # Windows
```

> **See also:** [Testing Guide](openwiki/testing/guidance.md) for test structure, patterns, benchmarks, and UI tests.

## CI Pipeline

Every pull request runs through a lightweight sanity pipeline:

- **Format check** — clang-format 18 enforces code style
- **Linux Debug build** — verifies the project still configures and compiles

Minor and major release tags (`vX.Y.0`, including `vX.0.0`) trigger the stricter release validation flow:

- **Packaging builds** — Linux and Windows artifacts
- **Full validation matrix** — Linux GCC Debug/Release, Linux Clang, Windows MinGW
- **Tests** — unit tests + UI tests where supported
- **AddressSanitizer** — memory safety checks on Linux

Patch release tags (`vX.Y.Z` where `Z > 0`) still build/package Linux and Windows artifacts, but skip the strict validation matrix.

### Release Process

Every release tag, including patch tags, requires a matching changelog entry before it is pushed:

1. Update `CMakeLists.txt` and add the matching `## [X.Y.Z]` section at the top of `CHANGELOG.md`.
2. Push `master`, then create and push the annotated `vX.Y.Z` tag.
3. The release workflow validates the tag against `CMakeLists.txt`, runs the applicable build/test matrix, and packages Linux and Windows binaries.
4. The workflow extracts the matching `CHANGELOG.md` section as the draft release description. `CHANGELOG.md` is the release-note source of truth; `cliff.toml` is for standalone git-cliff generation only.
5. Review and publish the generated GitHub draft after the workflow succeeds.

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
- Branch from `master`, delete the feature branch after merging.

### Commits

- **Atomic commits:** one logical change per commit (feature, fix, refactor, or docs).
- **Commit early, commit often** — a commit should represent a working state.
- **Verify the build and tests pass** before committing.
- **Imperative mood** subject line, <70 chars, no body.

### Pull Requests

- Use the [PR template](.github/PULL_REQUEST_TEMPLATE.md).
- Ensure CI passes before requesting review.
- Squash-merge or rebase-merge to keep history clean.

## Where to Get Help

- **Bug reports:** [open a bug issue](../../issues/new?template=bug_report.md)
- **Feature requests:** [open a feature issue](../../issues/new?template=feature_request.md)
- **Build questions:** check [README.md](README.md) first, then open an issue.

## License

By contributing, you agree that your contributions will be licensed under the [Apache-2.0 License](LICENSE).
