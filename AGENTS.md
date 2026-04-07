# RF Simulator - Agent Guide

## Build System

- **CMake + Ninja** is the primary build system
- Build commands (run from repo root):
  - `cmake -B build -G Ninja` (first time only)
  - `cmake --build build` (incremental builds)
- Output binary: `build/bin/main.exe`
- Dependencies (ImGui, ImPlot, GLFW) are fetched automatically via CMake FetchContent

## Architecture

- **C++20** project with modular library design
- Each module follows the pattern: `*_engine` (DSP/logic) + `*_widget` (ImGui UI)
- Modules: `signal_generator`, `amplifier`, `spectrum_analyzer`, `logging`
- `core/` - ImGui/ImPlot/GLFW backend setup
- `common/` - Shared headers (signal_node.h, spectrum.h, utils.h)
- `app/` - Application orchestrator that wires modules together
- Entry point: `src/main.cpp`

## Key Directories

```
core/           - UI subsystem (ImGui + ImPlot + GLFW backend)
app/            - Application orchestration
signal_generator/ - Signal generation engine + widget
amplifier/      - Amplifier engine + widget
spectrum_analyzer/ - Spectrum analyzer engine + widget
logging/        - Logging core + widget
common/         - Shared interfaces
```

## Code Style

- `.clang-format` enforces LLVM-based style with 4-space indent, 100 column limit
- Use `clang-format` for formatting: `clang-format -i <file>`
- clangd is configured to use `build/compile_commands.json`

## Build Configuration

- C++20 required (`CMAKE_CXX_STANDARD 20`)
- Visual Studio solution files exist in `.vs/` but CMake is primary
- No CI workflows, no pre-commit hooks
- No test suite (tests/ directory is empty)
