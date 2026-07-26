---
type: Entry Point
title: RF Simulator — Quickstart
description: Entry point for the RF Simulator documentation. Covers repository layout, build & test commands, key architectural concepts, recent milestones, and links to all major wiki sections.
tags: [quickstart, entrypoint, rf-simulator]
---

# RF Simulator — Quickstart

RF Simulator is a **modular RF signal chain simulator** with a real-time spectrum display. Design a cascade of RF components (generators, amplifiers, mixers, filters, ADCs, channelizers) in a visual node editor, probe any node, and see the spectrum update live.

**Language:** C++20 | **Build:** CMake 3.20+ / Ninja | **UI:** Dear ImGui (docking) + ImPlot + imnodes | **Tests:** Catch2 v3.4.0 + imgui_test_engine | **Version:** v0.11.1

---

## Quick Start

### Prerequisites

| Dependency | Minimum | Notes |
|---|---|---|
| Compiler | C++20 | GCC 11+ (MinGW-w64 on Windows), Clang 14+ |
| CMake | >= 3.20 | [cmake.org/download](https://cmake.org/download) |
| Ninja | >= 1.10 | [ninja-build.org](https://ninja-build.org) |
| OpenGL | 2.1+ | System-provided on all platforms |

> **Windows:** MSVC is not supported. Use MinGW-w64. Delete `build/` if switching compilers.

### Build & Run

```bash
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc
cmake --build build
build/bin/tiny-rf-simulator.exe        # Windows
# build/bin/tiny-rf-simulator          # Linux/macOS
```

First build takes 60–90s (FetchContent downloads all dependencies). Subsequent builds are fast.

### Run Tests

```bash
ctest --test-dir build --output-on-failure

# Benchmarks only
build/bin/tests [bench]
```

---

## Repository Layout

| Path | Purpose |
|---|---|
| `app/` | Application orchestrator (`RfSimulatorApp`), component registry, library browser, inspector panel |
| `core/` | GLFW window, ImGui/ImPlot lifecycle, main loop / previously `RfSimulatorCore` (now inlined) |
| `common/` | Header-only data model: `Spectrum`, `SignalNode`, `IComponentEngine`, `ViewManager`, `Group` |
| `signal_generator/` | Tone generator engine + widget |
| `amplifier/` | Gain + noise figure + nonlinearity (OIP2/OIP3) + S-param mode |
| `mixer/` | Frequency conversion with LO, sum/difference |
| `splitter/` | 1-to-2 power splitter (-3 dB) |
| `ideal_filter/` | Brickwall LPF/HPF/BPF/BSF + S-param mode |
| `equalizer/` | Gain-slope (dB/decade) equalizer + S-param mode |
| `attenuator/` | Passive attenuator with manual dB control + S-param mode, passive noise model |
| `combiner/` | 2-input, 1-output passive RF combiner (Wilkinson -3 dB model) + 3-port S-param mode |
| `coax/` | Coaxial cable loss/phase model (MilTech presets) |
| `adc/` | RF ADC with sampling, aliasing, NSD noise model |
| `pfb_channelizer/` | Polyphase filter bank (M channels, K taps) |
| `spectrum_analyzer/` | Real-time spectrum display (RBW, VBW, jitter, markers, peaks) |
| `iq_plot/` | Time-domain I/Q waveform (IFFT from spectrum) with ring buffer and zoom |
| `node_graph/` | Node graph topology engine + imnodes-based editor + subcircuit groups |
| `touchstone/` | Touchstone .sNp file parser + S-parameter interpolation |
| `icon_registry/` | Node icon texture management (PNG → OpenGL) |
| `logging/` | Singleton logger with ImGui viewer |
| `tests/` | Catch2 unit tests (~210 test cases, 14 benchmarks) |
| `test_engine/` | ImGui test engine UI tests |
| `component_data/` | S-parameter data files (.s2p/.sNp) + JSON component library definitions (amplifiers, filters, equalizers, etc.) |
| `src/` | `main.cpp` entry point |
| `docs/` | Engineering docs (nonlinear model, PFB, ADC, Touchstone specs) |

---

## Documentation Map

| Page | What It Covers |
|---|---|
| [Architecture Overview](architecture/overview.md) | Engine+Widget pattern, signal chain, dirty flags, generation counters |
| [RF Components](domains/rf-components.md) | Every DSP engine module: purpose, parameters, design decisions |
| [DSP Pipeline & Workflows](workflows/dsp-pipeline.md) | Frame loop, signal routing, topological sort, probe system |
| [S-Parameter System](integrations/s-param-system.md) | Touchstone parser, per-component S-param mode, interpolation |
| [Testing Guide](testing/guidance.md) | Unit tests, benchmarks, UI tests — structure and patterns |
| [Build & Operations](operations/build-runbook.md) | Building, CI, debugging, configuration, common issues |

---

## Key Concepts

**Engine + Widget separation** — Every RF component is split into a pure-DSP `*Engine` (no UI includes) and an optional `*Widget` (ImGui UI). Only widget files include `<imgui.h>`.

**Spectrum data structure** — All signals flow as `Spectrum` objects containing a frequency grid, discrete tones (`{freq, power_dBm, phase_deg}`), and noise PSD vectors in **W/Hz** (migrated from per-bin W; the old `addedNoisePerBin_W()` helper is deprecated).

**Noise model** — Noise is stored as power spectral density (W/Hz) throughout the signal chain. Each engine adds noise density appropriate to its model (thermal noise floor at kT ≈ −174 dBm/Hz, noise figure, or NSD).

**Project save/load** — Full circuit persistence to `.rfsim` JSON files (v0.8.0). Every engine implements `serialize()`/`deserialize()` via nlohmann/json. Graph topology, node positions, links, and probes are all round-tripped. Unsaved-changes dialog with dirty tracking.

**S-parameter mode** — Five components (amplifier, ideal filter, equalizer, attenuator, combiner) support dual-mode operation: ideal parametric OR Touchstone .sNp file driven. S-parameter data files live in `component_data/`.

**Component library** — File-based library browser (v0.9.0) with global and per-project JSON component definitions. Supports 7 categories (amplifiers, attenuators, splitters, filters, mixers, equalizers, combiners, ADCs) with datasheet parameters (gain, NF, OIP3, P1dB). One-click insert into the node graph via View menu.

**P1dB parameter** — First-class 1-dB compression point support (v0.9.0) on `NonlinearModel` and `AmplifierEngine`. Automatic OIP3 ↔ P1dB derivation (OIP3 = P1dB + 9.6 dB) when OIP3 is at default. Persisted in project save/load.

**Subcircuit groups** — The node graph supports grouping nodes into subcircuits with automatic boundary pin synthesis, collapse/expand, and rename.

**Dirty-flag caching** — Each `Spectrum` has a `generation` counter. Engines cache `(input*, generation)` pairs and skip recomputation when nothing changed (~5 ns overhead for cached skip).

**Node graph** — Components are wired visually in an imnodes-based editor. The `NodeGraphEngine` provides topological sort (Kahn's algorithm) for correct DAG evaluation order. Subcircuit groups provide visual-only collapse/expand.

---

## Recent Milestones

| Milestone | Date | Description | Git Ref |
|---|---|---|---|
| Spectrum analyzer trace modes | v0.11.0 | 4 trace modes (ClearWrite, MaxHold, MinHold, VideoAverage EWMA) with per-trace history buffers, auto-prune, mode-switch reset; UI controls for trace mode + video average count | `ab87552` |
| Library S-param data file import | v0.10.0 | JSON schema v2 with `data_files` array for Touchstone references; auto-load S-param on library instantiation; graceful fallback to single-point params; `[DATA]` indicator in browser | `0934e8b` |
| Part number display | v0.9.1 | Component blocks show library part number subtitle; 7 new component categories in library | `0934e8b` |
| Component library | v0.9.0 | File-based library manager with JSON definitions, tree-browser panel, one-click insert; P1dB parameter on AmplifierEngine/NonlinearModel with auto OIP3 derivation; 3 example amplifiers (AM1143, ZX60-33LN+, MGA-62563) | `0934e8b` |
| Duplicate components | v0.8.4 | Right-click → duplicate copies a component with all parameters (offset position, no connections copied) | `0934e8b` |
| Marker fix | v0.8.2 | Markers now only consider actively displayed traces; per-trace visibility tracking | `0934e8b` |
| Project save/load | v0.8.0 | File menu, keyboard shortcuts, unsaved-changes dialog, serialization on all 12 component types, 9 round-trip tests | `77c5611` |
| Combiner component | July 14 | 2-input passive RF combiner (Wilkinson -3 dB model), 3-port Touchstone S-param mode, Y-shaped symbol | `e8226fe` |
| Attenuator component | July 14 | Passive attenuator with manual dB control, passive noise model (NF = atten), S-param mode | `3e4b025` |
| Touchstone validation | Latest | Input validation, `log10(0)` clamp, `lower_bound` interpolation | `6d71618` |
| IQ plot DSP extraction | Latest | Extracted `build_iq_spectrum()` from widget to testable function, added Fs guard | `5cfa82d` |
| Equalizer NaN guards | Latest | NaN guards for `log10(0)`, clamp ref freq | `fdd70ab` |
| Coax phase/presets | Latest | Fixed phase calc (removed redundant 1e-3), clamp connector loss, corrected MT 340 preset | `dc25342` |
| ADC cleanup | Latest | Removed dead bits/v_fs params, clamp Fs, use dbToLinear | `6752e20` |
| NF/OIP clamp | Latest | Clamp NF ≥ 0 dB, OIP2/OIP3 ≥ −30 dBm | `5b8490a` |
| S-param rework | July 6 | Deleted generic `SParamEngine`, added per-component S-param modes (amplifier, filter, equalizer) | `c8f5bd9` |
| EqualizerEngine | July 6 | New component: gain-slope + S-param mode | `d584d4a` |
| Subcircuit groups | June 21 | Expandable/collapsible node groups | Earlier |
| Coax cable | June 18 | MilTech cable presets with K1/K2 loss model | Earlier |
| v0.3.0 | June 22 | Project save/load (JSON serialization) | Earlier |
