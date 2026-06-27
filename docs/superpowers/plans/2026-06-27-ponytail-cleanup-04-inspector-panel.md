# Ponytail Cleanup 4 — InspectorPanel Collapse Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the 9 per-type `drawXxxProperties()` methods in `InspectorPanel` (each ~40-70 lines of ImGui widgets) with a single generic loop driven by a parameter descriptor per engine. Net ~300 lines saved from `inspector_panel.cpp`. The engines each gain a small `std::vector<PropertyDesc> properties() const;` method that returns their parameter list.

**Architecture:** Add a `struct PropertyDesc` (a tagged union over Double/Int/Bool/Enum + a `drawProperty()` free function) in `common/`. Each engine header adds a `properties()` method that returns the list of descriptors for its parameters. `InspectorPanel` selects the engine, calls `properties()`, and iterates them through a single draw loop. Special-case widgets (PFB channel selector, coax preset dropdown, s-param file path) get explicit `Kind` tags or extended kinds.

**Tech Stack:** C++20, Catch2 v3.4.0, ImGui. No new dependencies.

**Specification:** `docs/superpowers/specs/2026-06-27-ponytail-cleanup-design.md` §7.

## Global Constraints

- **C++20** standard.
- **Code style**: 4-space indent, 100-col width, `PointerAlignment: Right` per `.clang-format`. Run `clang-format -i <file>` before committing.
- **No behavior changes** — every existing label, range, format, and tooltip must be preserved.
- **Side-by-side visual verification** is required: take a screenshot of the inspector for each engine type before and after, compare.
- **Branch from `master`**: `refactor/ponytail-cleanup-04-inspector-panel`.

## File Structure

```
common/property_grid.h                    # new — PropertyDesc + drawProperty
common/CMakeLists.txt                     # (header-only, no change needed)

amplifier/include/amplifier_engine.h      # modified — gains properties() method
adc/include/adc_engine.h                  # modified
coax/include/coax_cable_engine.h          # modified
mixer/include/mixer_engine.h              # modified
splitter/include/splitter_engine.h        # modified
ideal_filter/include/ideal_filter_engine.h # modified
pfb_channelizer/include/pfb_channelizer_engine.h # modified
s_parametric_component/include/s_param_engine.h  # modified
signal_generator/include/signal_generator_engine.h # modified

app/include/inspector_panel.h             # modified — 9 draw methods replaced by drawProperties()
app/src/inspector_panel.cpp               # modified — ~500 lines deleted, ~30 lines added
```

---

## Task 1: Add `PropertyDesc` + `drawProperty` to common

**Files:**
- Create: `common/property_grid.h`

**Interfaces:**
- Consumes: `<imgui.h>` (for `ImGui::InputDouble`, `ImGui::SliderInt`, etc.).
- Produces: `struct PropertyDesc` with `Kind` enum and a tagged union of `double*` / `int*` / `bool*` / choice items. Plus `void drawProperty(const PropertyDesc&);`.

- [ ] **Step 1: Define `PropertyDesc`**

Create `common/property_grid.h`:

```cpp
#pragma once
#include "imgui.h"
#include <cstring>

struct PropertyDesc {
    enum class Kind { Double, Int, Bool, Enum };

    const char* label;
    Kind kind;

    union {
        struct { double* ptr; double min; double max; const char* fmt; } dbl;
        struct { int* ptr; int min; int max; } integer;
        struct { bool* ptr; } boolean;
        struct { int* ptr; const char* const* items; int n; } choice;
    };

    static PropertyDesc Double(const char* label, double* p, double min, double max,
                               const char* fmt = "%.6g") {
        PropertyDesc d;
        d.label = label;
        d.kind = Kind::Double;
        d.dbl.ptr = p;
        d.dbl.min = min;
        d.dbl.max = max;
        d.dbl.fmt = fmt;
        return d;
    }
    // Similar static factories for Int, Bool, Enum ...
};

inline void drawProperty(const PropertyDesc& d) {
    switch (d.kind) {
        case PropertyDesc::Kind::Double: {
            double v = *d.dbl.ptr;
            if (ImGui::InputDouble(d.label, &v, 0.0, 0.0, d.dbl.fmt)) {
                v = std::clamp(v, d.dbl.min, d.dbl.max);
                *d.dbl.ptr = v;
            }
            break;
        }
        case PropertyDesc::Kind::Int: {
            int v = *d.integer.ptr;
            if (ImGui::InputInt(d.label, &v)) {
                v = std::clamp(v, d.integer.min, d.integer.max);
                *d.integer.ptr = v;
            }
            break;
        }
        case PropertyDesc::Kind::Bool:
            ImGui::Checkbox(d.label, d.boolean.ptr);
            break;
        case PropertyDesc::Kind::Enum:
            ImGui::Combo(d.label, d.choice.ptr, d.choice.items, d.choice.n);
            break;
    }
}
```

