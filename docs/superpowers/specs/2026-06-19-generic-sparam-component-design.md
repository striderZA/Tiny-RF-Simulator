# Generic S-Parameter Component — Design Spec

**Date:** 2026-06-19
**Author:** Brainstorming session
**Status:** Draft

## 1. Problem Statement

The simulator currently has two near-identical S-parameter-based components:

- **`SParameterFilterEngine`** — applies S-parameter data (magnitude + phase) to an input spectrum. Passive-only, no noise or nonlinearity.
- **`SParameterAmplifierEngine`** — applies S-parameter data plus optional noise figure and nonlinearity (OIP2/OIP3).

There is no technical reason for this split. Both process S-parameters identically; the amplifier merely adds two optional behaviors. This duplication adds maintenance burden, confuses users ("which one do I pick?"), and each new S-parameter feature requires touching two files.

## 2. Design

### 2.1 Engine

A single `SParamEngine` class replaces both existing engines. It implements `IComponentEngine`.

```cpp
class SParamEngine : public IComponentEngine {
  public:
    SParamEngine(int id, NodeGraphEngine& graph, const std::string& filepath);

    // IComponentEngine
    int id() const override;
    int graphNodeId() const override;
    int inputPinId() const override;
    int outputPinId() const override;
    std::string hoverSummary() const override;
    SignalNode& node() override;
    const SignalNode& node() const override;
    void update(double dt) override;

    void reload(const std::string& filepath);

    // S-parameter access
    const std::string& filepath() const;
    bool loaded() const;
    const SParameterData& data() const;
    int forwardParamIdx() const;
    void setForwardParamIdx(int idx);

    // Optional: noise figure (0.0 = off/passive)
    double nf_dB() const;
    void setNF_dB(double nf);

    // Optional: nonlinearity (disabled by default)
    bool enableNonlinear() const;
    double oip2_dBm() const;
    double oip3_dBm() const;
    void setEnableNonlinear(bool en);
    void setOIP2_dBm(double oip2);
    void setOIP3_dBm(double oip3);

  private:
    int m_id;
    int m_graph_node_id = -1;
    NodeGraphEngine* m_graph = nullptr;
    SignalNode m_node;
    SParameterData m_data;
    std::string m_filepath;
    int m_forward_param_idx = 0;
    bool m_dirty = true;
    const Spectrum* m_cached_input_ptr = nullptr;
    uint64_t m_cached_input_generation = 0;

    double m_nf_dB = 0.0;
    NonlinearModel m_nonlinear;
};
```

**Processing logic in `update()`:**

1. Cache check: skip if input unchanged (same pointer + generation), identical to current logic.
2. Apply S-parameters via `m_data.applyToSpectrum(in, out, m_forward_param_idx)` — tone magnitude/phase + noise scaling.
3. If NF > 0.0: compute added noise density `k * Te * |S|^2` per bin, add to `noise_total_W`.
4. If nonlinear enabled: process tones through `NonlinearModel`, add IM products, apply gain compression.
5. Bump output generation.

**Forward param default:** `m_forward_param_idx = (numPorts > 1) ? numPorts : 0` (S21 for 2-port, S11 for 1-port).

### 2.2 Module Structure

```
s_parametric_component/
├── CMakeLists.txt
├── include/
│   └── s_param_engine.h
└── src/
    └── s_param_engine.cpp
```

**CMake target:** `simulator::s_param_engine`

```cmake
target_link_libraries(s_param_engine
    PUBLIC common simulator::node_graph_engine simulator::touchstone_parser
)
```

**Deleted:**
- `s_parameter_filter/` (entire directory)
- `s_parameter_amplifier/` (entire directory)

### 2.3 Node Graph & Wiring

**`NodeGraphWidget` callbacks:**
- Remove `onAddSParamFilter` and `onAddSParamAmp`
- Add single `onAddSParamComponent`

**Context menu:** Single entry "Add S-Parameter Component".

**`RfSimulatorApp` constructor:**
```cpp
m_graph_widget->onAddSParamComponent = [this]() {
    m_components.add<SParamEngine>(m_next_component_id++, m_graph_engine, "");
};
```

The component starts with no Touchstone file loaded (empty string). The user browses from the inspector panel.

### 2.4 Inspector Panel

A single unified `drawSParamProperties()` replaces both old property panels.

**Layout (top to bottom):**

1. **File section** — path text, [Browse...] button with file dialog
2. **S-parameter info** — forward param combo (S11/S21/...), port count, data points, max freq
3. **Noise Figure** — checkbox (default unchecked = passive), NF (dB) input (only when checked)
4. **Nonlinearity** — checkbox (default unchecked), OIP2/OIP3 inputs (only when checked), estimated P1dB display
5. **Delete** button

**`InspectorPanel::findSelected()` changes:**
- Remove `ComponentType::SParamFilter` and `ComponentType::SParamAmp`
- Add single `ComponentType::SParam`
- Remove `dynamic_cast` for both old types, add `dynamic_cast<SParamEngine*>`

**`InspectorPanel::labelForHit()` changes:**
- Remove both old S-param label cases
- Add single case returning `"S-Param " + id`

### 2.5 `ComponentRegistry` Integration

No changes needed. The `add<SParamEngine>(...)` template works as-is. The type index `std::type_index(typeid(SParamEngine))` automatically replaces entries for both old types.

## 3. Files to Touch

| File | Change |
|------|--------|
| `s_parametric_component/CMakeLists.txt` | **Create** — new module |
| `s_parametric_component/include/s_param_engine.h` | **Create** — unified engine header |
| `s_parametric_component/src/s_param_engine.cpp` | **Create** — unified engine impl |
| `s_parameter_filter/CMakeLists.txt` | **Delete** |
| `s_parameter_filter/include/s_parameter_filter_engine.h` | **Delete** |
| `s_parameter_filter/src/s_parameter_filter_engine.cpp` | **Delete** |
| `s_parameter_amplifier/CMakeLists.txt` | **Delete** |
| `s_parameter_amplifier/include/s_parameter_amplifier_engine.h` | **Delete** |
| `s_parameter_amplifier/src/s_parameter_amplifier_engine.cpp` | **Delete** |
| `CMakeLists.txt` (root) | Remove old subdirectories, add `s_parametric_component` |
| `app/include/app.h` | Update includes and component references |
| `app/src/app.cpp` | Replace callbacks, replace `add<SParameterFilter/Amplifier>` with `add<SParamEngine>` |
| `app/include/inspector_panel.h` | Update `ComponentType` enum, method declarations |
| `app/src/inspector_panel.cpp` | Replace `findSelected`, `labelForHit`, draw methods |
| `node_graph/include/node_graph_widget.h` | Replace callbacks |
| `node_graph/src/node_graph_widget.cpp` | Update context menu |

## 4. Migration

There is no session save/load system for the node graph, so no migration is needed. Old sessions simply lose references to the removed component types (the graph is created fresh each launch).

## 5. Testing

- Existing tests for `SParameterData` / touchstone parser are unaffected.
- The behavioral contract of `SParamEngine::update()` is identical to the two replaced engines combined → existing engine-level behavior is preserved.
- New tests should verify:
  - Default state is passive (no NF, no nonlinear)
  - NF toggle works (noise added when enabled)
  - Nonlinearity toggle works
  - Forward param selection matches old behavior
