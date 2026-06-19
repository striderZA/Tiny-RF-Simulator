# Multi-Port S-Parameter Component — Revised Design

**Date:** 2026-06-19
**Status:** Approved
**Supersedes:** `2026-06-19-multiport-sparam-component-design.md`

## 1. The Problem

The previous design gave every Touchstone port both an input pin and an output pin. For a 3-port device this means 6 pins on the node — excessive for common devices like splitters and combiners where most ports function unidirectionally.

## 2. The Fix: Mode-Aware Pin Layout

`SParamEngine` gets a `Mode` enum that determines how many pins are created, their direction, and how signals are routed internally.

### 2.1 Mode: Splitter

One input (the common port), N−1 outputs.

```
  ┌────────────────────┐
  │              Port 1 ├──► Port 1 OUT (S₁₂ × input)
──┤   (Common) Port 2  │
  │              Port 3 ├──► Port 3 OUT (S₃₂ × input)
  └────────────────────┘
```

- `numInputPins()` = 1
- `numOutputPins()` = N−1
- `inputPinId(0)` returns the common port's input pin
- `outputPinId(j)` returns the j-th non-common output

### 2.2 Mode: Combiner

N−1 inputs, one output (the common port).

```
  ┌────────────────────┐
──┤ Port 1             │
  │   Port 2 (Common)  ├──► output (S₂₁×in₁ + S₂₃×in₃)
──┤ Port 3             │
  └────────────────────┘
```

- `numInputPins()` = N−1
- `numOutputPins()` = 1
- `inputPinId(k)` returns the k-th non-common input
- `outputPinId(0)` returns the common port's output pin

### 2.3 Mode: FullMatrix (existing behavior)

N inputs + N outputs, forward-only linear evaluation.

- Kept as-is for diagnostic or bidirectional use
- `numInputPins()` = N, `numOutputPins()` = N

### 2.4 Common Port Configuration

The user selects which physical Touchstone port is the "common" port (default = 1 for most devices, but 2 for MPD-0226CH). This setting only matters in Splitter/Combiner modes.

## 3. Pin Labeling

The existing `GraphNode::input_labels` / `output_labels` vectors are used. Labels depend on mode:

| Mode | Input Labels | Output Labels |
|------|-------------|---------------|
| Splitter | `["Common (Port C)"]` | `["Port 1", "Port 3", ...]` (all non-C) |
| Combiner | `["Port 1", "Port 3", ...]` (all non-C) | `["Common (Port C)"]` |
| FullMatrix | `["Port 1", ..., "Port N"]` | `["Port 1", ..., "Port N"]` |

## 4. Signal Evaluation by Mode

### Splitter

```
output(p) = data.interpolate(freq, p * N + C)
```
where p iterates over the non-common output ports, C = common port index.

### Combiner

```
output(C) = Σ data.interpolate(freq, C * N + p) × input(p)
```
where p iterates over the non-common input ports that are connected.

### FullMatrix

Unchanged from the current implementation: outputⱼ = Σ Sⱼₖ × inputₖ for all connected k.

## 5. Interface Additions

### `IComponentEngine`

```cpp
virtual int numInputPins() const { return 1; }
virtual int numOutputPins() const { return 1; }
```

Default 1/1 preserves backward compat for all existing engines.

### `SParamEngine`

```cpp
enum class Mode { Splitter, Combiner, FullMatrix };

Mode mode() const;
void setMode(Mode mode);     // triggers rebuildNode
int commonPort() const;
void setCommonPort(int p);   // triggers rebuildNode
int numInputPins() const override;
int numOutputPins() const override;
```

## 6. App Routing

The `update_dsp()` loop uses `numInputPins()` instead of hardcoded 1 or `dynamic_cast<SParamEngine*>`:

```cpp
for (auto* comp : m_components.all()) {
    int N = comp->numInputPins();
    for (int k = 0; k < N; ++k) {
        int pid = comp->inputPinId(k);
        if (pid >= 0) {
            auto* source = m_graph_engine.getSourceForInput(pid);
            comp->node().inputs[k] = source ? &source->outputs[0] : nullptr;
        }
    }
    updates[comp->graphNodeId()] = [comp]() { comp->update(0.0); };
}
```

Pin ids for outputs not wired are simply ignored downstream — no change needed.

## 7. Inspector Panel

Replace the "Full Matrix" checkbox with:

```
  Mode: [Splitter ▼]
  Common Port: [2 ▼]      (only when mode ≠ FullMatrix)
  Ports: 3 | Data: 102 pts | Max: 26.5 GHz
  NF/Nonlinearity sections
```

## 8. Files to Touch

| File | Change |
|------|--------|
| `common/component_interface.h` | Add `numInputPins()` / `numOutputPins()` virtuals |
| `s_parametric_component/include/s_param_engine.h` | Add `Mode` enum, mode/port methods, `numInputPins()`/`numOutputPins()` overrides |
| `s_parametric_component/src/s_param_engine.cpp` | Mode-aware `rebuildNode()`, mode-aware `update()` paths |
| `app/src/app.cpp` | Use `numInputPins()` in routing loop |
| `app/src/inspector_panel.cpp` | Mode dropdown + common port selector |

## 9. Testing

- Existing tests: unchanged (mode defaults to FullMatrix → backward compat)
- New tests: splitter mode (1 input → 2 outputs), combiner mode (2 inputs → 1 output), mode switching rebuilds pins