Add the missing static factories (Int, Bool, Enum) following the same pattern as `Double`.

- [ ] **Step 2: Build to verify the header compiles**

```bash
cmake --build build
```

Expected: BUILD SUCCEEDED. (No engine uses `PropertyDesc` yet.)

- [ ] **Step 3: Commit**

```bash
git add common/property_grid.h
git commit -m "refactor: add PropertyDesc and drawProperty for InspectorPanel"
```

---

## Task 2: Add `properties()` method to amplifier

**Files:**
- Modify: `amplifier/include/amplifier_engine.h`

**Interfaces:**
- Consumes: `IComponentEngine` and the `PropertyDesc` struct.
- Produces: `std::vector<PropertyDesc> properties() const;` returning the gain, NF, OIP2 (if nonlinear), OIP3 (if nonlinear).

- [ ] **Step 1: Add the method declaration**

In `amplifier_engine.h`, add:

```cpp
#include "property_grid.h"
// ...
std::vector<PropertyDesc> properties() const {
    std::vector<PropertyDesc> out;
    out.push_back(PropertyDesc::Double("Gain (dB)", const_cast<double*>(&m_gain_dB), -100.0, 100.0));
    out.push_back(PropertyDesc::Double("Noise Figure (dB)", const_cast<double*>(&m_nf_dB), 0.0, 30.0));
    if (m_nonlinear.enabled()) {
        out.push_back(PropertyDesc::Double("OIP2 (dBm)", const_cast<double*>(&m_oip2_dBm), 0.0, 100.0));
        out.push_back(PropertyDesc::Double("OIP3 (dBm)", const_cast<double*>(&m_oip3_dBm), 0.0, 100.0));
    }
    return out;
}
```

Note: `OIP2` and `OIP3` are managed by `NonlinearModel` — add public accessors `setOIP2_dBm(double)` / `oip2_dBm()` already exist. But the property descriptor needs a direct `double*`. Either:
- Add a `double*` field to `NonlinearModel` that the descriptor points to, OR
- Use a `double` local copy in `properties()` and a setter callback (overkill).

Pick the first: add `double* oip2_ptr() { return &m_oip2_dBm; }` and `double* oip3_ptr() { return &m_oip3_dBm; }` to `NonlinearModel`. The setter flips the dirty flag.

- [ ] **Step 2: Build and test**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: 72 tests pass.

- [ ] **Step 3: Commit**

```bash
git add amplifier/ common/
git commit -m "refactor(amplifier): add properties() method"
```

---

## Task 3: Add `properties()` to remaining 7 engines

**Files:** each engine header

For each engine, follow the same pattern as Task 2: add `#include "property_grid.h"`, add a `properties()` method that returns the descriptors for the parameters the inspector currently draws.

- [ ] **Step 1: AdcEngine**

```cpp
// adc_engine.h
std::vector<PropertyDesc> properties() const {
    return {
        PropertyDesc::Double("Sample rate (Hz)", const_cast<double*>(&m_fs_Hz), 1e3, 100e9),
        PropertyDesc::Double("NSD (dBm/Hz)", const_cast<double*>(&m_nsd_dBm_per_Hz), -200.0, 0.0),
        PropertyDesc::Int("Bits", const_cast<int*>(&m_bits), 1, 24),
        PropertyDesc::Double("V_fs (V)", const_cast<double*>(&m_v_fs), 0.1, 10.0),
    };
}
```

- [ ] **Step 2: CoaxCableEngine**

```cpp
// coax_cable_engine.h
std::vector<PropertyDesc> properties() const {
    return {
        PropertyDesc::Double("Length (m)", const_cast<double*>(&m_length_m), 0.0, 1000.0),
        PropertyDesc::Double("Connector loss (dB)", const_cast<double*>(&m_connectors_loss_dB), -50.0, 50.0),
    };
}
```

The preset is special — handle in the inspector's draw loop as a `Kind::Enum` directly (since the cable engine uses an `int` index into a global `kCoaxCablePresets` array).

