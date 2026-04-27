# RF Simulator - Agent Guide

## Build & Test

- **CMake + Ninja** primary. First time: `cmake -B build -G Ninja`, then `cmake --build build`
- Output: `build/bin/main.exe`
- Tests: `cmake --build build && ctest --test-dir build` or run `build/bin/tests.exe` directly
- Dependencies (ImGui docking, ImPlot, GLFW, Catch2 v3.4.0) auto-fetched via FetchContent
- `compile_commands.json` generated in `build/`; `.clangd` points there

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
- `typedef std::pair<double, double> tone` (freq_Hz, power_dBm)
- `view_enabled` flag on `SignalNode` controls spectrum analyzer visibility
- Commits: imperative mood, short (<70 char) subject, no body (conventional commit style)
