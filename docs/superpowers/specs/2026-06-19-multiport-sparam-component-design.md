# Multi-Port S-Parameter Component — Design Spec

**Date:** 2026-06-19
**Status:** Draft

## 1. Problem Statement

The current `SParamEngine` handles 1-port and 2-port devices by applying a single S-parameter index as a transfer function from one input to one output. Many real RF components have more than two ports (splitters, combiners, directional couplers, balanced amplifiers), and the Touchstone data files already represent the full N×N matrix. The engine needs to support N-port components with:

- One input pin + one output pin per physical port (Option B from design discussion)
- Full linear matrix evaluation (all Sⱼₖ applied simultaneously based on graph wiring)
- Wiring determines which ports are stimulated (no fixed port-to-role mapping)
- No reflection/mismatch modeling (Sₖₖ contributions to upstream ports are ignored for now)

## 2. Core Model

### 2.1 Pin Arrangement

For an N-port device loaded from an .sNp file:

```
         ┌─────────────────────────────┐
         │  MyDevice.s3p (S-Param 1)   │
         ├─────────────────────────────┤
  ─────  │  Port 1                  │  ─────  Port 1 OUT
  ─────  │  Port 2                  │  ─────  Port 2 OUT
  ─────  │  Port 3                  │  ─────  Port 3 OUT
         └─────────────────────────────┘
```

- Each physical Touchstone port gets one **input pin** (incident wave from upstream) and one **output pin** (transmitted wave to downstream)
- An input pin with nothing wired is Z₀ terminated (incident wave = zero)
- An output pin with nothing downstream is simply ignored
- Wiring determines the component's role: same .s3p acts as a splitter or combiner depending purely on graph connections

### 2.2 Signal Evaluation

For a linear N-port network, each output port j receives:

```
output_j = Σ_k (Sⱼₖ × inputₖ)   for all k where port k has a connected input
```

where Sⱼₖ is the row-major parameter index `j * N + k` interpolated at each frequency of interest.

**Processing per output port j:**

1. **Frequency grid** — use the grid from the first connected input (lowest port index with a signal); if none, build a default grid
2. **Tones (coherent)** — for each connected input port k and each tone in inputₖ:
   - Interpolate Sⱼₖ at the tone's frequency
   - Apply gain (|Sⱼₖ|) and phase rotation (arg Sⱼₖ)
   - Accumulate into output_j's tone list, merging same-frequency tones as complex voltages
3. **Noise (incoherent)** — for each connected input port k and each frequency bin:
   - Interpolate Sⱼₖ at the bin frequency
   - Add `|Sⱼₖ|² × input_noiseₖ` to output_j's noise power (uncorrelated noise adds linearly in power)
4. **Noise figure** — apply NF noise addition per output port (identical to current single-port model)
5. **Nonlinearity** — apply per output port (identical to current model)
6. **Bump generation**

**Reflection handling:** Currently not modeled. Contributions from Sₖₖ back into input port k are ignored — a unilateral approximation.

### 2.3 2-Port Backward Compatibility

When only one input is connected, the multi-port evaluation reduces to the current single-param transfer (e.g., for 2-port: `output₂ = S₂₁ × input₁`). All existing 2-port tests continue to pass unchanged.

A "Forward Param" selector remains in the inspector (default: Full Matrix) for debug/manual override.

## 3. Interfaces

### 3.1 IComponentEngine Changes

```cpp
class IComponentEngine {
    // ... existing pure virtuals ...

    // New multi-pin accessors (default: return -1 for single-pin engines)
    virtual int inputPinId(int port) const { return -1; }
    virtual int outputPinId(int port) const { return -1; }

    // Keep originals for backward compat (return first pin)
    virtual int inputPinId() const { return -1; }
    virtual int outputPinId() const = 0;
};
```

All existing engines are unaffected — they inherit the `return -1` defaults.

### 3.2 GraphNode Changes

```cpp
struct GraphNode {
    int node_id;
    std::vector<int> input_pin_ids;
    std::vector<int> output_pin_ids;
    SignalNode* signal_node;
    std::string label;

    // Optional per-pin labels (empty → "IN"/"OUT" default rendering)
    std::vector<std::string> input_labels;
    std::vector<std::string> output_labels;
};
```

SParamEngine populates these on reload:

```cpp
for (int p = 0; p < N; ++p) {
    node.input_labels[p]  = "Port " + std::to_string(p + 1);
    node.output_labels[p] = "Port " + std::to_string(p + 1);
}
```

### 3.3 SParamEngine Extensions

