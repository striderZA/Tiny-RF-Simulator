# S-Parameter Component Rework

**Date:** 2026-07-06
**Status:** Draft

## Goal

Replace the generic, multi-mode `SParamEngine` with per-component S-parameter support. Each RF component (amplifier, ideal filter, equalizer, etc.) operates in either **ideal mode** (its current behavior) or **S-parameter mode** (loaded from a .sNp file). The generic one-size-fits-all `SParamEngine` with Splitter/Combiner/FullMatrix modes is removed.

## What Goes Away

- **`s_parametric_component/`** — entire directory (CMakeLists.txt, `s_param_engine.h`, `s_param_engine.cpp`)
- **`NodeKind::SParam`** — removed from `node_graph_engine.h` enum, `themeColor()`, and `nodeKindFromLabel()`
- **S-Param symbol** — removed from `node_graph_widget.cpp` (`drawSParamSymbol`, case in `drawSchematicSymbol`)
- **"Add S-Param Component"** — removed from context menu in `node_graph_widget.cpp` and its `onAddSParamComponent` callback
- **`InspectorPanel::drawSParamProperties()`** — removed, along with `ComponentType::SParam`
- **`add_subdirectory("s_parametric_component")`** — removed from root `CMakeLists.txt`
- **`#include "s_param_engine.h"`** — removed from `app.h`, `inspector_panel.h`

### What Stays

- **`touchstone/s_parameter_data.h/.cpp`** — untouched. `SParameterData` is the the right reusable data model for loading, storing, and interpolating S-parameter touchstone files. Each engine uses an `SParameterData` member.

## Per-Component S-Parameter Mode Pattern

### Member additions (each engine):

```cpp
#include "s_parameter_data.h"

// In class body:
SParameterData m_sparam_data;
std::string m_sparam_filepath;
bool m_sparam_mode = false;
int m_sparam_fwd_idx = 0;    // 1*N+0 = S21 for N-port
const Spectrum* m_cached_sparam_input = nullptr;
uint64_t m_cached_sparam_generation = 0;
```

### Methods:

```cpp
void setSParamFilepath(const std::string& path) {
    m_sparam_filepath = path;
    m_sparam_mode = m_sparam_data.load(path);
    if (m_sparam_data.loaded())
        m_sparam_fwd_idx = 1 * m_sparam_data.numPorts() + 0; // S21
    m_dirty = true;
}

bool sparamMode() const { return m_sparam_mode; }
void setSParamMode(bool en) { m_sparam_mode = en; m_dirty = true; }
bool sparamLoaded() const { return m_sparam_data.loaded(); }
const std::string& sparamFilepath() const { return m_sparam_filepath; }
```

### `update()` branching pattern:

```cpp
if (m_sparam_mode && m_sparam_data.loaded()) {
    // Frequency grid from input or build default
    if (in_ptr && !in_ptr->frequencies.empty())
        out.frequencies = in_ptr->frequencies;
    else
        buildDefaultFrequencyGrid(out.frequencies);

    // Apply S21 complex gain to tones
    int idx = m_sparam_fwd_idx;
    for (auto& t : out.tones) {
        auto S = m_sparam_data.interpolate(t.freq_Hz, idx);
        t.power_dBm += 20.0 * log10(std::abs(S));
        t.phase_deg += std::arg(S) * 180.0 / std::numbers::pi;
    }

    // Apply S21 power gain to noise
    for (size_t i = 0; i < N; ++i) {
        auto S = m_sparam_data.interpolate(out.frequencies[i], idx);
        double g = std::norm(S);
        out.noise_W[i] = g * input_noise[i];
    }

    // Component-specific properties still apply:
    //   Amplifier: NF, nonlinearity
    //   Filter, Equalizer: none
}
```

### Design principles:
- NF and nonlinearity stay per-component — they are physical device properties, not derivable from S-parameters
- The S-parameter data provides only the linear transfer function (complex gain vs frequency)
- Forward param is always S21 (index `1*N+0`). If a different S-param is needed, load a different file

## AmplifierEngine Changes

- Add fields above to `amplifier_engine.h`
- In `update()`: when `m_sparam_mode && m_sparam_data.loaded()`, use S21 interpolation instead of `m_gain_dB`
- NF (`m_nf_dB`) and nonlinearity (`m_nonlinear`) still apply in both modes
- Caching: separate cache pointer/generation for S-param path
- **Inspector panel**: Add mode toggle (combo: Ideal / S-Parameter). When S-Parameter is selected, show file browser + path, hide gain control (or gray it out). NF and nonlinearity controls remain visible.

