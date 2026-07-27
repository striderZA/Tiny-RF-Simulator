---
type: Architecture Overview
title: Architecture Overview
description: Four-layer architecture of the RF Simulator — platform core, common data model, DSP engines/widgets, and application orchestrator with node graph, dirty-flag caching, and project save/load.
tags: [architecture, dsp, design-patterns]
---

# Architecture Overview

## Layered Design

The RF Simulator is structured in four layers, each with strict dependency direction:

```
┌─────────────────────────────────────────┐
│  Application Orchestrator (app/)        │
│  RfSimulatorApp, InspectorPanel,        │
│  ComponentRegistry, ComponentLibrary,   │
│  LibraryBrowserWidget, SessionState     │
├─────────────────────────────────────────┤
│  Node Graph (node_graph/)               │
│  NodeGraphEngine + NodeGraphWidget      │
├──────────┬──────────────────────────────┤
│  Widgets │  DSP Engines                 │
│  (ImGui) │  (pure C++, no UI)           │
│          │                              │
│  signal_generator/  amplifier/  attenuator/│
│  mixer/  splitter/  combiner/  coax/     │
│  adc/  ideal_filter/  equalizer/         │
│  pfb_channelizer/  spectrum_analyzer/    │
│  iq_plot/                                │
├──────────┴──────────────────────────────┤
│  Common Data Model (common/)            │
│  IComponentEngine, SignalNode,          │
│  Spectrum, ViewManager, Group           │
├─────────────────────────────────────────┤
│  Platform Core (core/)                  │
│  GLFW window, OpenGL2 renderer,         │
│  ImGui/ImPlot context lifecycle         │
└─────────────────────────────────────────┘
```

## Platform Core (`core/`)

**`RfSimulatorCore`** (recently inlined — previously had its own class, now the logic resides directly in `core/src/core.cpp`) owns the GLFW window, OpenGL2 backend, and the main ImGui/ImPlot context. It provides:

```cpp
void Run(const std::function<void()>& onGui);
```

This executes the frame loop — polling events, starting a new ImGui frame, calling `onGui()` (which runs `RfSimulatorApp::draw_ui()`), then rendering ImGui with the OpenGL2 backend. Docking and multi-viewport are enabled.

The core uses a PIMPL pattern (`struct Impl`) to hide GLFW/OpenGL types from all includers.

**Default layout** (via `ImGui::DockBuilder`):
- Left: Node Editor (root)
- Right: Spectrum Analyzer
- Bottom-right: IQ Plot
- Bottom: Log
- Bottom-left: Properties

## Application Orchestrator (`app/`)

**`RfSimulatorApp`** is the central coordinator, instantiated by `main.cpp`. It owns:

- **All DSP engines** — stored in a `ComponentRegistry` (type-erased, indexable by `std::type_index`)
- **All widgets** — `NodeGraphWidget`, `SpectrumAnalyzerWidget`, `InspectorPanel`, `IQPlotWidget`s, `PFBChannelizerWidget`s, `LoggingWidget`, `SignalGeneratorWidget`s, `HelpWidget`
- **`NodeGraphEngine`** — the topology manager
- **`ViewManager`** — registry of all `SignalNode*` instances

Two methods are called every frame:

| Method | When | What it does |
|---|---|---|
| `update_dsp()` | Before `draw_ui()` each frame | Wires inputs from graph topology, computes topological sort, calls each engine's `update()`, updates probe labels and view states |
| `draw_ui()` | Inside the ImGui frame | Renders all ImGui windows: node editor, spectrum analyzer, IQ plots, PFB grids, properties panel, log, help window |

### ComponentRegistry

Defined in `app/include/component_registry.h`. A type-erased polymorphic container:

- **`add<T>(args...)`** — Creates a `T` engine, registers its `SignalNode` with `ViewManager`, indexes it by `std::type_index`, returns `T&`
- **`remove(graphNodeId)`** — Removes by graph node ID, cleans up view and type index
- **`byType<T>()`** — Returns `std::vector<T*>` for all engines of type `T`
- **`all()`** — Returns a `std::span<IComponentEngine*>` over all engines
- **`find(graphNodeId)`** — Find by graph node ID

### ComponentLibrary

Added in v0.9.0. Defined in `app/include/component_library.h`. A file-based component definition manager:

