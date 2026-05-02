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
- **RF ADC** — sampler with configurable sample rate, NSD noise floor, bit depth, and full-scale voltage; includes aliasing and Nyquist zone effects

### Spectrum Analyzer

Displays separate colored traces for each probe point (up to 4 simultaneous). Adjust start/stop frequency, RBW, VBW, and reference level. Marker with peak search and snap-to-peak navigation.

Noise is modeled as power spectral density (W/Hz). Displayed noise floor depends only on component gain/NF and the analyzer's RBW.

## Tests

```bash
cmake --build build
ctest --test-dir build
```

55 unit tests covering all DSP engines, node graph, touchstone parser, and UI.

## Architecture

C++20 modular library. Each module has an `*_engine` (pure DSP, no UI deps) and optional `*_widget` (ImGui UI). Widgets are per-type property drawers shown in the Inspector Panel (node-selection-driven). The node graph (`imnodes`) drives signal chain topology. `ViewManager` tracks which `SignalNode`s the spectrum analyzer observes.

## Changelog

### 2026-05-01 — Spectrum Analyzer Improvements
- Frequency range extended to ±20 GHz
- Minimum widget height enforced (400×300)
- Multi-probe: up to 4 simultaneous probe points with colored pin indicators
- Separate per-probe spectrum traces with matching colors and legend
- Ctrl+click probes, shift+click removes
- Noise jitter on individual probe traces for live spectrum feel

### 2026-05-01 — Node Symbols
- Per-component vector graphics (sine wave, triangle, mixer cross, ADC, splitter, S-param)
- Drawn via ImDrawList — no external assets

### 2026-05-01 — Inspector Panel
- Replaced 5 floating widget windows (Amplifier, Mixer, Splitter, S-Param Amp, ADC)
- Single panel driven by node selection in the graph
- Hover tooltips on nodes showing key parameters at a glance
- Context menu removed for probes — cleaner graph interaction

### 2026-04-30 — Architecture Cleanup
- Deduplicated pin-ID lookups (6 engines → shared helper on NodeGraphEngine)
- Deduplicated default frequency grid construction (4 engines → `buildDefaultFrequencyGrid()`)
- Deduplicated signal wiring in `update_dsp()` (5 copies → template lambda)
- Removed dead code: `tone` typedef, `NUM_BINS` constant, `AdcEngine::loaded()`
- Fixed hidden `logging_core` dependency in `common` CMake target

### 2026-04-30 — RF ADC Component
- Sampling with configurable Fs, NSD, bit depth, full-scale voltage
- Aliasing with proper Nyquist zone handling
- Pure frequency-domain processing (no time-domain pipeline)
- Frequency-independent noise coupling

### 2026-04-30 — S-Parameter Amplifier
- Multi-port Touchstone support (.s2p/.s3p/.s4p)
- Row-major parameter ordering
- Forward parameter selector combo
- Phase rotation via `std::arg(S)`
- Real-time file reload via portable file dialog
- Per-parameter visibility toggles with collapsible tree

### 2026-04-30 — Node Tooltips
- Per-pin hover tooltips showing signal summary
- Tone count + strongest tone, noise floor, frequency range

## License

MIT
