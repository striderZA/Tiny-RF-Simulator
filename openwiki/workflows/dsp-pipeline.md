---
type: Workflow
title: "DSP Pipeline & Workflows"
description: "Per-frame DSP execution flow, signal routing, topological sort, probe system, UI render order, and component lifecycle."
tags: [dsp, pipeline, workflow, signal-flow]
---

# DSP Pipeline & Workflows

This page covers the per-frame execution flow, signal routing logic, probe system, and how the application bootstraps.

---

## Application Bootstrap

```
src/main.cpp
  ├─ Creates RfSimulatorCore (window, ImGui, ImPlot, GLFW, OpenGL2)
  ├─ Creates ImNodes context
  ├─ Creates RfSimulatorApp
  │     ├─ Constructs NodeGraphWidget (canvas context menu wiring)
  │     ├─ Adds default SignalGenerator + Amplifier
  │     ├─ Constructs SpectrumAnalyzerWidget
  │     ├─ Constructs NetworkAnalyzerWidget (singleton instrument)
  │     ├─ Constructs IQPlotWidget(s), PFBChannelizerWidget(s)
  │     └─ Constructs InspectorPanel (callback wiring)
  └─ core.Run(lambda)
        └─ Each frame: app.update_dsp() + app.draw_ui()
```

---

## Per-Frame DSP Pipeline

`RfSimulatorApp::update_dsp()` executes in this order each frame:

### Step 1 — Signal Routing

For every component in `m_components`:

