# Attenuator Component Design

> **Status:** Draft
> **Date:** 2026-07-14
> **Scope:** Minimal passive attenuator with manual dB control and optional S-parameter mode

## Summary

Add a passive attenuator component to the RF Simulator. It provides adjustable signal attenuation with physically accurate noise modeling (NF = attenuation) and optional frequency-dependent behavior via Touchstone S-parameter files.

This is the smallest useful attenuator implementation — a single component following the existing dual-mode pattern (manual + S-param), matching Amplifier, IdealFilter, and Equalizer.

## Motivation

Every RF signal chain needs attenuation for level setting, isolation, and impedance matching. The simulator currently has no attenuator component despite having `component_data/fixed_attenuators/` and `component_data/step_attenuators/` directories with real part data. Adding this component unblocks basic RF chain construction.

## Design Decisions

### Scope

**Single unified Attenuator component** with two modes:
1. **Manual mode:** User specifies attenuation in dB
2. **S-param mode:** User loads a `.sNp` Touchstone file, S21 magnitude/phase applied frequency-dependently

**Not in scope:**
- Step attenuator with discrete digital control (ADRF5703-style) — can be added later as a variant
- Variable attenuator with separate NF parameter — physically incorrect for passive components

### Noise Model

**Physically accurate passive noise:**
- Noise figure equals attenuation: `NF_dB = atten_dB`
- Added noise: `N_added = k * T * (1 - G)` where `G = 10^(-atten_dB/10)` is linear gain (< 1)
- Output noise: `noise_total_W = noise_W * G + k * T * (1 - G)`

**Rationale:**
- Passive attenuators at thermal equilibrium have NF = loss by thermodynamic necessity
- Simpler than manual NF parameter (one fewer knob)
- Matches coax cable behavior (also passive, also NF = loss)
- At infinite attenuation, output noise converges to k*T (thermal noise floor at 290K)

**S-param mode noise:**
- Same formula, but G becomes frequency-dependent: `G(f) = |S21(f)|²`
- Output noise: `noise_total_W[i] = noise_W[i] * |S21(f_i)|² + k * T * (1 - |S21(f_i)|²)`

### Architecture

**Follow the existing engine/widget pattern:**
- `AttenuatorEngine` inherits `IComponentEngine`
- 1 input pin, 1 output pin
- Dirty-flag caching (skip update if input unchanged)
- Properties edited via `InspectorPanel` (no dedicated widget)
- New `NodeKind::Attenuator` in node graph

**No shared base class** — YAGNI. The attenuator is ~150 lines; extracting a "passive component" base with coax would add coupling for minimal gain.

## Engine Class

### Header: `attenuator/include/attenuator_engine.h`

```cpp
class AttenuatorEngine : public IComponentEngine {
public:
    AttenuatorEngine(int id, NodeGraphEngine& graph);
    
    // IComponentEngine interface
    int id() const override;
    int graphNodeId() const override;
    std::string hoverSummary() const override;
    int inputPinId() const override;
    int outputPinId() const override;
    void update(double dt) override;
    SignalNode& node() override;
    
    // Parameters
    void setAttenuation(double dB);          // 0.0 to 200.0 dB
    double attenuation() const;
    void setSParamMode(bool enabled);
    bool sParamMode() const;
    void setSParamFile(const std::string& path);
    const std::string& sParamFile() const;

private:
    int m_id;
    int m_graph_node_id = -1;
    NodeGraphEngine* m_graph = nullptr;
    SignalNode m_node;
    
    // Manual mode
    double m_atten_dB = 0.0;             // Attenuation value, clamped [0, 200]
    
    // S-param mode
    bool m_sparam_mode = false;
    std::string m_sparam_path;
    SParameterData m_sparam;             // Loaded Touchstone data
    
    // Dirty-flag cache
    bool m_dirty = true;
    const Spectrum* m_cached_input_ptr = nullptr;
    uint64_t m_cached_input_generation = 0;
};
```

### DSP Logic: `update(double dt)`

**Dirty-flag check:**
```cpp
if (!m_dirty && m_cached_input_ptr == &input &&
    m_cached_input_generation == input.generation) {
    return; // No change — skip
}
m_cached_input_ptr = &input;
m_cached_input_generation = input.generation;
m_dirty = false;
```

