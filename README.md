<p align="center">
  <img src="assets/banner.png" alt="RF Simulator Banner" width="70%">
</p>

# RF Simulator

Modular RF signal chain simulator with real-time spectrum display using Dear ImGui + ImPlot.
Design a cascade of RF components and probe any node to see the spectrum.

## Requirements

| Dependency | Minimum | Notes |
|------------|---------|-------|
| **Compiler** | C++20 | GCC 11+ (MinGW-w64 on Windows), Clang 14+ |
| **CMake** | ≥ 3.20 | [cmake.org/download](https://cmake.org/download) |
| **Ninja** | ≥ 1.10 | [github.com/ninja-build/ninja](https://github.com/ninja-build/ninja) |
| **OpenGL** | 2.1+ | System-provided on all platforms |
| **Git** | — | Required for FetchContent to clone dependencies |

All library dependencies are fetched automatically at configure time via CMake FetchContent:

| Library | Purpose |
|---------|---------|
| [Dear ImGui](https://github.com/ocornut/imgui) (docking) | UI framework |
| [ImPlot](https://github.com/epezent/implot) | Spectrum plotting |
| [GLFW](https://github.com/glfw/glfw) | Window + OpenGL context |
| [imnodes](https://github.com/Nelarius/imnodes) | Node editor canvas |
| [Catch2](https://github.com/catchorg/Catch2) v3 | Unit tests + benchmarks |
| [imgui_test_engine](https://github.com/ocornut/imgui_test_engine) | UI test engine |
| [portable-file-dialogs](https://github.com/samhocevar/portable-file-dialogs) | Native file browser |
| [kissfft](https://github.com/mborgerding/kissfft) | FFT for IQ time-domain plot |
| [stb](https://github.com/nothings/stb) | PNG loading for node icons |

## Quick Start

### Windows (MinGW-w64)

```powershell
# Requires: CMake ≥ 3.20 (choco install cmake or from cmake.org)
# Requires: Ninja (choco install ninja or from ninja-build.org)
# Requires: MinGW-w64 (winlibs.com or via msys2.org)

cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc
cmake --build build
build/bin/main.exe
```

### Linux

```bash
# Install Ninja + build tools
sudo apt install ninja-build cmake g++ pkg-config

# OpenGL headers (may already be present)
sudo apt install libgl1-mesa-dev

cmake -B build -G Ninja
cmake --build build
./build/bin/main
```

### macOS

```bash
# Install Ninja + CMake
brew install ninja cmake

# OpenGL + GLFW headers (system-provided on macOS)
cmake -B build -G Ninja
cmake --build build
open build/bin/main
```

### Notes

- **First build** is slow (60-90s) while FetchContent clones and builds all dependencies. Subsequent builds are fast — only your code recompiles.
- **Working tree:** keep your checkout clean, all generated files go under `build/`.
- **Compiler flags:** add `-DCMAKE_BUILD_TYPE=Release` for optimized builds.
- **clangd / LSP:** `build/compile_commands.json` is generated automatically; `.clangd` points to it.

## Usage

### Node Editor (main workspace)

Build your RF chain visually: add components via right-click context menu on the canvas. Connect output pins to input pins to wire the signal path.

| Action | Effect |
|--------|--------|
| **Left-click** node | Select — shows properties in Inspector Panel |
| **Ctrl+left-click** pin (or node) | Add spectrum probe (up to 4) |
| **Shift+left-click** pin (or node) | Remove spectrum probe |
| **Left-click + drag** pin-to-pin | Connect components |
| **Right-click** canvas | Add component |
| **Right-click** node | Remove / context menu |
| **Delete key** | Remove selected node |
| **Hover** node | Tooltip with key parameters |
| **Hover** pin | Tooltip with signal summary |
| **Shift+left-click + drag** empty canvas | Rubber-band select components |
| **Shift+left-click + drag** ≥ 2 components | Open "Create Subcircuit" popup |
| **Click ▶ on group block** | Expand subcircuit (show internals) |
| **Click ▼ on group title bar** | Collapse subcircuit (show as block) |
| **Right-click** group block | Ungroup / Rename / Collapse / Expand |
| **Click** group block | Select subcircuit (group panel in Inspector) |

### Components

- **Generator** — tone generator (frequency, amplitude, phase) with flat thermal noise floor
- **Amplifier** — gain (dB) + noise figure (dB), adds amplified noise. Optional nonlinear mode with OIP2/OIP3 (harmonics, IMD, compression)
- **Splitter** — 1-in/2-out, -3 dB split loss
- **Mixer** — frequency conversion with LO, sum/difference tones, conversion gain
- **RF ADC** — sampler with configurable sample rate, NSD noise floor, bit depth, and full-scale voltage; includes aliasing and Nyquist zone effects
- **PFB Channelizer** — polyphase filter bank channelizer (M channels, K taps/branch, Kaiser window) with per-channel noise and tone routing. Full-spectrum output with active channel highlighting when probed

### Spectrum Analyzer

Displays separate colored traces for each probe point (up to 4 simultaneous). Adjust start/stop frequency, RBW, VBW, and reference level. Marker with peak search and snap-to-peak navigation. Drag-to-zoom on the frequency axis with a reset button.

Noise jitter animates the trace like a real instrument. The RBW convolution is cached internally — when signal data is static, only the jitter+VBW pass runs each frame, preserving the live feel without redoing the expensive Gaussian filter.

Noise is modeled as power spectral density (W/Hz). Displayed noise floor depends only on component gain/NF and the analyzer's RBW.

## Tests

```bash
cmake --build build
ctest --test-dir build
```

[![Build & Test](https://github.com/striderZA/Tiny-RF-Simulator/actions/workflows/build.yml/badge.svg)](https://github.com/striderZA/Tiny-RF-Simulator/actions/workflows/build.yml)

73 tests (67 unit + 6 benchmarks) covering all DSP engines, node graph, touchstone parser, PFB channelizer, amplifier nonlinear model, and UI.

### Benchmarks

Each DSP engine has a dirty/clean benchmark: mean time per `update()` call (dirty = first call after parameter change, clean = cached skip). Run with:

```bash
build/bin/tests [bench]       # Linux
build/bin/tests.exe [bench]   # Windows
```

| Benchmark | Measures |
|-----------|----------|
| `SignalGeneratorEngine` | Tone generation + noise floor |
| `AmplifierEngine` | Gain/NF + nonlinear harmonics/IMD/compression |
| `MixerEngine` | Frequency conversion + sideband generation |
| `SplitterEngine` | 1-to-2 signal split |
| `PFBChannelizerEngine` | Per-channel noise + tone routing |
| `SpectrumAnalyzerEngine` | RBW convolution + jitter + VBW pass |

## Architecture

C++20 modular library. Each module has an `*_engine` (pure DSP, no UI deps) and optional `*_widget` (ImGui UI). Widgets are per-type property drawers shown in the Inspector Panel (node-selection-driven). The node graph (`imnodes`) drives signal chain topology. `ViewManager` tracks which `SignalNode`s the spectrum analyzer observes.

**Signal chain:** Components produce `Spectrum` objects (frequency bins + tone list + noise PSD). Signal wiring uses `const Spectrum*` pointers — zero-copy propagation. The node graph provides topological sort (Kahn's algorithm) for correct DAG evaluation order.

**Performance features:**
- **Dirty flags** — every engine tracks parameter changes and input generation. When nothing has changed, `update()` returns immediately (~5 ns overhead)
- **RBW cache** — `renderSpectrum()` caches the expensive Gaussian convolution; jitter + VBW still animate every frame for a live instrument feel
- **PFB channel cache** — channel bin indices are recomputed only when the frequency grid or sample rate changes

## License

[MIT](LICENSE)