1. For each input pin, call `NodeGraphEngine::getSourceForInput(input_pin_id)` to find the upstream node.
2. Set `component->node().inputs[k] = &source.node->outputs[source.output_index]` — a raw pointer, zero-copy. Since v0.16.1 the lookup returns a `SignalSource{node, output_index}` pair, so splitter/PFB `OUT2` connects to `outputs[1]` instead of always `outputs[0]` (issue #42); severed inputs are nulled.

### Step 2 — Topological Sort

`NodeGraphEngine::topologicalOrder()` runs Kahn's algorithm:

1. Compute in-degree for every node from current links.
2. Enqueue zero-in-degree nodes.
3. Process queue: add to ordered list, decrement downstream in-degrees, enqueue newly zero nodes.
4. If remaining nodes exist (cycle), append them unsorted (should not happen in normal use).

### Step 3 — DSP Update

Iterate components in topological order and call `engine->update(0.0)` for each:

- Each engine checks dirty flags: if its input pointer and generation haven't changed since last frame, return immediately.
- Otherwise: read input `Spectrum`, apply component-specific processing, write output `Spectrum`, increment `generation`.

### Step 4 — Probe Sync

Call `NodeGraphEngine::probedSignalNodes()` to get up to 4 probed `SignalSource{node, output_index}` pairs. Set `view_enabled` flags accordingly:

- Each probed node gets `view_enabled = true`
- Previously probed but no-longer-probed nodes get `view_enabled = false`
- Probe labels carry an `OUT2`/`OUT3` suffix when the resolved output index > 0, and `SpectrumAnalyzerWidget::setProbeTargets()` renders the probed port's `Spectrum` (not always `outputs[0]`, issue #42)

### Step 5 — Spectrum Analyzer Update

- Pass probe labels and PFB engine pointers to `SpectrumAnalyzerWidget`.
- The spectrum analyzer accumulates `renderCombinedSpectrum()` from all `view_enabled` nodes.

---

## UI Render Flow

`RfSimulatorApp::draw_ui()` renders in this order:

```
Main Menu Bar        ← File / View / Help menus with keyboard shortcuts
Node Editor         ← NodeGraphWidget::draw()
Spectrum Analyzer   ← SpectrumAnalyzerWidget::draw()
Network Analyzer    ← NetworkAnalyzerEngine::update() + NetworkAnalyzerWidget::draw() (only while m_show_na)
IQ Plot (per PFB)   ← IQPlotWidget::draw()
Channelizer Grid    ← PFBChannelizerWidget::draw()
Properties Panel    ← InspectorPanel::draw()  (selected component)
Generator Widgets   ← SignalGeneratorWidget::draw()
Log                 ← LoggingWidget::draw()
Help (How to Use)   ← HelpWidget::draw()      (toggled via F1 or Help > How to Use)
Tutorial Guide      ← TutorialWidget::draw()  (floating walkthrough window; inactive unless running)
```

The main menu bar includes **File** (New/Open/Save/Exit with keyboard shortcuts), **View** (toggle Log, Spectrum Analyzer, Network Analyzer, Properties, Node Editor, Component Library), and **Help** (toggle "How to Use" panel via F1, plus `Help > Tutorial`). Keyboard shortcuts (Ctrl+S, Ctrl+O, Ctrl+N, F1) are only active when text fields are not focused (`!io.WantTextInput`).

The **Network Analyzer** panel is a singleton instrument (like the Spectrum Analyzer): while visible, `RfSimulatorApp::draw_ui()` first calls `m_na_engine.update()` — which finds the unique path between Point A and Point B over the real graph, gates on a serialize-dump signature, and runs the clone-chain measurement — then renders `m_na_widget->draw()`. Its sweep/point edits fire `onParamChange` → `markDirty()` like component params. See [Network Analyzer Instrument](../architecture/overview.md#network-analyzer-instrument) and [RF Components](../domains/rf-components.md#network-analyzer-network_analyzer).

A one-time first-run "Welcome to Tiny RF Simulator" modal (v0.17.0) offers the [guided tutorial](../architecture/overview.md) — either answer marks it completed via the exe-relative `.tutorial_completed` marker, so it never nags again.

All windows are gated by boolean visibility flags persisted in `SessionState`, including the help window state (`m_show_help`, saved as `"WindowState.Help"`).

---

## Probe Selection Flow

```
User clicks output pin in NodeGraphWidget
  └─ NodeGraphWidget::handleProbeClick(pin_id)
        └─ NodeGraphEngine::setActiveProbePin(pin_id)
              ├─ Tracks up to 4 probes with distinct colors
              └─ Next frame: update_dsp() syncs view_enabled
                    └─ SpectrumAnalyzerWidget shows probed node spectrum
```

Probe colors (in order): teal (`#16C79A`), orange (`#E69628`), purple (`#7832AA`), blue (`#3C8CDC`).

---

## Component Lifecycle

### Adding a Component

1. User right-clicks canvas → context menu → selects component type.
2. `NodeGraphWidget::onAdd*` callback triggered (wired in `RfSimulatorApp` constructor).
3. `RfSimulatorApp` creates new engine instance, registers in `m_components`.
4. Adds `GraphNode` with input/output pins to `NodeGraphEngine`.
5. Next frame: engine appears in node graph, user can wire it.

### Removing a Component

1. User right-clicks node → "Remove" or selects node + Delete key.
2. `RfSimulatorApp` removes node from `NodeGraphEngine` (cascading: removes links, auto-removes groups if membership drops below 2).
3. Removes engine from `ComponentRegistry`, then calls `rewireInputs()` **synchronously** so no surviving component keeps a dangling `Spectrum*` into the destroyed engine's `SignalNode` — widgets that dereference `node().inputs[]` during `draw_ui()` (e.g. `PFBChannelizerWidget`) would otherwise use-after-free (issue #37).
4. Probes on removed node are cleaned up.
5. Next frame: node disappears from graph, signal chain re-routes.

---

## Session State Persistence

`SessionState` (`common/session_state.h`) reads/writes `app.ini` using Windows INI APIs (no-op on other platforms). Persists:

- Window position and size
- Visibility flags for sub-windows (Log, IQ Plot, Spectrum Analyzer, **Network Analyzer**, etc.)
- PFB active channel selections

---

## Future Workflows (Planned)

| Workflow | Status | Notes |
|---|---|---|
| Pulsed signal generation | 📋 Planned | Time-domain pulse capability |
| Time-domain view improvements | 📋 Planned | Beyond current IQ plot |
| Spectrum analyzer enhancements | ✅ Completed (v0.11.0) | MaxHold, MinHold, VideoAverage trace modes with per-trace history |
| RF-accurate node-graph components | 📋 Planned | Improved RF representation |

See [ROADMAP.md](/ROADMAP.md) for full feature tracking.