**Manual mode** (`m_sparam_mode == false`):

For each tone:
```cpp
output_tone.power_dBm = input_tone.power_dBm - m_atten_dB;
output_tone.phase_deg = input_tone.phase_deg; // Phase unchanged
```

For noise (per-bin):
```cpp
double G_linear = std::pow(10.0, -m_atten_dB / 10.0);
output.noise_total_W[i] = input.noise_W[i] * G_linear + k * T * (1.0 - G_linear);
```

**S-param mode** (`m_sparam_mode == true`):

For each tone at frequency `f`:
```cpp
auto S21 = m_sparam.interpolate(f); // complex<double>
double gain_dB = 20.0 * std::log10(std::abs(S21));
output_tone.power_dBm = input_tone.power_dBm + gain_dB; // gain_dB is negative for attenuator
output_tone.phase_deg = input_tone.phase_deg + std::arg(S21) * 180.0 / M_PI;
```

For noise (per-bin at frequency `f_i`):
```cpp
auto S21 = m_sparam.interpolate(f_i);
double mag_sq = std::norm(S21); // |S21|²
output.noise_total_W[i] = input.noise_W[i] * mag_sq + k * T * (1.0 - mag_sq);
```

### Constants

- `k = 1.3806e-23` (Boltzmann constant, from `common/common.h`)
- `T = 290.0` (Reference temperature in Kelvin, from `common/common.h`)
- Attenuation clamped to [0, 200] dB
- `m_atten_dB` default = 0.0 (pass-through)

## Node Graph Integration

### NodeKind

Add `Attenuator` to `NodeKind` enum in `node_graph/include/node_graph_engine.h`:
```cpp
enum class NodeKind {
    SignalGenerator,
    Amplifier,
    Splitter,
    Mixer,
    CoaxCable,
    IdealFilter,
    Equalizer,
    Adc,
    PfbChannelizer,
    Attenuator  // NEW
};
```

**Theme color:** Muted green/teal (passive component family) — suggest `IM_COL32(100, 180, 140, 255)`

### Right-Click Context Menu

In `node_graph/src/node_graph_widget.cpp`, add "Attenuator" entry alongside existing components.

### Schematic Symbol

Add case in `drawSchematicSymbol` for `NodeKind::Attenuator`:
- **Symbol:** Zigzag line (resistor-style) between input and output pins
- **Label:** "ATT" centered above the zigzag
- **Rationale:** Standard RF schematic symbol for attenuator, visually distinct from amplifier (triangle) and filter (box)

## Inspector Panel Integration

In `app/src/inspector_panel.cpp`, add attenuator section:

```cpp
if (auto* atten = dynamic_cast<AttenuatorEngine*>(engine)) {
    ImGui::SeparatorText("Attenuator");
    
    double atten_dB = atten->attenuation();
    if (ImGui::DragScalar("Atten (dB)", ImGuiDataType_Double, &atten_dB, 0.1f, &zero, &max_atten)) {
        atten->setAttenuation(atten_dB);
    }
    
    bool sparam_mode = atten->sParamMode();
    if (ImGui::Checkbox("S-param mode", &sparam_mode)) {
        atten->setSParamMode(sparam_mode);
    }
    
    if (sparam_mode) {
        std::string path = atten->sParamFile();
        if (ImGui::InputText("S-param file", ...)) {
            atten->setSParamFile(path);
        }
        if (ImGui::Button("Browse")) {
            // Open file dialog, call atten->setSParamFile(...)
        }
    }
    
    // Read-only display
    ImGui::Text("NF = %.2f dB", atten->attenuation());
}
```

**UI elements:**
- `Atten (dB)` — DragFloat, range [0, 200], step 0.1
- `S-param mode` — checkbox toggle
- When S-param enabled: file path input + "Browse" button
- Read-only: `NF = X dB` (mirrors attenuation, since NF = atten for passive)

## Component Registry & App Wiring

### Registration

In `app/src/app.cpp`, register `AttenuatorEngine` in the component factory:
```cpp
registry.add<AttenuatorEngine>(next_id++, *m_graph);
```

### Update Loop

No changes to `update_dsp()` — topological sort handles it automatically once the engine is registered.

