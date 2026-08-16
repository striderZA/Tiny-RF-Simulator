---
type: Entry Point
title: RF Simulator — Quickstart
description: Entry point for the RF Simulator documentation. Covers repository layout, build & test commands, key architectural concepts, recent milestones, and links to all major wiki sections.
tags: [quickstart, entrypoint, rf-simulator]
---

# RF Simulator — Quickstart

RF Simulator is a **modular RF signal chain simulator** with a real-time spectrum display. Design a cascade of RF components (generators, amplifiers, mixers, filters, ADCs, channelizers) in a visual node editor, probe any node, and see the spectrum update live.

**Language:** C++20 | **Build:** CMake 3.20+ / Ninja | **UI:** Dear ImGui (docking) + ImPlot + imnodes | **Tests:** Catch2 v3.4.0 + imgui_test_engine | **Version:** v0.19.1

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
| `core/` | GLFW window, ImGui/ImPlot lifecycle, main loop (`RfSimulatorCore`, PIMPL in `core/src/core.cpp`) |
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
| `network_analyzer/` | Singleton network analyzer instrument: Point A/B probe pins, gain + noise-figure sweep over a cloned private chain ("cheat" mode) |
| `iq_plot/` | Time-domain I/Q waveform (IFFT from spectrum) with ring buffer and zoom |
| `node_graph/` | Node graph topology engine + imnodes-based editor + subcircuit groups |
| `touchstone/` | Touchstone .sNp file parser + S-parameter interpolation |
| `help/` | Help window with F1 hotkey and Help menu entry, data-driven quick-reference sections |
| `tutorial/` | Interactive first-run guided walkthrough: data-driven step catalog, `TutorialState` (pure logic + completion marker), `TutorialWidget` (panel highlight) |
| `icon_registry/` | Node icon texture management (PNG → OpenGL) |
| `layout/` | Exe-relative ImGui layout persistence (default + named presets) |
| `logging/` | Singleton logger with ImGui viewer |
| `tests/` | Catch2 unit tests (~340 test cases, 14 benchmarks) |
| `test_engine/` | ImGui test engine UI tests |
| `component_data/` | S-parameter data files (.s2p/.sNp) + JSON component library definitions (amplifiers, filters, equalizers, etc.) |
| `src/` | `main.cpp` entry point |
| `docs/` | Engineering docs (nonlinear model, PFB, ADC, Touchstone specs) |

> The extension system's built-in root (`<source>/extensions/`) is scanned only if present — the repo ships no built-in extension payload; test fixtures live in `tests/fixtures/extensions/`. Global (`~/.rf-sim/extensions/`) and project-local (`<project>/rf-sim-extensions/`) roots are scanned too.

---

## Documentation Map

| Page | What It Covers |
|---|---|
| [Architecture Overview](architecture/overview.md) | Engine+Widget pattern, signal chain, dirty flags, generation counters |
| [RF Components](domains/rf-components.md) | Every DSP engine module + instrument (network analyzer): purpose, parameters, design decisions |
| [DSP Pipeline & Workflows](workflows/dsp-pipeline.md) | Frame loop, signal routing, topological sort, probe system |
| [S-Parameter System](integrations/s-param-system.md) | Touchstone parser, per-component S-param mode, interpolation |
| [Testing Guide](testing/guidance.md) | Unit tests, benchmarks, UI tests — structure and patterns |
| [Build & Operations](operations/build-runbook.md) | Building, CI, debugging, configuration, common issues |

---

## Task Routing