- **Three scan roots**: built-in examples (`component_data/library/`), global (`~/.rf-sim/libraries/`), per-project (`./rf-sim-libraries/`)
- **JSON component definitions** with datasheet parameters (gain, NF, OIP3, P1dB) organized by type → manufacturer
- **LibraryBrowserWidget** (`app/include/library_browser_widget.h`) provides a tree-view panel with text filter
- **One-click insert** creates a fully configured component in the node graph, sets `part_number` on the graph node for subtitle display (v0.9.1)
- Supports 8 component categories: amplifiers, attenuators, splitters, filters, mixers, equalizers, combiners, ADCs
- **Data file support** (v0.10.0): JSON schema version 2 adds `data_files` array for referencing external data files (S-parameters, etc.). Relative paths resolved against the JSON file's directory. When a library amplifier with S-param data files is instantiated, the engine auto-loads the Touchstone file via `setSParamFilepath()` for frequency-dependent simulation. Falls back to single-point parameters if the file is missing or invalid. Library browser shows `[DATA]` indicator on components with data files.

### Project Save/Load

Save/load (v0.8.0) provides full circuit persistence to `.rfsim` JSON files:

- **File menu** — New (Ctrl+N), Open (Ctrl+O), Save (Ctrl+S), Save As
- **Unsaved-changes dialog** — prompts Save/Discard/Cancel when closing or opening a new project with unsaved changes
- **Dirty tracking** — `RfSimulatorApp::markDirty()` called on parameter edits, node moves, link changes, component add/remove. Dirty flag cleared on save.
- **Serialization** — Every engine implements `serialize()`/`deserialize()` via `nlohmann::json`. `saveProject()`/`loadProject()` orchestrates engine state, graph topology (node positions, links, probes), and component registry reconstruction.
- **Graph state helpers** — `setNextIds()`, `removeAllLinks()` for clean project init and link restoration.

Test coverage: 9 round-trip tests in `tests/test_project_file.cpp` (55 assertions).

### SessionState

**`SessionState`** (Windows-only; no-op on other platforms) persists window visibility toggles to INI files via `save()`/`load()`. Not used for project data (use `.rfsim` files for that).

### Component Data Files

S-parameter data files live in `component_data/` at the repository root, organized by type: `amplifiers/`, `filters/`, `equalizers/`, `fixed_attenuators/`, `splitters/`, `step_attenuators/`. Each directory contains `.s2p`/`.sNp` Touchstone-formatted files that feed the per-component S-param modes.

`common/session_state.h` — Persists window state (open/closed) to `app.ini` using Win32 `WritePrivateProfileStringA`/`GetPrivateProfileStringA`. On non-Windows platforms the load/save methods are no-ops.

## Engine/Widget Pattern

Every RF component follows this pattern:

```
<component>/
├── include/<component>_engine.h    # Pure DSP, no UI deps
├── src/<component>_engine.cpp      # DSP logic
├── include/<component>_widget.h    # ImGui UI (optional)
└── src/<component>_widget.cpp      # Widget rendering
```

**Engine rules:**
- Inherits from `IComponentEngine` (defined in `common/component_interface.h`)
- Owns a `SignalNode m_node` with input and output `Spectrum` vectors (multi-port components use vectors)
- Implements `update(double dt)` — reads from `node().inputs[k]`, processes, writes to `node().outputs[k]`
- Uses dirty-flag caching: cached input pointer + generation counter

**Widget rules:**
- Free function or class that renders ImGui controls
- Binds to an engine by reference
- Only widget `.cpp` files may `#include <imgui.h>` or `<implot.h>`

## Signal Data Model

### Spectrum (`common/spectrum.h`)

Core data structure carrying frequency-domain signal representation:

```cpp
struct Spectrum {
    struct Tone { double freq_Hz, power_dBm, phase_deg; };

    std::vector<double> frequencies;        // Bin center frequencies (Hz)
    std::vector<Tone> tones;                // Discrete tones
    std::vector<double> noise_W;            // Input noise PSD (W/Hz)
    std::vector<double> noise_added_W;      // Added noise PSD (W/Hz)
    std::vector<double> noise_total_W;      // Total noise PSD (W/Hz)
    std::vector<double> phase_deg;          // Phase per bin (degrees)
    double fs_Hz = 0.0;                     // Sample rate (set by ADCs)
    uint64_t generation = 0;                // Dirty-flag counter
};
```

**Design decisions:**
- Noise stored as **power spectral density** in W/Hz (grid-independent for RBW processing).
- `generation` incremented by producers; consumers cache `(input*, generation)` for O(1) skip.
- `fs_Hz` propagated through components, consumed by PFB channelizer.
- `phase_deg` per bin (added recently for phase-aware processing).

### SignalNode (`common/signal_node.h`)

```cpp
struct SignalNode {
    std::vector<const Spectrum*> inputs;
    std::vector<Spectrum> outputs;
    bool view_enabled = false;
};
```

The universal interface between engines. Multi-port components use vector-based inputs/outputs (e.g., `SplitterEngine` has 1 input, 2 outputs; `PFBChannelizerEngine` has 1 input, 2 outputs).