- [ ] **Step 3: MixerEngine**

```cpp
// mixer_engine.h
std::vector<PropertyDesc> properties() const {
    return {
        PropertyDesc::Double("LO frequency (Hz)", const_cast<double*>(&m_lo_freq_Hz), 0.0, 100e9),
        PropertyDesc::Double("Conversion gain (dB)", const_cast<double*>(&m_conv_gain_dB), -50.0, 50.0),
        PropertyDesc::Double("Noise figure (dB)", const_cast<double*>(&m_nf_dB), 0.0, 30.0),
    };
}
```

- [ ] **Step 4: SplitterEngine**

```cpp
// splitter_engine.h
// (no parameters; returns empty vector)
std::vector<PropertyDesc> properties() const { return {}; }
```

- [ ] **Step 5: IdealFilterEngine**

```cpp
// ideal_filter_engine.h
std::vector<PropertyDesc> properties() const {
    return {
        PropertyDesc::Double("Cutoff low (Hz)", const_cast<double*>(&m_fc_low_Hz), 1.0, 100e9),
        PropertyDesc::Double("Cutoff high (Hz)", const_cast<double*>(&m_fc_high_Hz), 1.0, 100e9),
    };
}
```

The filter type is special — `FilterType` is a 4-value enum (LPF/HPF/BPF/BSF). Add a `Kind::Enum` entry with the labels in the inspector.

- [ ] **Step 6: PFBChannelizerEngine**

```cpp
// pfb_channelizer_engine.h
std::vector<PropertyDesc> properties() const {
    return {
        PropertyDesc::Int("Channels (M)", const_cast<int*>(&m_cfg.M), 2, 256),
        PropertyDesc::Int("Taps/branch (K)", const_cast<int*>(&m_cfg.K), 2, 32),
        PropertyDesc::Double("Kaiser beta", const_cast<double*>(&m_cfg.beta), 0.0, 20.0),
        PropertyDesc::Int("Active channel", &m_active_channel, 0, m_cfg.M - 1),
    };
}
```

- [ ] **Step 7: SParamEngine**

```cpp
// s_param_engine.h
std::vector<PropertyDesc> properties() const {
    std::vector<PropertyDesc> out;
    out.push_back(PropertyDesc::Double("Noise figure (dB)", const_cast<double*>(&m_nf_dB), 0.0, 30.0));
    if (m_nonlinear.enabled()) {
        out.push_back(PropertyDesc::Double("OIP2 (dBm)", ...));
        out.push_back(PropertyDesc::Double("OIP3 (dBm)", ...));
    }
    return out;
}
```

The mode and forward-param-index are special — handle as `Kind::Enum` in the inspector.

- [ ] **Step 8: SignalGeneratorEngine**

The current inspector for the generator shows a tone list (not just a parameter list). Tone list is special: each tone has freq + power + phase. For now, expose the generator's properties as empty (the tone list stays as a custom draw). Alternative: add a `Kind::ToneList` extension — pick whichever is less code.

For the minimal version, leave the generator's `drawGeneratorProperties` as a custom draw (do not collapse this one). The `properties()` method returns an empty vector for signal_generator.

- [ ] **Step 9: Build and test after each engine**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: 72 tests pass after each addition.

- [ ] **Step 10: Commit per engine (or batched — pick whichever keeps commits reviewable)**

```bash
git add adc/ coax/ mixer/ splitter/ ideal_filter/ pfb_channelizer/ s_parametric_component/ signal_generator/
git commit -m "refactor: add properties() to all engines"
```

---

## Task 4: Replace InspectorPanel with a generic draw loop

**Files:**
- Modify: `app/include/inspector_panel.h`
- Modify: `app/src/inspector_panel.cpp`

**Interfaces:**
- Consumes: `IComponentEngine::properties()`.
- Produces: a single `drawProperties(IComponentEngine&)` method that iterates the descriptors. The 9 per-type `drawXxxProperties` methods are deleted.

- [ ] **Step 1: Delete the 9 method declarations from `inspector_panel.h`**

In `app/include/inspector_panel.h`, delete the 9 `drawXxxProperties` declarations. Keep:
- `drawAmplifierProperties` (gone)
- `drawCoaxCableProperties` (gone)
- `drawMixerProperties` (gone)
- `drawSplitterProperties` (gone)
- `drawSParamProperties` (gone)
- `drawAdcProperties` (gone)
- `drawGeneratorProperties` (kept — the tone list is special)
- `drawPFBProperties` (kept — has a separate grid widget)
- `drawIdealFilterProperties` (gone)
- `drawGroupPanel` (kept — groups are not engines)

