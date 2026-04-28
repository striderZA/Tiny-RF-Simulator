# RF Simulator

Modular RF signal chain simulator with real-time spectrum display using Dear ImGui + ImPlot.

## Quick Start

**Dependencies:** OpenGL, CMake ≥ 3.20, Ninja build system.

```bash
cmake -B build -G Ninja
cmake --build build
build/bin/main.exe
```

Dependencies (ImGui docking, ImPlot, GLFW, Catch2) are auto-fetched via CMake FetchContent.

## Usage

- **Signal Chain** panel: add/remove generators and amplifiers
- **Generator** panel: set tone frequency (MHz), amplitude (dBm), and bin width — a clean source with flat thermal noise floor at -174 dBm/Hz
- **Amplifier** panel: set gain (dB) and noise figure (dB) — adds amplified noise proportional to NF and RBW
- **Spectrum Analyzer** panel: adjust start/stop frequency, RBW, VBW, and reference level. Check "Measure" on any component to view its spectrum. Combine multiple components by enabling Measure on each

Noise is modeled as power spectral density (W/Hz). Displayed noise floor depends only on component gain/NF and the analyzer's RBW — not on the internal simulation grid.

## Tests

```bash
cmake --build build
ctest --test-dir build
```

## Architecture

C++20 modular library. Each module has an `*_engine` (pure DSP, no UI) and `*_widget` (ImGui UI holding an `Engine&`). Signal chains are wired in `app/`. `ViewManager` tracks which `SignalNode`s the spectrum analyzer observes.

## License

MIT