## IdealFilterEngine Changes

- Add fields above to `ideal_filter_engine.h`
- In `update()`: when `m_sparam_mode && m_sparam_data.loaded()`, use S21 interpolation instead of passband check
- Noise follows S21 power gain (no added noise in either mode for a passive filter)
- **Inspector panel**: Add mode toggle. When S-Parameter, show file browser, hide cutoff/type controls.

## Equalizer — New Component

### Ideal mode
A frequency-dependent gain profile modeled as:

`update()` applies the ideal gain to both tones and noise power per bin. No phase rotation, no added noise, no nonlinearity.

Gain formula:

```
G(f) = G_ref + slope * log10(f / f_ref)   [dB]
```

```
G(f) = G_ref + slope * log10(f / f_ref)   [dB]
```

Where:
- `G_ref` = reference gain at reference frequency (dB)
- `f_ref` = reference frequency (Hz)
- `slope` = gain slope (dB/decade, positive = rising with frequency)

### S-param mode
Load an .sNp file and apply S21 as the gain profile — identical to the amp/filter pattern.

### Structure:
- `equalizer/include/equalizer_engine.h`
- `equalizer/src/equalizer_engine.cpp`
- `equalizer/CMakeLists.txt`
- 1 input, 1 output (single-pin)
- No NF, no nonlinearity
- Node editor: "Add Equalizer" context menu entry, `NodeKind::Equalizer`, drawn with a tilting gain curve symbol
- Inspector panel: mode toggle, file browser in S-param mode, slope/ref controls in ideal mode

### Wiring (app.cpp):
```cpp
m_graph_widget->onAddEqualizer = [this]() {
    m_components.add<EqualizerEngine>(m_next_component_id++, m_graph_engine);
};
```

## Updated File List

### Deleted:
| Path | Reason |
|---|---|
| `s_parametric_component/CMakeLists.txt` | Component removed |
| `s_parametric_component/include/s_param_engine.h` | Component removed |
| `s_parametric_component/src/s_param_engine.cpp` | Component removed |

### New:
| Path | Description |
|---|---|
| `equalizer/CMakeLists.txt` | Equalizer library target |
| `equalizer/include/equalizer_engine.h` | Equalizer engine header |
| `equalizer/src/equalizer_engine.cpp` | Equalizer engine implementation |

### Modified:
| Path | Changes |
|---|---|
| `CMakeLists.txt` | Remove s_parametric_component, add equalizer |
| `node_graph/include/node_graph_engine.h` | Remove `NodeKind::SParam`, add `NodeKind::Equalizer`, update `themeColor()` and `nodeKindFromLabel()` |
| `node_graph/include/node_graph_widget.h` | Remove `onAddSParamComponent`, add `onAddEqualizer` |
| `node_graph/src/node_graph_widget.cpp` | Remove S-Param context menu + symbol, add Equalizer menu + symbol |
| `common/component_interface.h` | No changes needed |
| `amplifier/include/amplifier_engine.h` | Add S-param mode fields and methods |
| `amplifier/src/amplifier_engine.cpp` | Add S-param branch in `update()` |
| `ideal_filter/include/ideal_filter_engine.h` | Add S-param mode fields and methods |
| `ideal_filter/src/ideal_filter_engine.cpp` | Add S-param branch in `update()` |
| `app/include/app.h` | Remove `s_param_engine.h` include |
| `app/src/app.cpp` | Remove `onAddSParamComponent` wiring, add `onAddEqualizer` wiring |
| `app/include/inspector_panel.h` | Remove `SParam` type, add `Equalizer` type, add `drawEqualizerProperties()` |
| `app/src/inspector_panel.cpp` | Remove `drawSParamProperties()`, add `drawEqualizerProperties()`, add amp/filter S-param UI |
| `tests/test_s_parameter_amplifier.cpp` | Migrate to `tests/test_amplifier_sparam.cpp` testing `AmplifierEngine` in S-param mode |
| `tests/CMakeLists.txt` | Remove old test, add new test |

## Testing Strategy

1. **Amplifier s-param tests**: Migrate existing `test_s_parameter_amplifier.cpp` tests to test `AmplifierEngine` in S-param mode. Test: loading, interpolation, tone gain, NF, nonlinearity.
2. **Filter s-param tests**: Test `IdealFilterEngine` in S-param mode — S21 applied correctly.
3. **Equalizer tests**: Test ideal mode (slope at known freq points), test S-param mode.
4. **Sanity check**: Existing amp/filter ideal-mode tests must still pass.