| Change area / intent | Wiki page | Source entry points | Key symbols / types | Focused tests | Minimal validation |
|---|---|---|---|---|---|
| Add / modify a DSP engine component | [RF Components](domains/rf-components.md), [Architecture](architecture/overview.md) | `app/src/component_type_registry.cpp`, `<component>/src/*_engine.cpp`, `app/src/inspector_panel.cpp` (`drawerMap()`) | `IComponentEngine`, `ComponentTypeDescriptor`, `ComponentRegistry` | `tests/test_<component>.cpp` | `ctest --test-dir build -R <component> --output-on-failure` |
| Change the node graph / topology / probes | [DSP Pipeline](workflows/dsp-pipeline.md) | `node_graph/src/node_graph_engine.cpp`, `node_graph/src/node_graph_widget.cpp` | `NodeGraphEngine`, `GraphNode`, `GraphLink`, `SignalSource` | `test_node_graph_engine.cpp`, `test_issue42_multi_output.cpp` | `ctest --test-dir build -R "node_graph|issue42" --output-on-failure` |
| S-parameter / Touchstone work | [S-Parameter System](integrations/s-param-system.md) | `touchstone/src/touchstone_parser.cpp`, `touchstone/src/s_parameter_data.cpp`, `app/src/project_serializer.cpp`, `app/src/component_library.cpp` | `TouchstoneParser`, `SParameterData`, `resolveSparamPath` | `test_touchstone.cpp`, `test_*_sparam.cpp`, `test_path_containment.cpp` | `ctest --test-dir build -R "touchstone|sparam|path_containment" --output-on-failure` |
| Project save/load (`.rfsim`) | [Architecture](architecture/overview.md) | `app/src/project_serializer.cpp`, engine `serialize()`/`deserialize()` | `ProjectSerializer` | `test_project_file.cpp` (17 cases) | `build/bin/test_project_file` or `ctest --test-dir build -R test_project_file --output-on-failure` |
| Network Analyzer instrument | [Architecture](architecture/overview.md#network-analyzer-instrument), [RF Components](domains/rf-components.md#network-analyzer-network_analyzer) | `network_analyzer/src/network_analyzer_engine.cpp`, `network_analyzer/src/network_analyzer_widget.cpp`, `app/src/app.cpp` (`NaHost`/`NaScratch`) | `NetworkAnalyzerEngine`, `INetworkAnalyzerHost`, `INetworkAnalyzerScratch` | `test_network_analyzer.cpp`, NA round-trip cases in `test_project_file.cpp` | `build/bin/test_network_analyzer` |
| Extensions / external tools | [Architecture](architecture/overview.md) | `app/src/extension_manager.cpp`, `app/src/extension_manifest.cpp`, `app/src/external_tool_runner.cpp` | `ExtensionManager`, `ExtensionManifest`, `ExternalToolRunner` | `test_extensions.cpp` | `build/bin/test_extensions` |
| Tutorial / first-run flow | [Architecture](architecture/overview.md) | `tutorial/src/tutorial_widget.cpp`, `tutorial/include/tutorial_state.h`, `app/src/app.cpp` | `TutorialState`, `TutorialWidget` | `test_tutorial_state.cpp`, UI tests `tutorial_*` | `build/bin/test_tutorial_state`; `xvfb-run build/bin/test_ui` |
| Library browser / component authoring | [RF Components](domains/rf-components.md) | `app/src/component_library.cpp`, `app/src/component_form_model.cpp`, `app/src/component_form_widget.cpp` | `ComponentLibrary`, `ComponentFormModel`, `ComponentFormWidget` | `test_component_library.cpp`, `test_component_authoring.cpp` | `build/bin/test_component_authoring` |
| UI / ImGui panel work | [DSP Pipeline](workflows/dsp-pipeline.md) | `app/src/app.cpp` (`draw_ui`), `<component>/src/*_widget.cpp` | widget classes, `RfSimulatorApp::draw_ui` | UI tests in `test_engine/ui_tests.cpp` | `xvfb-run build/bin/test_ui` |

---

## Key Concepts

**Engine + Widget separation** — Every RF component is split into a pure-DSP `*Engine` (no UI includes) and an optional `*Widget` (ImGui UI). Only widget files include `<imgui.h>`.

**Spectrum data structure** — All signals flow as `Spectrum` objects containing a frequency grid, discrete tones (`{freq, power_dBm, phase_deg}`), and noise PSD vectors in **W/Hz** (migrated from per-bin W; the old `addedNoisePerBin_W()` helper is deprecated).

**Noise model** — Noise is stored as power spectral density (W/Hz) throughout the signal chain. Each engine adds noise density appropriate to its model (thermal noise floor at kT ≈ −174 dBm/Hz, noise figure, or NSD).

**Project save/load** — Full circuit persistence to `.rfsim` JSON files (v0.8.0). Every engine implements `serialize()`/`deserialize()` via nlohmann/json. Graph topology, node positions, links, and probes are all round-tripped. Unsaved-changes dialog with dirty tracking.

**S-parameter mode** — Five components (amplifier, ideal filter, equalizer, attenuator, combiner) support dual-mode operation: ideal parametric OR Touchstone .sNp file driven. S-parameter data files live in `component_data/`.

**Component library** — File-based library browser (v0.9.0) with global and per-project JSON component definitions. Supports 8 categories (amplifiers, attenuators, splitters, filters, mixers, equalizers, combiners, ADCs) with datasheet parameters (gain, NF, OIP3, P1dB). One-click insert into the node graph via View menu. In-app authoring (v0.16.0) adds a New/Edit Component form (`ComponentFormModel`/`ComponentFormWidget`) that writes schema-v2 JSON, validated against the [ComponentTypeRegistry](architecture/overview.md) schema; built-in `component_data/library/` entries are read-only.

**P1dB parameter** — First-class 1-dB compression point support (v0.9.0) on `NonlinearModel` and `AmplifierEngine`. Automatic OIP3 ↔ P1dB derivation (OIP3 = P1dB + 9.6 dB) when OIP3 is at default. Persisted in project save/load.

**Subcircuit groups** — The node graph supports grouping nodes into subcircuits with automatic boundary pin synthesis, collapse/expand, and rename.

**Dirty-flag caching** — Each `Spectrum` has a `generation` counter. Engines cache `(input*, generation)` pairs and skip recomputation when nothing changed (~5 ns overhead for cached skip).

**Node graph** — Components are wired visually in an imnodes-based editor. The `NodeGraphEngine` provides topological sort (Kahn's algorithm) for correct DAG evaluation order. Subcircuit groups provide visual-only collapse/expand. Routing and probes resolve `SignalSource{node, output_index}` pairs so multi-output components (Splitter, PFB) connect to the correct port (`outputs[1]`, not always `outputs[0]`).

**Guided tutorial** (v0.17.0) — New users get a one-time first-run "Welcome" modal offering a data-driven 6-step walkthrough (`tutorial/` module, sibling to `help/`/`layout/`). Each step highlights its target panel via a foreground-drawlist outline and a floating "Tutorial Guide" window with Back/Next/Skip/Exit. Completion persists to an exe-relative `.tutorial_completed` marker (mirrors `LayoutManager`; deliberately not `SessionState`, which is Windows-only), so the offer never repeats. `Help > Tutorial` re-runs it, routed through the same unsaved-changes guard as New/Open/Exit. See [architecture overview](architecture/overview.md) and [testing guide](testing/guidance.md).

**Extension system** (v0.16.0) — Manifest-based plugins (`plugin.json`) for data packs and external tools. `ExtensionManager` discovers them across built-in (`<source>/extensions/`, scanned if present), global (`~/.rf-sim/extensions/`), and project-local (`<project>/rf-sim-extensions/`) roots; `ExternalToolRunner` executes approved tools via a JSON request/result file handshake. Surfaced through a Tools menu and an Extensions panel. See [architecture overview](architecture/overview.md).

**Network analyzer** (v0.19.x) — A singleton instrument panel (like the Spectrum Analyzer) that measures gain and noise figure of the signal chain between two probe points, Point A (reference) and Point B (measured). It is **not** an `IComponentEngine`: it has no graph node, no pins, no registry row. The v3 engine (`NetworkAnalyzerEngine`) walks the real graph's links, requires exactly one distinct path from A to B that never crosses a Combiner's combined input, then clones the discovered chain onto a private, throwaway scratch graph (`INetworkAnalyzerScratch`) and sweeps a synthetic tone-comb stimulus across 2–2001 points — the live simulation is never read for signal purposes nor written to ("cheat" mode). Sweep params and Point A/B survive project save/load; a signature-gated dirty check (each chain node's `serialize()` dump + sweep params) skips the expensive clone-and-cascade when nothing changed. Toggled via `View > Network Analyzer`. See [architecture overview](architecture/overview.md), [RF components](domains/rf-components.md), and [DSP pipeline](workflows/dsp-pipeline.md).

**S-param path containment** (v0.19.x, 2026-08-09 codebase review) — Two security hardenings: S1 confines S-parameter file paths to the project directory on `.rfsim` load (`ProjectSerializer::resolveSparamPath`) and to the library JSON's directory on instantiation (`ComponentLibrary::resolveDataFilePath`), relativizing in-project paths on save; S2 caps Touchstone input at 256 MiB and 10M frequency points enforced during the parse loop. Covered by the `test_path_containment` standalone suite. See [S-parameter system](integrations/s-param-system.md).

---

## Recent Milestones

| Milestone | Date | Description |
|---|---|---|
| Network Analyzer v3 + perf fix | v0.19.x | Singleton gain/NF instrument with Point A/B probe pins and private clone-chain sweep; `View > Network Analyzer`; `.rfsim` round-trip of sweep params + points; v0.19.1 fixes O(N*M) tone matching with 1 Hz cell bucketing + signature-gated dirty check; `test_network_analyzer.cpp` (12 cases) |
| S-param path containment | v0.19.x | S1: `.rfsim` load confines `sparam_filepath` to the project dir, save relativizes in-project paths; library `data_files` confined to the JSON's dir. S2: Touchstone parser 256 MiB size cap + 10M in-loop frequency-point cap. `test_path_containment.cpp` |
| Interactive tutorial mode | v0.17.0 | Data-driven 6-step guided walkthrough with panel highlight, first-run "Welcome" offer, exe-relative `.tutorial_completed` marker; `Help > Tutorial` guarded by the unsaved-changes modal; `test_tutorial_state.cpp` + 5 UI tests |
| Extension system | v0.16.0 | `plugin.json` manifests for data packs + external tools, discovery across built-in/global/project-local roots, JSON request/result `ExternalToolRunner`, Tools menu + Extensions panel; test fixtures in `tests/fixtures/extensions/` (repo ships no built-in extension payload) |
| Component registry unification | v0.16.0 | Single `ComponentTypeRegistry` dispatch table for all 11 types (canvas menu, add, duplicate, save/load, inspector, NodeKind); `RfSimulatorApp` decomposed into `ProjectSerializer` + `PFBViewManager`; S-param modes now reload on project deserialize |
| Spectrum analyzer trace modes | v0.11.0 | 4 trace modes (ClearWrite, MaxHold, MinHold, VideoAverage EWMA) with per-trace history buffers, auto-prune, mode-switch reset; UI controls for trace mode + video average count |
| Library S-param data file import | v0.10.0 | JSON schema v2 with `data_files` array for Touchstone references; auto-load S-param on library instantiation; graceful fallback to single-point params; `[DATA]` indicator in browser |
| Part number display | v0.9.1 | Component blocks show library part number subtitle; 7 new component categories in library |
| Component library | v0.9.0 | File-based library manager with JSON definitions, tree-browser panel, one-click insert; P1dB parameter on AmplifierEngine/NonlinearModel with auto OIP3 derivation; 3 example amplifiers (AM1143, ZX60-33LN+, MGA-62563) |
| Duplicate components | v0.8.4 | Right-click → duplicate copies a component with all parameters (offset position, no connections copied) |
| Marker fix | v0.8.2 | Markers now only consider actively displayed traces; per-trace visibility tracking |
| Project save/load | v0.8.0 | File menu, keyboard shortcuts, unsaved-changes dialog, serialization on all 12 component types, 9 round-trip tests |
| Combiner component | July 14 | 2-input passive RF combiner (Wilkinson -3 dB model), 3-port Touchstone S-param mode, Y-shaped symbol |
| Attenuator component | July 14 | Passive attenuator with manual dB control, passive noise model (NF = atten), S-param mode |
| Touchstone validation | Latest | Input validation, `log10(0)` clamp, `lower_bound` interpolation |
| IQ plot DSP extraction | Latest | Extracted `build_iq_spectrum()` from widget to testable function, added Fs guard |
| Equalizer NaN guards | Latest | NaN guards for `log10(0)`, clamp ref freq |
| Coax phase/presets | Latest | Fixed phase calc (removed redundant 1e-3), clamp connector loss, corrected MT 340 preset |
| ADC cleanup | Latest | Removed dead bits/v_fs params, clamp Fs, use dbToLinear |
| NF/OIP clamp | Latest | Clamp NF ≥ 0 dB, OIP2/OIP3 ≥ −30 dBm |
| S-param rework | July 6 | Deleted generic `SParamEngine`, added per-component S-param modes (amplifier, filter, equalizer) |
| EqualizerEngine | July 6 | New component: gain-slope + S-param mode |
| Subcircuit groups | June 21 | Expandable/collapsible node groups |
| Coax cable | June 18 | MilTech cable presets with K1/K2 loss model |
| v0.3.0 | June 22 | Project save/load (JSON serialization) |

> Note: the repository history was squashed into a single commit, so per-milestone commit refs are no longer available. Milestone dates/descriptions come from `CHANGELOG.md` and source evidence; note that `CHANGELOG.md` itself lags the app version (latest entry 0.11.0 vs current v0.19.1) — the v0.12.0–v0.19.1 rows above are grounded in source (e.g. `network_analyzer/`, `tutorial/`, `app/include/extension_manager.h`, `component_type_registry.h`, `tests/test_path_containment.cpp`).

---

## Backlog

Documented in `ROADMAP.md`, not yet implemented:

| Area | Source anchor | Reason deferred |
|---|---|---|
| Pulsed signal generation | `ROADMAP.md` item 7 | Time-domain pulse capability, no design yet |
| Time-domain view improvements | `ROADMAP.md` item 6 | Beyond the current IQ plot (scroll mode, time/div) |
| Modulation components (AM/FM/PM/QAM/PSK/OFDM) | `ROADMAP.md` item 20 | Fundamental comms blocks, future scope |
| Measurement instruments (power meter, SNR/THD/SFDR) | `ROADMAP.md` item 21 | The network analyzer partially addresses this; meters still planned |
| Plugin SDK for custom components | `ROADMAP.md` item 19 | Extension system covers data packs/tools; component SDK design still TBD |