Add one new method:
```cpp
void drawProperties(IComponentEngine& engine);
```

- [ ] **Step 2: Implement `drawProperties` in `inspector_panel.cpp`**

In `app/src/inspector_panel.cpp`, add:

```cpp
void InspectorPanel::drawProperties(IComponentEngine& engine) {
    for (const auto& desc : engine.properties()) {
        drawProperty(desc);
    }
}
```

Include `property_grid.h` at the top of `inspector_panel.cpp`.

- [ ] **Step 3: Replace the per-type dispatches in `InspectorPanel::draw`**

In `InspectorPanel::draw`, find the switch on `m_selected.type` (or similar). For each `ComponentType`:
- If it's a type whose `drawXxxProperties` is gone (Amplifier, Mixer, Splitter, IdealFilter, Adc), call `drawProperties(*hit.engine)` instead.
- If it's a type that still has a custom draw (Generator, PFB), keep the custom call.

Example for amplifier:
```cpp
// Was:
drawAmplifierProperties(static_cast<AmplifierEngine&>(*hit.engine), index);
// Becomes:
drawProperties(*hit.engine);
```

- [ ] **Step 4: Handle the special-case widgets**

For coax (preset dropdown), filter (type dropdown), s-param (mode dropdown, forward-param dropdown), the inspector still needs a custom draw. Options:
- (a) Add them as `Kind::Enum` properties to the engine's `properties()` method. The descriptor stores a `const char* const*` items array and a `*int` value.
- (b) Keep small `drawXxxSpecial()` methods for these.

Pick (a) — it's the consistent pattern. The coax preset items are a static array of preset names; the filter types are 4 strings; s-param mode and forward-param are similar. Add them to the engine's `properties()` method (extend Task 3 if needed).

- [ ] **Step 5: Delete the per-type `drawXxxProperties` implementations in `inspector_panel.cpp`**

Delete all 9 method bodies (except `drawGeneratorProperties` if you kept it for the tone list, and `drawGroupPanel`).

- [ ] **Step 6: Build to verify**

```bash
cmake --build build
```

Expected: BUILD SUCCEEDED. If a method is still referenced, fix the call site.

- [ ] **Step 7: Run tests**

```bash
ctest --test-dir build --output-on-failure
```

Expected: 72 tests pass.

- [ ] **Step 8: Side-by-side visual check**

This is the most important verification step. Before starting this task, take a screenshot of the inspector for each engine type. After the task, take the same screenshots and compare:

- Amplifier: gain, NF, OIP2/OIP3 (when nonlinear enabled) — same labels, same ranges, same widgets.
- ADC: sample rate, NSD, bits, V_fs — same.
- Coax: preset dropdown, length, connector loss — same.
- Mixer: LO freq, conv gain, NF — same.
- Splitter: no properties — empty inspector body, but the component is still selectable.
- Ideal filter: type, cutoff — same.
- PFB: M, K, beta, active channel — same.
- S-param: mode, port selections, NF, OIP2/OIP3 (if enabled) — same.
- Generator: tone list (still custom draw) — same.

If any control looks different, the descriptor or the special-case widget was set up wrong. Fix and re-verify.

- [ ] **Step 9: Commit**

```bash
git add app/
git commit -m "refactor: collapse InspectorPanel into a generic property loop"
```

---

## Task 5: Final verification

- [ ] **Step 1: Full build**

```bash
cmake --build build
```

Expected: BUILD SUCCEEDED.

- [ ] **Step 2: Full test**

```bash
ctest --test-dir build --output-on-failure
```

Expected: 72 tests pass.

- [ ] **Step 3: Manual smoke**

Click through each engine type in the inspector. All controls present and functional. No crashes, no missing labels, no wrong ranges.

- [ ] **Step 4: Measure line reduction**

```bash
find . -path ./build -prune -o -path ./node_modules -prune -o -path ./out -prune -o -type f \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" \) -print | xargs wc -l 2>/dev/null | tail -1
```

Expected: ~300 lines removed from `inspector_panel.cpp` alone, with ~100 lines added to engine headers for `properties()` methods. Net ~200 line reduction; full cleanup total should be ~1,100-1,300 lines from the original 10,675.

- [ ] **Step 5: Merge**

Push and open a PR.
