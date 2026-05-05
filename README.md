<p align="center">
  <img src="assets/banner.png" alt="RF Simulator Banner" width="70%">
</p>

# RF Simulator

Modular RF signal chain simulator with real-time spectrum display using Dear ImGui + ImPlot.
Design a cascade of RF components and probe any node to see the spectrum.

## Quick Start

**Dependencies:** OpenGL, CMake ≥ 3.20, Ninja build system.

```bash
cmake -B build -G Ninja
cmake --build build
build/bin/main.exe
```

Dependencies (ImGui docking, ImPlot, GLFW, imnodes, Catch2, portable-file-dialogs) are auto-fetched via CMake FetchContent.

## Usage

### Node Editor (main workspace)

Build your RF chain visually: add components via right-click context menu on the canvas. Connect output pins to input pins to wire the signal path.

| Action | Effect |
|--------|--------|
| **Left-click** node | Select — shows properties in Inspector Panel |
| **Ctrl+left-click** node | Add spectrum probe (up to 4) |
| **Shift+left-click** node | Remove spectrum probe |
| **Left-click** output pin | Toggle spectrum probe |
| **Right-click** canvas | Add component |
| **Right-click** node | Remove / context menu |
| **Delete key** | Remove selected node |
| **Hover** node | Tooltip with key parameters |
| **Hover** pin | Tooltip with signal summary |

### Components

- **Generator** — tone generator (frequency, amplitude, phase) with flat thermal noise floor
- **Amplifier** — gain (dB) + noise figure (dB), adds amplified noise
- **Splitter** — 1-in/2-out, -3 dB split loss
- **Mixer** — frequency conversion with LO, sum/difference tones, conversion gain
- **S-Parameter Amplifier** — frequency-dependent gain from Touchstone (.s2p/.s3p/.s4p) files, with phase rotation and file browser
- **S-Parameter Filter** — passive frequency-selective component using Touchstone S-parameter data
- **RF ADC** — sampler with configurable sample rate, NSD noise floor, bit depth, and full-scale voltage; includes aliasing and Nyquist zone effects
- **PFB Channelizer** — polyphase filter bank channelizer (M channels, K taps/branch, Kaiser window) with per-channel noise and tone routing

### Spectrum Analyzer

Displays separate colored traces for each probe point (up to 4 simultaneous). Adjust start/stop frequency, RBW, VBW, and reference level. Marker with peak search and snap-to-peak navigation. Drag-to-zoom on the frequency axis with a reset button.

Noise jitter animates the trace like a real instrument. The RBW convolution is cached internally — when signal data is static, only the jitter+VBW pass runs each frame, preserving the live feel without redoing the expensive Gaussian filter.

Noise is modeled as power spectral density (W/Hz). Displayed noise floor depends only on component gain/NF and the analyzer's RBW.

## Tests

```bash
cmake --build build
ctest --test-dir build
```

66 tests (60 unit + 6 benchmarks) covering all DSP engines, node graph, touchstone parser, PFB channelizer, and UI.

Run the DSP benchmarks to see per-operation timing:

```bash
build/bin/tests.exe [bench]
```

## Architecture

C++20 modular library. Each module has an `*_engine` (pure DSP, no UI deps) and optional `*_widget` (ImGui UI). Widgets are per-type property drawers shown in the Inspector Panel (node-selection-driven). The node graph (`imnodes`) drives signal chain topology. `ViewManager` tracks which `SignalNode`s the spectrum analyzer observes.

**Signal chain:** Components produce `Spectrum` objects (frequency bins + tone list + noise PSD). Signal wiring uses `const Spectrum*` pointers — zero-copy propagation. The node graph provides topological sort (Kahn's algorithm) for correct DAG evaluation order.

**Performance features:**
- **Dirty flags** — every engine tracks parameter changes and input generation. When nothing has changed, `update()` returns immediately (~5 ns overhead)
- **RBW cache** — `renderSpectrum()` caches the expensive Gaussian convolution; jitter + VBW still animate every frame for a live instrument feel
- **PFB channel cache** — channel bin indices are recomputed only when the frequency grid or sample rate changes

## License

MIT