### Save/Load

Serialize to JSON in existing save/load system:
```json
{
  "type": "Attenuator",
  "atten_dB": 6.0,
  "sparam_mode": false,
  "sparam_path": ""
}
```

## Tests

### File: `tests/test_attenuator.cpp`

**Test cases:**

1. **Pass-through at 0 dB** — Input tones unchanged, noise unchanged
2. **Flat attenuation** — 6 dB atten: all tone powers reduced by 6 dB, noise PSD reduced by 6 dB
3. **Passive noise model** — Verify `noise_total_W = noise_W * G + k*T*(1-G)` for known attenuation
4. **Noise floor convergence** — At 100 dB attenuation, output noise ≈ k*T (thermal noise floor)
5. **S-param mode** — Load .s2p file, verify frequency-dependent gain matches |S21|
6. **S-param noise** — Verify passive noise formula with |S21|²
7. **S-param phase** — Verify S21 phase is applied to tone phases
8. **Clamping** — Attenuation clamped to [0, 200] dB
9. **Dirty-flag skip** — Second update with same input skips recomputation

### Test infrastructure

- Use existing `TEST_CASE` macros from Catch2
- Reuse `buildTestSpectrum()` helper from `tests/test_main.cpp`
- Load real .s2p file from `component_data/fixed_attenuators/atn01-0040psm/ATN01-0040PSM_SM_25C_De.s2p` for S-param tests

## Build System

### File: `attenuator/CMakeLists.txt`

```cmake
add_library(attenuator_engine
    src/attenuator_engine.cpp
)

target_include_directories(attenuator_engine PUBLIC include)
target_link_libraries(attenuator_engine
    PUBLIC simulator::common
    PUBLIC simulator::touchstone
    PUBLIC simulator::node_graph_engine
)

add_library(simulator::attenuator_engine ALIAS attenuator_engine)
```

### Root `CMakeLists.txt`

Add:
```cmake
add_subdirectory(attenuator)
```

### `app/CMakeLists.txt`

Link `simulator::attenuator_engine` to the app target.

### `tests/CMakeLists.txt`

Add:
```cmake
add_executable(test_attenuator test_attenuator.cpp)
target_link_libraries(test_attenuator PRIVATE
    simulator::attenuator_engine
    Catch2::Catch2WithMain
)
add_test(NAME test_attenuator COMMAND test_attenuator)
```

## File Structure

```
attenuator/
├── include/
│   └── attenuator_engine.h
├── src/
│   └── attenuator_engine.cpp
└── CMakeLists.txt
```

**Total estimated lines:** ~250 (header ~50, source ~150, CMake ~20, tests ~200)

## Acceptance Criteria

- [ ] `AttenuatorEngine` compiles and links
- [ ] Can be added from right-click menu in node graph
- [ ] Manual mode: attenuation reduces tone powers and noise by correct dB amount
- [ ] Manual mode: noise model is physically accurate (NF = atten)
- [ ] S-param mode: loads .s2p file, applies frequency-dependent |S21| gain
- [ ] S-param mode: applies S21 phase shift
- [ ] S-param mode: noise uses |S21|² formula
- [ ] Inspector panel shows Atten slider, S-param toggle, file browser
- [ ] Schematic symbol renders as zigzag with "ATT" label
- [ ] Save/load preserves attenuator state
- [ ] All 9 unit tests pass
- [ ] Dirty-flag caching works (no unnecessary recomputation)

## Future Extensions (Out of Scope)

- **Step attenuator variant** — ADRF5703-style with digital control bits, 0.5 dB steps
- **Variable attenuator with separate NF** — For modeling active/non-ideal attenuators
- **Temperature parameter** — Allow non-290K noise calculation
- **Bidirectional mode** — Model reverse isolation / return loss (S11)
- **Shared passive base class** — If 3+ passive components exist, extract common noise model

## References

- [Amplifier nonlinear model](../../docs/resources/amplifier_nonlinear_model.md) — Similar dual-mode pattern
- [Touchstone parser spec](../../docs/resources/touchstone_v2_parser_spec.md) — S-parameter file format
- [RF passive component noise theory](https://www.microwaves101.com/encyclopedia/noise_figure) — NF = loss for passive devices