```cpp
class SParamEngine : public IComponentEngine {
public:
    // ... existing interface unchanged ...

    // New multi-port accessors
    int numPorts() const { return m_data.numPorts(); }
    int inputPinId(int port) const override;
    int outputPinId(int port) const override;

    // Existing singular accessors return port 0
    int inputPinId() const override { return inputPinId(0); }
    int outputPinId() const override { return outputPinId(0); }

    // Reload is now the central lifecycle method
    void reload(const std::string& filepath);  // existing, enhanced

private:
    // ... existing members unchanged ...

    // Rebuilds the graph node with correct pin count
    void rebuildNode();
};
```

## 4. Pin Lifecycle

1. **Constructor** — creates a placeholder graph node with 1 input + 1 output pin, calls `reload()`
2. **`reload(filepath)`** — attempts to load the Touchstone file:
   - On success, determines `N = data.numPorts()`
   - If `N` differs from current pin count or first load: calls `rebuildNode()`
   - `rebuildNode()` removes the old graph node from `NodeGraphEngine`, creates a new one with `N` inputs + `N` outputs, resizes `m_node.inputs[N]` and `m_node.outputs[N]`, and sets pin labels
   - Existing links to the old node are lost (consistent with EDA tools)
3. **Wiring** only meaningful after a file is loaded; the inspector only shows wiring UI when `loaded()` is true

## 5. App Routing Changes

In `RfSimulatorApp::update_dsp()`, the routing loop expands to iterate all input pins:

```cpp
for (auto* comp : m_components.all()) {
    int N = 1;
    if (auto* sp = dynamic_cast<SParamEngine*>(comp))
        N = sp->numPorts();

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

## 6. Node Graph Widget Changes

### 6.1 Pin Label Rendering

In `drawNodes()`, use `GraphNode::input_labels` / `output_labels` when present:

```cpp
for (size_t i = 0; i < node.input_pin_ids.size(); ++i) {
    ImNodes::BeginInputAttribute(node.input_pin_ids[i]);
    const char* label = (i < node.input_labels.size() && !node.input_labels[i].empty())
        ? node.input_labels[i].c_str() : "IN";
    ImGui::Text("%s", label);
    ImNodes::EndInputAttribute();
}
// Same for output pins with "OUT" default
```

### 6.2 Probe Behavior

Unchanged. Ctrl+click on any output pin probes that specific pin. The per-pin probe code already works with any number of pins.

## 7. Inspector Panel Changes

- Keep the "Forward Param" combo but label the default as "Full Matrix" — when selected, the engine uses all N² parameters; when a specific Sⱼₖ is chosen, the engine falls back to the current single-param mode (debug only)
- Show port count, data points, max freq from the loaded file
- Noise Figure and Nonlinearity sections remain unchanged (applied per output port)

## 8. Files to Touch

| File | Change |
|------|--------|
| `common/component_interface.h` | Add `inputPinId(int)` / `outputPinId(int)` virtuals |
| `node_graph/include/node_graph_engine.h` | Add `input_labels` / `output_labels` to `GraphNode` |
| `node_graph/src/node_graph_widget.cpp` | Render pin labels from `GraphNode` |
| `s_parametric_component/include/s_param_engine.h` | Add multi-port API, `rebuildNode()` |
| `s_parametric_component/src/s_param_engine.cpp` | Multi-port `update()`, pin lifecycle, `rebuildNode()` |
| `app/src/app.cpp` | Expand routing loop for N-port engines |
| `tests/test_s_parameter_amplifier.cpp` | Add multi-port tests using `MPD-0226CH_CH_25C_F.s3p` |

## 9. Testing

### Multi-port tests (new)

1. **3-port splitter default behavior** — load the MPD-0226CH .s3p, verify `numPorts() == 3`, verify pin labels match port numbers
2. **Single-input splitter** — wire one generator to Port 2 input (the COMMON port), verify both Port 1 and Port 3 outputs have ~-3.6 dB gain relative to input (the split ratio from the data)
3. **Combiner mode** — wire two generators to Port 1 and Port 3 inputs, verify Port 2 output is the sum
4. **Matrix multiplication correctness** — for a known frequency, manually verify the accumulated output matches the S-matrix equation
5. **Unconnected ports** — verify that unwired input pins produce zero contribution to outputs
6. **Backward compatibility** — all existing 2-port tests pass unchanged (the multi-port path degrades gracefully when only one input is connected)

### Existing tests

All existing `SParamEngine` tests (single-port forward transfer, NF, nonlinearity, reload, bad file) must pass without modification.

## 10. Out of Scope (Future)

- Full reflection / mismatch modeling (Sₖₖ feeding back to upstream ports)
- Port impedance normalization (all ports assumed 50 Ω)
- Mixed-mode S-parameters (diff/common mode for differential ports)
- Noise figure / nonlinearity per individual port (global NF applied uniformly for now)