### IComponentEngine (`common/component_interface.h`)

Abstract base for all DSP engines:

```cpp
class IComponentEngine {
    virtual int id() const = 0;
    virtual int graphNodeId() const = 0;
    virtual SignalNode& node() = 0;
    virtual void update(double dt) = 0;
    virtual std::string hoverSummary() const = 0;
    virtual int numInputPins() const { return 1; }
    virtual int numOutputPins() const { return 1; }
    virtual int inputPinId(int port) const;
    virtual int outputPinId(int port) const;

    // Serialization (default no-op)
    virtual nlohmann::json serialize() const;
    virtual void deserialize(const nlohmann::json&);
};
```

**Multi-pin support:** The base interface provides `numInputPins()`/`numOutputPins()` and indexed pin accessors (`inputPinId(int port)`, `outputPinId(int port)`). Default implementations return -1 for ports > 0, preserving backward compatibility with single-pin engines. `SignalNode` supports `std::vector`-based inputs/outputs for multi-port components like Splitter (1 input, 2 outputs) and PFB channelizer (2 outputs).

## Node Graph Topology

**`NodeGraphEngine`** (in `node_graph/include/node_graph_engine.h`) manages the signal flow DAG:

- **Nodes** — `GraphNode{node_id, input_pin_ids, output_pin_ids, signal_node*, label}`
- **Links** — `GraphLink{link_id, start_pin_id, end_pin_id}`
- **Probes** — Up to 4 output pins can be probed for spectrum display with distinct colors (teal, orange, purple, blue)

### ID Space Partitioning

| Concept | Range |
|---|---|
| `GraphNode::node_id` | 1..49999 |
| `Group::id` | 50000..99999 |
| `GroupBoundaryPin::id` | 100000+ |
| Phantom node ids (workaround) | 200000+ |

### Topological Sort

`topologicalOrder()` uses **Kahn's algorithm** to produce a linear evaluation order for the signal DAG. Cycles are handled gracefully (remaining nodes appended unsorted).

## Subcircuit Groups

Defined in `common/include/group.h`. A **visual-only** grouping mechanism:

- **`Group`** — `{id, name, member_node_ids, boundary_pins, collapsed}`
- **`GroupBoundaryPin`** — Synthesized pin on a collapsed group representing a cross-boundary link
- **Snapshot model** — `member_node_ids` is frozen at creation
- **DSP graph stays flat** — groups affect rendering only, not signal processing
- Auto-removed when a member node is removed or group drops below 2 members

## Dirty-Flag Caching Pattern

Used consistently across all engines to avoid redundant recomputation:

```cpp
if (!m_dirty && m_cached_input_ptr == &input &&
    m_cached_input_generation == input.generation) {
    return; // No change — skip
}
m_cached_input_ptr = &input;
m_cached_input_generation = input.generation;
// ... recompute ...
m_dirty = false;
```

This adds ~5 ns overhead for the cached skip path. Additional caches include:
- **RBW convolution cache** in spectrum analyzer (expensive Gaussian filter cached, only jitter+VBW runs per frame)
- **PFB channel cache** (bin indices recomputed only when grid/Fs changes)

## Key Source Files

| File | Role |
|---|---|
| `core/src/core.cpp` | GLFW window, ImGui/ImPlot init, default dock layout |
| `app/include/app.h` | `RfSimulatorApp` — orchestrator |
| `app/src/app.cpp` | update_dsp, draw_ui, component lifecycle wiring |
| `app/include/component_registry.h` | Type-erased engine container |
| `app/include/component_library.h` | File-based component library manager |
| `app/include/library_browser_widget.h` | Library browser tree-view widget |
| `app/include/inspector_panel.h` | Properties panel header |
| `app/src/inspector_panel.cpp` | Per-component property editors (23KB) |
| `common/include/component_interface.h` | `IComponentEngine` abstract base |
| `common/include/spectrum.h` | `Spectrum` data structure |
| `common/include/signal_node.h` | `SignalNode` structure |
| `common/include/group.h` | `Group` + `GroupBoundaryPin` |
| `common/include/nonlinear_model.h` | `NonlinearModel` (OIP2/OIP3) |
| `common/include/view_manager.h` | `ViewManager` — node visibility tracking |
| `common/include/session_state.h` | `SessionState` — window state persistence |
| `node_graph/include/node_graph_engine.h` | `NodeGraphEngine` + `GraphNode`/`GraphLink` |
| `node_graph/src/node_graph_widget.cpp` | Full imnodes rendering (40KB, largest file) |
| `src/main.cpp` | Entry point (19 lines) |
