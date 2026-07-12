# Build & Operations

Build system, CI/CD, debugging tips, and operational notes for the RF Simulator project.

---

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
build/bin/main.exe

# Linux/macOS
build/bin/main
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

### Build & Test (`build.yml`)

Runs on push to `v*` tags and PRs to `master`. Two-matrix strategy:

| OS | Compiler | Shell |
|---|---|---|
| ubuntu-24.04 | GCC 14 | bash |
| windows-latest | MinGW-w64 (MSYS2) | msys2 |

Steps:
1. Checkout + install dependencies (apt on Linux, MSYS2 on Windows).
2. Configure with CMake + Ninja.
3. Build.
4. Test with `ctest`, excluding benchmarks and UI tests.

### OpenWiki Update (`openwiki-update.yml`)

Scheduled daily at 08:00 UTC, also supports `workflow_dispatch`. Uses OpenWiki CLI to regenerate documentation and creates a PR.

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

The repository uses **git worktrees** for parallel feature development. Each worktree maps to a feature branch:

```bash
.git/worktrees/
├── adc/          # fix/adc-dead-params
├── amplifier/    # feat/amplifier-sparam
├── coax/         # fix/coax-phase-and-presets
├── equalizer/    # feat/equalizer
├── iq-plot/      # fix/iq-plot-bugs
├── pfb/          # feat/pfb-channelizer
├── shared-helpers/
├── signal-generator/
├── spectrum-analyzer/
├── splitter/
└── touchstone/   # fix/touchstone-validation
```

Each worktree has its own `build-<name>/` directory for parallel compilation without cache conflicts.

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
- Run `clang-format -i <file>` on every changed file before committing.
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
├── bin/            # Executables (main, tests, test_ui)
├── lib/            # Static libraries
└── _deps/          # FetchContent downloads
```

---

## File Structure Guidelines

### Adding a New Component

1. Create directory `new_component/` with:
   - `CMakeLists.txt` — define `simulator::new_component_engine` target.
   - `include/new_component_engine.h` — engine class inheriting `IComponentEngine`.
   - `src/new_component_engine.cpp` — DSP implementation.
   - `include/new_component_widget.h` (optional) — ImGui widget.
   - `src/new_component_widget.cpp` (optional) — UI rendering.
2. Add `add_subdirectory("new_component")` to root `CMakeLists.txt`.
3. Add `NodeKind` enum value in `node_graph/include/node_graph_engine.h`.
4. Add schematic symbol in `node_graph/src/node_graph_widget.cpp`.
5. Add context menu item in `node_graph/src/node_graph_widget.cpp`.
6. Add property editor in `app/src/inspector_panel.cpp`.
7. Wire constructor + callbacks in `app/src/app.cpp`.
8. Add tests in `tests/test_new_component.cpp`.

See existing components (amplifier, coax, equalizer) as reference.

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
