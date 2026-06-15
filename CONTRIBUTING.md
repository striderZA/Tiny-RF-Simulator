# Contributing to RF Simulator

Thanks for your interest in contributing! This guide covers everything you need to develop, test, and submit changes.

## Development Setup

### Prerequisites

| Dependency | Minimum | Notes |
|------------|---------|-------|
| **Compiler** | C++20 | GCC 11+ (MinGW-w64 on Windows), Clang 14+ |
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

## Testing

```bash
ctest --test-dir build --output-on-failure
```

73 tests (67 unit + 6 benchmarks) cover all DSP engines, the node graph, touchstone parser, PFB channelizer, amplifier nonlinear model, and UI.

For per-engine dirty/clean benchmarks:

```bash
build/bin/tests [bench]       # Linux/macOS
build/bin/tests.exe [bench]   # Windows
```

## Architecture (Quick Reference)

- **C++20** modular library.
- **Engine + Widget pattern:** each module has an `*_engine` (pure DSP, no UI deps) and an optional `*_widget` (ImGui UI). The engine owns a `SignalNode {input, output Spectrum, view_enabled}`.
- **Only widget files** may `#include <imgui.h>` / `<implot.h>`.
- **CMake targets** use `simulator::*` aliases (e.g. `simulator::signal_generator_engine`).
- **Signal wiring** is explicit: the app queries `NodeGraphEngine` to route `Spectrum` data between engines each frame.
- For full architecture details, see [ARCHITECTURE.md](ARCHITECTURE.md).

## Code Style

- **Format:** LLVM-based via [`.clang-format`](.clang-format) (4-space indent, 100 cols, `PointerAlignment: Right`).
  Run `clang-format -i <file>` on every changed file before committing.
- **DSP engine helpers** for ImGui inputs: `utils::inputDouble(label, ref, min, max)` and `utils::inputFrequency(label, freq_Hz, ...)`.
- **Spectrum tone struct:** `{double freq_Hz, power_dBm, phase_deg}`.
- **Test float comparisons** with `Catch::Approx` from `<catch2/catch_approx.hpp>`.
- **Portability:** `uint64_t` requires explicit `#include <cstdint>` (g++ does not provide it transitively).

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

By contributing, you agree that your contributions will be licensed under the [MIT License](LICENSE).
