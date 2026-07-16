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

1. For each input pin, call `NodeGraphEngine::getSourceForInput(comp_node_id, input_pin_id)` to find the upstream node.
2. Set `component->node().inputs[k] = &source->outputs[0]` — a raw pointer, zero-copy.

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

Call `NodeGraphEngine::probedSignalNodes()` to get up to 4 probed `SignalNode*` references. Set `view_enabled` flags accordingly:

- Each probed node gets `view_enabled = true`
- Previously probed but no-longer-probed nodes get `view_enabled = false`

### Step 5 — Spectrum Analyzer Update

- Pass probe labels and PFB engine pointers to `SpectrumAnalyzerWidget`.
- The spectrum analyzer accumulates `renderCombinedSpectrum()` from all `view_enabled` nodes.

---

## UI Render Flow

`RfSimulatorApp::draw_ui()` renders in this order:

```
Node Editor         ← NodeGraphWidget::draw()
Spectrum Analyzer   ← SpectrumAnalyzerWidget::draw()
IQ Plot (per PFB)   ← IQPlotWidget::draw()
Channelizer Grid    ← PFBChannelizerWidget::draw()
Properties Panel    ← InspectorPanel::draw()  (selected component)
Generator Widgets   ← SignalGeneratorWidget::draw()
Log                 ← LoggingWidget::draw()
```

All windows are gated by boolean visibility flags persisted in `SessionState`.

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

Probe colors (in order): teal (`#00CED1`), orange (`#FFA500`), purple (`#9370DB`), blue (`#00BFFF`).

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
3. Removes engine from `ComponentRegistry`.
4. Probes on removed node are cleaned up.
5. Next frame: node disappears from graph, signal chain re-routes.

---

## Session State Persistence

`SessionState` (`common/include/session_state.h`) reads/writes `app.ini` using Windows INI APIs (no-op on other platforms). Persists:

- Window position and size
- Visibility flags for sub-windows (Log, IQ Plot, etc.)
- PFB active channel selections

---

## Future Workflows (Planned)

| Workflow | Status | Notes |
|---|---|---|
| Pulsed signal generation | 📋 Planned | Time-domain pulse capability |
| Time-domain view improvements | 📋 Planned | Beyond current IQ plot |
| Spectrum analyzer enhancements | 📋 Planned | More analyzer features |
| RF-accurate node-graph components | 📋 Planned | Improved RF representation |
| Ponytail cleanup | Partially planned | ~1100–1330 line reduction |

See [ROADMAP.md](/ROADMAP.md) for full feature tracking.
