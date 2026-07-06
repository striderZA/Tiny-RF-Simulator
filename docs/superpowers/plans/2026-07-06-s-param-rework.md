# S-Parameter Component Rework — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace generic `SParamEngine` with per-component S-parameter mode (amplifier, ideal filter, equalizer).

**Architecture:** Delete `s_parametric_component/` entirely. Each engine gets `SParameterData` + `bool m_sparam_mode`. `update()` branches on the flag — when S-param mode is active, S21 interpolation replaces the ideal model. The `touchstone/` data layer stays unchanged. A new `EqualizerEngine` with ideal-gain-slope mode and S-param mode is added.

**Tech Stack:** C++20, CMake, Catch2

## Global Constraints

- `s_parametric_component/` is deleted — no references to `SParamEngine`, `NodeKind::SParam`, or "Add S-Param Component" survive
- `touchstone/s_parameter_data.h/.cpp` is untouched
- NF and nonlinearity stay per-component (amplifier keeps its own)
- Forward param is always S21 (index `1 * numPorts + 0`)
- Each component's ideal-mode behavior is unchanged

---

### Task 1: Remove s_parametric_component directory and all references

**Files:**
- Delete: `s_parametric_component/CMakeLists.txt`
- Delete: `s_parametric_component/include/s_param_engine.h`
- Delete: `s_parametric_component/src/s_param_engine.cpp`
- Delete: `tests/test_s_parameter_amplifier.cpp` (migrated to amp S-param test in Task 3)
- Modify: `CMakeLists.txt` — remove `add_subdirectory("s_parametric_component")`
- Modify: `node_graph/include/node_graph_engine.h` — remove `SParam` from `NodeKind` enum, `themeColor()` switch, `nodeKindFromLabel()` prefix match
- Modify: `node_graph/include/node_graph_widget.h` — remove `std::function<void()> onAddSParamComponent;`
- Modify: `node_graph/src/node_graph_widget.cpp` — remove "Add S-Param Component" menu item, remove `drawSParamSymbol()`, remove `case NodeKind::SParam:` from `drawSchematicSymbol()`
- Modify: `app/include/app.h` — remove `#include "s_param_engine.h"`
- Modify: `app/src/app.cpp` — remove `m_graph_widget->onAddSParamComponent = ...` block
- Modify: `app/include/inspector_panel.h` — remove `ComponentType::SParam`, remove `void drawSParamProperties(...)`, remove `SParamEngine` forward decl
- Modify: `app/src/inspector_panel.cpp` — remove `#include "s_param_engine.h"`, remove `case ComponentType::SParam:` in `findSelected()` and `draw()`, remove `drawSParamProperties()` function entirely

- [ ] **Step 1: Delete directory and CMake reference**

```bash
rm -rf s_parametric_component
```
Edit root `CMakeLists.txt`: remove the line `add_subdirectory("s_parametric_component")`.

- [ ] **Step 2: Remove NodeKind::SParam from node_graph_engine.h**

Edit `node_graph/include/node_graph_engine.h`:
- Remove `SParam,` from the `NodeKind` enum
- Remove `case NodeKind::SParam: return 0xFFF472B6; // pink` from `themeColor()`
- Remove `if (label.rfind("S-Param", 0) == 0) return NodeKind::SParam;` from `nodeKindFromLabel()`

- [ ] **Step 3: Remove S-Param from node_graph_widget**

Edit `node_graph/include/node_graph_widget.h`: remove declaration of `std::function<void()> onAddSParamComponent;`

Edit `node_graph/src/node_graph_widget.cpp`:
- Remove the `static void drawSParamSymbol(...)` function entirely
- Remove the `case NodeKind::SParam:` line from `drawSchematicSymbol()`
- Remove the `if (ImGui::MenuItem("Add S-Param Component")) { if (onAddSParamComponent) onAddSParamComponent(); }` block from the canvas context menu

- [ ] **Step 4: Remove S-Param from app and inspector panel**

Edit `app/include/app.h`: remove `#include "s_param_engine.h"`

Edit `app/src/app.cpp`: remove the entire `m_graph_widget->onAddSParamComponent = [this]() { ... };` block

Edit `app/include/inspector_panel.h`:
- Remove `#include "s_param_engine.h"` or the forward declaration `class SParamEngine;`
- Remove `SParam,` from the `ComponentType` enum
- Remove `void drawSParamProperties(SParamEngine& engine, int index);`

Edit `app/src/inspector_panel.cpp`:
- Remove `#include "s_param_engine.h"`
- Remove the `else if (dynamic_cast<SParamEngine*>(engine)) return {ComponentType::SParam, engine};` line from `findSelected()`
- Remove the `case ComponentType::SParam: drawSParamProperties(...); break;` block from `draw()`
- Remove the entire `drawSParamProperties()` function

- [ ] **Step 5: Remove old test file**

Edit `tests/CMakeLists.txt`: remove any reference to `test_s_parameter_amplifier.cpp`

Delete `tests/test_s_parameter_amplifier.cpp`

- [ ] **Step 6: Build to verify removal compiles cleanly**

```bash
cmake --build build --config Debug 2>&1 | tail -20
```
Expected: No errors referencing `SParamEngine`, `s_param_engine.h`, or `SParam` in the NodeKind enum.

---

### Task 2: Add S-parameter mode to AmplifierEngine

**Files:**
- Modify: `amplifier/include/amplifier_engine.h`
- Modify: `amplifier/src/amplifier_engine.cpp`
- Modify: `app/src/inspector_panel.cpp` (amplifier properties UI)

**Interfaces:**
- Consumes: `SParameterData` from `touchstone/s_parameter_data.h`
- Produces: `AmplifierEngine::setSParamFilepath()`, `sparamMode()`, `setSParamMode()`, `sparamLoaded()`, `sparamFilepath()`

- [ ] **Step 1: Add S-param fields and methods to header**

Edit `amplifier/include/amplifier_engine.h`:
- Add `#include "s_parameter_data.h"` at the top (after other includes)
- Add these members to the private section:

```cpp
SParameterData m_sparam_data;
std::string m_sparam_filepath;
bool m_sparam_mode = false;
int m_sparam_fwd_idx = 0;
const Spectrum* m_cached_sparam_input = nullptr;
uint64_t m_cached_sparam_generation = 0;
```

- Add these public methods (before `private:`):

```cpp
void setSParamFilepath(const std::string& path);
bool sparamMode() const { return m_sparam_mode; }
void setSParamMode(bool en) { m_sparam_mode = en; m_dirty = true; }
bool sparamLoaded() const { return m_sparam_data.loaded(); }
const std::string& sparamFilepath() const { return m_sparam_filepath; }
```

- [ ] **Step 2: Implement setSParamFilepath in the .cpp**

Edit `amplifier/src/amplifier_engine.cpp`. Add `#include "s_parameter_data.h"` if not auto-included.

Add this method:

```cpp
void AmplifierEngine::setSParamFilepath(const std::string& path) {
    m_sparam_filepath = path;
    m_sparam_mode = m_sparam_data.load(path);
    if (m_sparam_data.loaded())
        m_sparam_fwd_idx = 1 * m_sparam_data.numPorts() + 0; // S21
    m_dirty = true;
}
```

- [ ] **Step 3: Add S-param branch in update()**

Edit `amplifier/src/amplifier_engine.cpp`, inside `update()`. After the existing cache check, insert the S-param branch before the ideal-mode code.

Current `update()` flow:
```
1. Cache check (return if unchanged)
2. Set dirty=false, update cache pointer/gen
3. Build frequency grid
4. Apply gain to tones
5. Nonlinearity
6. Noise processing
7. bumpGeneration
```

New flow: insert after step 1 (cache check), before step 2:

```cpp
// --- S-parameter mode ---
if (m_sparam_mode && m_sparam_data.loaded()) {
    if (!m_dirty && in_ptr == m_cached_sparam_input &&
        (!in_ptr || in_ptr->generation == m_cached_sparam_generation))
        return;
    m_dirty = false;
    m_cached_sparam_input = in_ptr;
    if (in_ptr) m_cached_sparam_generation = in_ptr->generation;

    auto& out = m_node.outputs[0];

    // Frequency grid
    if (in_ptr && !in_ptr->frequencies.empty())
        out.frequencies = in_ptr->frequencies;
    else if (out.frequencies.size() < 2)
        buildDefaultFrequencyGrid(out.frequencies);

    out.tones = in_ptr ? in_ptr->tones : std::vector<Spectrum::Tone>{};
    const size_t N = out.frequencies.size();
    int idx = m_sparam_fwd_idx;

    // Apply S21 complex gain to tones
    for (auto& t : out.tones) {
        auto S = m_sparam_data.interpolate(t.freq_Hz, idx);
        double mag = std::abs(S);
        t.power_dBm += 20.0 * std::log10(mag);
        t.phase_deg += std::arg(S) * 180.0 / std::numbers::pi;
    }

    // Nonlinear processing (same as ideal mode, gain from S21 at each freq)
    if (m_nonlinear.enabled() && in_ptr && !in_ptr->tones.empty()) {
        size_t n_fund = out.tones.size();
        auto result = m_nonlinear.process(in_ptr->tones,
            [this, idx](double freq) {
                auto S = this->m_sparam_data.interpolate(freq, idx);
                return std::abs(S);
            });
        for (const auto& t : result.extra_tones)
            out.tones.push_back(t);
        if (result.compression_dB < -1e8) {
            for (size_t i = 0; i < n_fund; ++i)
                out.tones[i].power_dBm = MIN_POWER;
        } else {
            for (size_t i = 0; i < n_fund; ++i)
                out.tones[i].power_dBm += result.compression_dB;
        }
    }

    // Phase and noise
    if (in_ptr && !in_ptr->phase_deg.empty())
        out.phase_deg = in_ptr->phase_deg;
    else
        out.phase_deg.assign(N, 0.0);

    if (N < 2) {
        out.noise_W.assign(N, 0.0);
        out.noise_added_W.assign(N, 0.0);
        out.noise_total_W.assign(N, 0.0);
        out.bumpGeneration();
        return;
    }

    // Amplify input noise by |S21|^2
    out.noise_W.assign(N, 0.0);
    for (size_t i = 0; i < N; ++i) {
        auto S = m_sparam_data.interpolate(out.frequencies[i], idx);
        double gain_linear = std::norm(S);
        double nin = (in_ptr && i < in_ptr->noise_total_W.size() ? in_ptr->noise_total_W[i] : 0.0);
        out.noise_W[i] = gain_linear * nin;
    }

    // Noise figure (same as ideal mode)
    double G = 1.0; // NF uses its own gain-independent added noise density
    double added_density = addedNoiseDensity_W_per_Hz(m_nf_dB, G);
    out.noise_added_W.resize(N);
    if (added_density <= 0.0)
        out.noise_added_W.assign(N, 0.0);
    else
        out.noise_added_W.assign(N, added_density);

    out.noise_total_W.resize(N);
    for (size_t i = 0; i < N; ++i)
        out.noise_total_W[i] = out.noise_W[i] + out.noise_added_W[i];

    out.bumpGeneration();
    return;
}
```

- [ ] **Step 4: Update hoverSummary for S-param mode**

Edit `AmplifierEngine::hoverSummary()`:

```cpp
std::string AmplifierEngine::hoverSummary() const {
    if (m_sparam_mode && m_sparam_data.loaded()) {
        return "S-Param Amp | NF: " + std::to_string(m_nf_dB) + " dB"
            + (m_nonlinear.enabled() ? " | NL On" : "");
    }
    return "Gain: " + std::to_string(m_gain_dB) + " dB | NF: " + std::to_string(m_nf_dB) + " dB";
}
```

- [ ] **Step 5: Add S-param UI to amplifier inspector panel**

Edit `app/src/inspector_panel.cpp`, in `drawAmplifierProperties()`.

After the existing gain control, insert:

```cpp
ImGui::SeparatorText("Mode");
const char* amp_modes[] = {"Ideal", "S-Parameter"};
int amp_mode_idx = engine.sparamMode() ? 1 : 0;
if (ImGui::Combo("##amp_mode", &amp_mode_idx, amp_modes, IM_ARRAYSIZE(amp_modes))) {
    if (amp_mode_idx == 0) {
        engine.setSParamMode(false);
    }
}

if (amp_mode_idx == 1) {
    ImGui::TextWrapped("File: %s", engine.sparamFilepath().c_str());
    if (ImGui::Button("Browse##amp_sparam")) {
        auto result = pfd::open_file("Select S-parameter file", "",
            {"S-parameter Files", "*.s2p *.s3p *.s4p *.sNp"}).result();
        if (!result.empty()) {
            engine.setSParamFilepath(result[0]);
            LOG_INFO("Amplifier S-param file: %s", result[0].c_str());
        }
    }
    if (engine.sparamLoaded()) {
        ImGui::TextDisabled("Points: %zu | Ports: %d",
            engine.sparamData().freqs().size(),
            engine.sparamData().numPorts());
    } else if (!engine.sparamFilepath().empty()) {
        ImGui::TextColored(ImVec4(1,0,0,1), "Failed to load file");
    }
}

// Gray out gain control in S-param mode
if (engine.sparamMode()) {
    ImGui::BeginDisabled();
    double g = engine.gain_dB();
    utils::inputDouble("Gain (dB)", g, 1, 10, "%.1f", -10.0, 40.0);
    ImGui::EndDisabled();
} else {
    double gain = engine.gain_dB();
    if (utils::inputDouble("Gain (dB)", gain, 1, 10, "%.1f", -10.0, 40.0))
        engine.setGain_dB(gain);
}
```

Keep NF and nonlinearity controls visible in both modes (unchanged).

Note: `engine.sparamData()` needs to be added to the header as an accessor:

```cpp
const SParameterData& sparamData() const { return m_sparam_data; }
```

Also need to add `#include "portable-file-dialogs.h"` in inspector_panel.cpp if not already there (it is — used for SParamEngine previously).

- [ ] **Step 6: Build to verify**

```bash
cmake --build build --config Debug 2>&1 | tail -20
```
Expected: No errors.

---

### Task 3: Migrate amplifier S-param tests

**Files:**
- Create: `tests/test_amplifier_sparam.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Create test file**

Create `tests/test_amplifier_sparam.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "amplifier_engine.h"
#include "signal_generator_engine.h"
#include "node_graph_engine.h"
#include <cmath>
#include <numbers>

using Catch::Approx;

static std::string s2p_path() {
    return std::string(PROJECT_SOURCE_DIR) +
        "/amplifier/data_files/adm-8344psm-s_parameters/"
        "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
}

TEST_CASE("Amplifier ideal mode unchanged after S-param refactor", "[amp][sparam]") {
    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setGain_dB(15.0);

    SignalGeneratorEngine gen(1, graph);
    gen.addTone(100e6, -20.0);
    gen.update(0.0);

    amp.node().inputs[0] = &gen.node().outputs[0];
    amp.update(0.0);

    REQUIRE(amp.node().outputs[0].tones.size() == 1);
    REQUIRE(amp.node().outputs[0].tones[0].power_dBm == Approx(-5.0).margin(0.01));
}

TEST_CASE("Amplifier S-param mode loads .s2p and applies S21 gain", "[amp][sparam]") {
    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setSParamFilepath(s2p_path());

    REQUIRE(amp.sparamLoaded());
    REQUIRE(amp.sparamMode());
    REQUIRE(amp.sparamData().numPorts() == 2);
    REQUIRE(amp.sparamData().freqs().size() > 10);

    SignalGeneratorEngine gen(1, graph);
    gen.addTone(1e9, -20.0);
    gen.update(0.0);

    amp.node().inputs[0] = &gen.node().outputs[0];
    amp.update(0.0);

    const auto& out = amp.node().outputs[0];
    REQUIRE(out.tones.size() == 1);

    // S21 at 1 GHz on this amp gives ~19.6 dB gain
    auto S21 = amp.sparamData().interpolate(1e9, 2);
    double expected_gain = 20.0 * std::log10(std::abs(S21));
    REQUIRE(out.tones[0].power_dBm == Approx(-20.0 + expected_gain).margin(0.5));
    REQUIRE(out.tones[0].phase_deg == Approx(std::arg(S21) * 180.0 / std::numbers::pi).margin(1.0));
}

TEST_CASE("Amplifier S-param applies phase rotation", "[amp][sparam]") {
    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setSParamFilepath(s2p_path());
    REQUIRE(amp.sparamLoaded());

    SignalGeneratorEngine gen(1, graph);
    gen.addTone(1e9, -20.0);
    gen.update(0.0);

    amp.node().inputs[0] = &gen.node().outputs[0];
    amp.update(0.0);

    const auto& out = amp.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    auto S21 = amp.sparamData().interpolate(1e9, 2);
    double expected_phase = std::arg(S21) * 180.0 / std::numbers::pi;
    REQUIRE(out.tones[0].phase_deg == Approx(expected_phase).margin(1.0));
}

TEST_CASE("Amplifier S-param interpolates between data points", "[amp][sparam]") {
    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setSParamFilepath(s2p_path());
    REQUIRE(amp.sparamLoaded());

    SignalGeneratorEngine gen(1, graph);
    gen.addTone(1.005e9, -20.0);
    gen.update(0.0);

    amp.node().inputs[0] = &gen.node().outputs[0];
    amp.update(0.0);

    const auto& out = amp.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    auto S21 = amp.sparamData().interpolate(1.005e9, 2);
    double expected_gain = 20.0 * std::log10(std::abs(S21));
    REQUIRE(out.tones[0].power_dBm == Approx(-20.0 + expected_gain).margin(0.5));
}

TEST_CASE("Amplifier S-param handles out-of-band frequency", "[amp][sparam]") {
    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setSParamFilepath(s2p_path());
    REQUIRE(amp.sparamLoaded());

    SignalGeneratorEngine gen(1, graph);
    double f_outside = amp.sparamData().freqs().back() + 10e9;
    gen.addTone(f_outside, -20.0);
    gen.update(0.0);

    amp.node().inputs[0] = &gen.node().outputs[0];
    amp.update(0.0);

    const auto& out = amp.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    auto S21 = amp.sparamData().interpolate(f_outside, 2);
    double expected_phase = std::arg(S21) * 180.0 / std::numbers::pi;
    REQUIRE(out.tones[0].phase_deg == Approx(expected_phase).margin(0.1));
}

TEST_CASE("Amplifier S-param adds noise figure correctly", "[amp][sparam][nf]") {
    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setSParamFilepath(s2p_path());
    REQUIRE(amp.sparamLoaded());

    SignalGeneratorEngine gen(1, graph);
    gen.update(0.0);
    amp.node().inputs[0] = &gen.node().outputs[0];
    amp.update(0.0);

    // NF = 0: noise_added_W should be zero
    amp.setNF_dB(0.0);
    amp.update(0.0);
    const auto& out0 = amp.node().outputs[0];
    REQUIRE(!out0.noise_added_W.empty());
    for (double n : out0.noise_added_W)
        REQUIRE(n == Approx(0.0).margin(1e-30));

    // NF = 3 dB: noise_added_W should be positive
    amp.setNF_dB(3.0);
    amp.update(0.0);
    const auto& out3 = amp.node().outputs[0];
    bool any_positive = false;
    for (double n : out3.noise_added_W) {
        if (n > 0.0) { any_positive = true; break; }
    }
    REQUIRE(any_positive);
}

TEST_CASE("Amplifier S-param nonlinear creates harmonics", "[amp][sparam][nonlinear]") {
    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setSParamFilepath(s2p_path());
    REQUIRE(amp.sparamLoaded());

    SignalGeneratorEngine gen(1, graph);
    gen.addTone(100e6, -40.0);
    gen.update(0.0);

    amp.setOIP2_dBm(40.0);
    amp.setOIP3_dBm(30.0);
    amp.setEnableNonlinear(true);
    amp.node().inputs[0] = &gen.node().outputs[0];
    amp.update(0.0);

    const auto& out = amp.node().outputs[0];
    REQUIRE(out.tones.size() >= 3);

    bool found_h2 = false, found_h3 = false;
    for (const auto& t : out.tones) {
        if (std::abs(t.freq_Hz - 200e6) < 1.0) found_h2 = true;
        if (std::abs(t.freq_Hz - 300e6) < 1.0) found_h3 = true;
    }
    REQUIRE(found_h2);
    REQUIRE(found_h3);
}

TEST_CASE("Amplifier S-param handles bad file gracefully", "[amp][sparam]") {
    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setSParamFilepath("/nonexistent/file.s2p");
    REQUIRE(!amp.sparamLoaded());
    REQUIRE(!amp.sparamMode());
    amp.update(0.0); // should not crash
}
```

- [ ] **Step 2: Add test to CMakeLists.txt**

Edit `tests/CMakeLists.txt`:

Find the `target_sources(rf_sim_tests ...)` block and add:
```
${CMAKE_CURRENT_SOURCE_DIR}/test_amplifier_sparam.cpp
```

Also ensure the test links against the amplifier engine library (it probably already does for other tests).

- [ ] **Step 3: Build and run tests**

```bash
cmake --build build --config Debug 2>&1 | tail -10
cd build && ctest --test-dir build -R "amp.*sparam" -V
```
Expected: All new tests pass. Old `test_s_parameter_amplifier` tests are gone (file deleted in Task 1).

---

### Task 4: Add S-parameter mode to IdealFilterEngine

**Files:**
- Modify: `ideal_filter/include/ideal_filter_engine.h`
- Modify: `ideal_filter/src/ideal_filter_engine.cpp`
- Create: `tests/test_ideal_filter_sparam.cpp`
- Modify: `app/src/inspector_panel.cpp` (filter properties UI)

**Interfaces:**
- Consumes: `SParameterData`
- Produces: Same method names as amp: `setSParamFilepath()`, `sparamMode()`, `setSParamMode()`, `sparamLoaded()`, `sparamFilepath()`, `sparamData()`

- [ ] **Step 1: Add S-param fields and methods to header**

Edit `ideal_filter/include/ideal_filter_engine.h` — same pattern as the amplifier:

Add `#include "s_parameter_data.h"`.

Add to public section:
```cpp
void setSParamFilepath(const std::string& path);
bool sparamMode() const { return m_sparam_mode; }
void setSParamMode(bool en) { m_sparam_mode = en; m_dirty = true; }
bool sparamLoaded() const { return m_sparam_data.loaded(); }
const std::string& sparamFilepath() const { return m_sparam_filepath; }
const SParameterData& sparamData() const { return m_sparam_data; }
```

Add to private section:
```cpp
SParameterData m_sparam_data;
std::string m_sparam_filepath;
bool m_sparam_mode = false;
int m_sparam_fwd_idx = 0;
const Spectrum* m_cached_sparam_input = nullptr;
uint64_t m_cached_sparam_generation = 0;
```

- [ ] **Step 2: Implement setSParamFilepath and S-param branch in update()**

In `ideal_filter/src/ideal_filter_engine.cpp`:

Add the method:
```cpp
void IdealFilterEngine::setSParamFilepath(const std::string& path) {
    m_sparam_filepath = path;
    m_sparam_mode = m_sparam_data.load(path);
    if (m_sparam_data.loaded())
        m_sparam_fwd_idx = 1 * m_sparam_data.numPorts() + 0;
    m_dirty = true;
}
```

In `update()`, insert an S-param branch after the cache check (same pattern as amp, but simpler — no NF, no nonlinearity):

```cpp
// --- S-parameter mode ---
if (m_sparam_mode && m_sparam_data.loaded()) {
    if (!m_dirty && in_ptr == m_cached_sparam_input &&
        (!in_ptr || in_ptr->generation == m_cached_sparam_generation))
        return;
    m_dirty = false;
    m_cached_sparam_input = in_ptr;
    if (in_ptr) m_cached_sparam_generation = in_ptr->generation;

    auto& out = m_node.outputs[0];

    if (in_ptr && !in_ptr->frequencies.empty())
        out.frequencies = in_ptr->frequencies;
    else if (out.frequencies.size() < 2)
        buildDefaultFrequencyGrid(out.frequencies);

    const size_t N = out.frequencies.size();
    int idx = m_sparam_fwd_idx;

    // Apply S21 to tones
    out.tones = in_ptr ? in_ptr->tones : std::vector<Spectrum::Tone>{};
    for (auto& t : out.tones) {
        auto S = m_sparam_data.interpolate(t.freq_Hz, idx);
        t.power_dBm += 20.0 * std::log10(std::abs(S));
        t.phase_deg += std::arg(S) * 180.0 / std::numbers::pi;
    }

    // Phase
    if (in_ptr && !in_ptr->phase_deg.empty())
        out.phase_deg = in_ptr->phase_deg;
    else
        out.phase_deg.assign(N, 0.0);

    if (N < 2) {
        out.noise_W.assign(N, 0.0);
        out.noise_added_W.assign(N, 0.0);
        out.noise_total_W.assign(N, 0.0);
        out.bumpGeneration();
        return;
    }

    // Filter noise by |S21|^2, no added noise
    out.noise_W.assign(N, 0.0);
    for (size_t i = 0; i < N; ++i) {
        auto S = m_sparam_data.interpolate(out.frequencies[i], idx);
        double nin = (in_ptr && i < in_ptr->noise_total_W.size() ? in_ptr->noise_total_W[i] : 0.0);
        out.noise_W[i] = std::norm(S) * nin;
    }
    out.noise_added_W.assign(N, 0.0);
    out.noise_total_W = out.noise_W;

    out.bumpGeneration();
    return;
}
```

- [ ] **Step 3: Update hoverSummary**

```cpp
std::string IdealFilterEngine::hoverSummary() const {
    if (m_sparam_mode && m_sparam_data.loaded()) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "S-Param Filter | %zu pts", m_sparam_data.freqs().size());
        return buf;
    }
    // ... existing code ...
}
```

- [ ] **Step 4: Add S-param UI to filter inspector panel**

In `drawIdealFilterProperties()`, add a mode toggle before the existing controls:

```cpp
ImGui::SeparatorText("Mode");
const char* filter_modes[] = {"Ideal", "S-Parameter"};
int f_mode_idx = engine.sparamMode() ? 1 : 0;
if (ImGui::Combo("##filter_mode", &f_mode_idx, filter_modes, IM_ARRAYSIZE(filter_modes))) {
    engine.setSParamMode(f_mode_idx == 1);
}

if (engine.sparamMode()) {
    ImGui::TextWrapped("File: %s", engine.sparamFilepath().c_str());
    if (ImGui::Button("Browse##filter_sparam")) {
        auto result = pfd::open_file("Select S-parameter file", "",
            {"S-parameter Files", "*.s2p *.s3p *.s4p *.sNp"}).result();
        if (!result.empty()) {
            engine.setSParamFilepath(result[0]);
        }
    }
    if (engine.sparamLoaded()) {
        ImGui::TextDisabled("Points: %zu | Ports: %d",
            engine.sparamData().freqs().size(),
            engine.sparamData().numPorts());
    }
    // Disable ideal-mode controls in S-param mode
    ImGui::BeginDisabled();
}

// ... existing filter type/cutoff controls ...

if (engine.sparamMode()) {
    ImGui::EndDisabled();
}
```

- [ ] **Step 5: Create and run tests**

Create `tests/test_ideal_filter_sparam.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "ideal_filter_engine.h"
#include "signal_generator_engine.h"
#include "node_graph_engine.h"

using Catch::Approx;

static std::string s2p_path() {
    return std::string(PROJECT_SOURCE_DIR) +
        "/amplifier/data_files/adm-8344psm-s_parameters/"
        "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
}

TEST_CASE("IdealFilter ideal mode unchanged after S-param refactor", "[filter][sparam]") {
    NodeGraphEngine graph;
    IdealFilterEngine flt(0, graph);
    flt.setFilterType(FilterType::LPF);
    flt.setCutoff_Hz(200e6);

    SignalGeneratorEngine gen(1, graph);
    gen.addTone(100e6, -10.0); // in passband
    gen.addTone(300e6, -10.0); // in stopband
    gen.update(0.0);

    flt.node().inputs[0] = &gen.node().outputs[0];
    flt.update(0.0);

    const auto& out = flt.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    REQUIRE(out.tones[0].freq_Hz == Approx(100e6));
}

TEST_CASE("IdealFilter S-param mode loads file and applies S21", "[filter][sparam]") {
    NodeGraphEngine graph;
    IdealFilterEngine flt(0, graph);
    flt.setSParamFilepath(s2p_path());

    REQUIRE(flt.sparamLoaded());
    REQUIRE(flt.sparamMode());

    SignalGeneratorEngine gen(1, graph);
    gen.addTone(1e9, -20.0);
    gen.update(0.0);

    flt.node().inputs[0] = &gen.node().outputs[0];
    flt.update(0.0);

    const auto& out = flt.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    auto S21 = flt.sparamData().interpolate(1e9, 2);
    double expected_gain = 20.0 * std::log10(std::abs(S21));
    REQUIRE(out.tones[0].power_dBm == Approx(-20.0 + expected_gain).margin(0.5));
}
```

Add to `tests/CMakeLists.txt`: add `test_ideal_filter_sparam.cpp` to sources.

Build and run:
```bash
cmake --build build --config Debug
cd build && ctest --test-dir build -R "filter.*sparam" -V
```

---

### Task 5: Create EqualizerEngine

**Files:**
- Create: `equalizer/CMakeLists.txt`
- Create: `equalizer/include/equalizer_engine.h`
- Create: `equalizer/src/equalizer_engine.cpp`
- Create: `tests/test_equalizer.cpp`
- Modify: `CMakeLists.txt` — add `add_subdirectory("equalizer")`
- Modify: `node_graph/include/node_graph_engine.h` — add `NodeKind::Equalizer` + color + label match
- Modify: `node_graph/include/node_graph_widget.h` — add `onAddEqualizer` callback
- Modify: `node_graph/src/node_graph_widget.cpp` — add Equalizer symbol + context menu entry
- Modify: `app/src/app.cpp` — add `onAddEqualizer` wiring
- Modify: `app/include/inspector_panel.h` — add `Equalizer` to enum + `drawEqualizerProperties()`
- Modify: `app/src/inspector_panel.cpp` — add Equalizer case + properties panel

- [ ] **Step 1: Create equalizer engine header**

Create `equalizer/include/equalizer_engine.h`:

```cpp
#pragma once

#include "common.h"
#include "component_interface.h"
#include "node_graph_engine.h"
#include "s_parameter_data.h"
#include "signal_node.h"
#include <string>

class EqualizerEngine : public IComponentEngine {
public:
    EqualizerEngine(int id, NodeGraphEngine& graph);

    int id() const override { return m_id; }
    int graphNodeId() const override { return m_graph_node_id; }
    std::string hoverSummary() const override;
    int inputPinId() const override;
    int outputPinId() const override;
    SignalNode& node() override { return m_node; }
    const SignalNode& node() const override { return m_node; }
    void update(double dt) override;

    // Ideal mode parameters
    void setRefGain_dB(double g) { m_ref_gain_dB = g; m_dirty = true; }
    double refGain_dB() const { return m_ref_gain_dB; }
    void setRefFreq_Hz(double f) { m_ref_freq_Hz = f; m_dirty = true; }
    double refFreq_Hz() const { return m_ref_freq_Hz; }
    void setSlope_dBPerDecade(double s) { m_slope_dB_per_decade = s; m_dirty = true; }
    double slope_dBPerDecade() const { return m_slope_dB_per_decade; }

    // S-param mode
    void setSParamFilepath(const std::string& path);
    bool sparamMode() const { return m_sparam_mode; }
    void setSParamMode(bool en) { m_sparam_mode = en; m_dirty = true; }
    bool sparamLoaded() const { return m_sparam_data.loaded(); }
    const std::string& sparamFilepath() const { return m_sparam_filepath; }
    const SParameterData& sparamData() const { return m_sparam_data; }

private:
    int m_id;
    int m_graph_node_id = -1;
    NodeGraphEngine* m_graph = nullptr;
    SignalNode m_node;
    bool m_dirty = true;
    const Spectrum* m_cached_input_ptr = nullptr;
    uint64_t m_cached_input_generation = 0;

    // Ideal mode
    double m_ref_gain_dB = 0.0;
    double m_ref_freq_Hz = 1e9;
    double m_slope_dB_per_decade = 0.0;

    // S-param mode
    SParameterData m_sparam_data;
    std::string m_sparam_filepath;
    bool m_sparam_mode = false;
    int m_sparam_fwd_idx = 0;
    const Spectrum* m_cached_sparam_input = nullptr;
    uint64_t m_cached_sparam_generation = 0;
};
```

- [ ] **Step 2: Create equalizer engine implementation**

Create `equalizer/src/equalizer_engine.cpp`:

```cpp
#include "equalizer_engine.h"
#include <cmath>
#include <numbers>

EqualizerEngine::EqualizerEngine(int id, NodeGraphEngine& graph)
    : m_id(id), m_graph(&graph) {
    m_graph_node_id = graph.addNode("Equalizer " + std::to_string(id), &m_node, 1, 1);
    m_node.inputs.resize(1);
    m_node.outputs.resize(1);
}

int EqualizerEngine::inputPinId() const {
    return m_graph ? m_graph->inputPinId(m_graph_node_id) : -1;
}

int EqualizerEngine::outputPinId() const {
    return m_graph ? m_graph->outputPinId(m_graph_node_id) : -1;
}

void EqualizerEngine::setSParamFilepath(const std::string& path) {
    m_sparam_filepath = path;
    m_sparam_mode = m_sparam_data.load(path);
    if (m_sparam_data.loaded())
        m_sparam_fwd_idx = 1 * m_sparam_data.numPorts() + 0;
    m_dirty = true;
}

void EqualizerEngine::update(double dt) {
    (void)dt;
    const Spectrum* in_ptr = m_node.inputs.empty() ? nullptr : m_node.inputs[0];

    // --- S-parameter mode ---
    if (m_sparam_mode && m_sparam_data.loaded()) {
        if (!m_dirty && in_ptr == m_cached_sparam_input &&
            (!in_ptr || in_ptr->generation == m_cached_sparam_generation))
            return;
        m_dirty = false;
        m_cached_sparam_input = in_ptr;
        if (in_ptr) m_cached_sparam_generation = in_ptr->generation;

        auto& out = m_node.outputs[0];

        if (in_ptr && !in_ptr->frequencies.empty())
            out.frequencies = in_ptr->frequencies;
        else if (out.frequencies.size() < 2)
            buildDefaultFrequencyGrid(out.frequencies);

        const size_t N = out.frequencies.size();
        int idx = m_sparam_fwd_idx;

        out.tones = in_ptr ? in_ptr->tones : std::vector<Spectrum::Tone>{};
        for (auto& t : out.tones) {
            auto S = m_sparam_data.interpolate(t.freq_Hz, idx);
            t.power_dBm += 20.0 * std::log10(std::abs(S));
            t.phase_deg += std::arg(S) * 180.0 / std::numbers::pi;
        }

        if (in_ptr && !in_ptr->phase_deg.empty())
            out.phase_deg = in_ptr->phase_deg;
        else
            out.phase_deg.assign(N, 0.0);

        if (N < 2) {
            out.noise_W.assign(N, 0.0);
            out.noise_added_W.assign(N, 0.0);
            out.noise_total_W.assign(N, 0.0);
            out.bumpGeneration();
            return;
        }

        out.noise_W.assign(N, 0.0);
        for (size_t i = 0; i < N; ++i) {
            auto S = m_sparam_data.interpolate(out.frequencies[i], idx);
            double nin = (in_ptr && i < in_ptr->noise_total_W.size() ? in_ptr->noise_total_W[i] : 0.0);
            out.noise_W[i] = std::norm(S) * nin;
        }
        out.noise_added_W.assign(N, 0.0);
        out.noise_total_W = out.noise_W;
        out.bumpGeneration();
        return;
    }

    // --- Ideal mode ---
    if (!m_dirty && in_ptr == m_cached_input_ptr &&
        (!in_ptr || in_ptr->generation == m_cached_input_generation))
        return;
    m_dirty = false;
    m_cached_input_ptr = in_ptr;
    if (in_ptr) m_cached_input_generation = in_ptr->generation;

    auto& out = m_node.outputs[0];

    if (in_ptr && !in_ptr->frequencies.empty())
        out.frequencies = in_ptr->frequencies;
    else if (out.frequencies.size() < 2)
        buildDefaultFrequencyGrid(out.frequencies);

    const size_t N = out.frequencies.size();

    // Apply gain vs frequency profile
    out.tones = in_ptr ? in_ptr->tones : std::vector<Spectrum::Tone>{};
    for (auto& t : out.tones) {
        double gain_db = m_ref_gain_dB + m_slope_dB_per_decade * std::log10(t.freq_Hz / m_ref_freq_Hz);
        t.power_dBm += gain_db;
        // No phase rotation in ideal mode
    }

    if (in_ptr && !in_ptr->phase_deg.empty())
        out.phase_deg = in_ptr->phase_deg;
    else
        out.phase_deg.assign(N, 0.0);

    if (N < 2) {
        out.noise_W.assign(N, 0.0);
        out.noise_added_W.assign(N, 0.0);
        out.noise_total_W.assign(N, 0.0);
        out.bumpGeneration();
        return;
    }

    // Apply gain to noise per bin
    out.noise_W.assign(N, 0.0);
    for (size_t i = 0; i < N; ++i) {
        double gain_db = m_ref_gain_dB + m_slope_dB_per_decade * std::log10(out.frequencies[i] / m_ref_freq_Hz);
        double gain_linear = dbToLinear(gain_db);
        double nin = (in_ptr && i < in_ptr->noise_total_W.size() ? in_ptr->noise_total_W[i] : 0.0);
        out.noise_W[i] = gain_linear * nin;
    }
    out.noise_added_W.assign(N, 0.0);
    out.noise_total_W = out.noise_W;
    out.bumpGeneration();
}

std::string EqualizerEngine::hoverSummary() const {
    if (m_sparam_mode && m_sparam_data.loaded()) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "S-Param Equalizer | %zu pts", m_sparam_data.freqs().size());
        return buf;
    }
    char buf[96];
    std::snprintf(buf, sizeof(buf), "Equalizer | Ref: %.1f dB @ %.0f MHz | Slope: %.1f dB/dec",
        m_ref_gain_dB, m_ref_freq_Hz / 1e6, m_slope_dB_per_decade);
    return buf;
}
```

- [ ] **Step 3: Create CMakeLists.txt for equalizer**

Create `equalizer/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(equalizer LANGUAGES CXX)

add_library(equalizer_engine STATIC
    src/equalizer_engine.cpp
)

target_include_directories(equalizer_engine
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(equalizer_engine
    PUBLIC common simulator::node_graph_engine simulator::touchstone_parser
)
add_library(simulator::equalizer_engine ALIAS equalizer_engine)
```

- [ ] **Step 4: Add NodeKind::Equalizer**

Edit `node_graph/include/node_graph_engine.h`:

In the `NodeKind` enum, add `Equalizer,` between `CoaxCable` and `GroupCollapsed`.

In `nodeKindFromLabel()`, add:
```cpp
if (label.rfind("Equalizer", 0) == 0)   return NodeKind::Equalizer;
```

In `themeColor()`, add:
```cpp
case NodeKind::Equalizer:      return 0xFF34D399;  // emerald
```

- [ ] **Step 5: Add Equalizer symbol and context menu**

Edit `node_graph/src/node_graph_widget.cpp`:

Add a symbol drawing function:
```cpp
static void drawEqualizerSymbol(ImDrawList* dl, ImVec2 c, ImU32 color) {
    // Rising/falling slope line
    dl->AddLine(ImVec2(c.x - 20, c.y + 8), ImVec2(c.x + 20, c.y - 8), color, 2.0f);
    // Small reference markers
    dl->AddCircleFilled(ImVec2(c.x - 14, c.y + 4), 2.0f, color);
    dl->AddCircleFilled(ImVec2(c.x + 14, c.y - 4), 2.0f, color);
}
```

In `drawSchematicSymbol()`, add:
```cpp
case NodeKind::Equalizer:      drawEqualizerSymbol(dl, center, color);      break;
```

In `handleContextMenu()`, add after "Add Coax Cable":
```cpp
if (ImGui::MenuItem("Add Equalizer")) {
    if (onAddEqualizer) onAddEqualizer();
}
```

Edit `node_graph/include/node_graph_widget.h`: add `std::function<void()> onAddEqualizer;`

- [ ] **Step 6: Wire equalizer in app**

Edit `app/src/app.cpp`: add after the existing `onAddCoaxCable` wiring:

```cpp
m_graph_widget->onAddEqualizer = [this]() {
    m_components.add<EqualizerEngine>(m_next_component_id++, m_graph_engine);
};
```

Add `#include "equalizer_engine.h"` to `app/include/app.h`.

- [ ] **Step 7: Add equalizer to inspector panel**

Edit `app/include/inspector_panel.h`:
- Forward-declare `class EqualizerEngine;`
- Add `Equalizer` to the `ComponentType` enum (before `CoaxCable`)
- Add `void drawEqualizerProperties(EqualizerEngine& engine, int index);`

Edit `app/src/inspector_panel.cpp`:

In `findSelected()`:
```cpp
else if (dynamic_cast<EqualizerEngine*>(engine)) return {ComponentType::Equalizer, engine};
```

In `labelForHit()`:
```cpp
case ComponentType::Equalizer:     return "Equalizer " + std::to_string(hit.engine->id());
```

In `draw()`:
```cpp
case ComponentType::Equalizer:
    drawEqualizerProperties(*static_cast<EqualizerEngine*>(hit.engine), hit.engine->id());
    break;
```

Add `#include "equalizer_engine.h"`.

Add the `drawEqualizerProperties()` function:

```cpp
void InspectorPanel::drawEqualizerProperties(EqualizerEngine& engine, int index) {
    (void)index;
    
    ImGui::SeparatorText("Mode");
    const char* eq_modes[] = {"Ideal", "S-Parameter"};
    int eq_mode_idx = engine.sparamMode() ? 1 : 0;
    if (ImGui::Combo("##eq_mode", &eq_mode_idx, eq_modes, IM_ARRAYSIZE(eq_modes))) {
        engine.setSParamMode(eq_mode_idx == 1);
    }

    if (engine.sparamMode()) {
        ImGui::TextWrapped("File: %s", engine.sparamFilepath().c_str());
        if (ImGui::Button("Browse##eq_sparam")) {
            auto result = pfd::open_file("Select S-parameter file", "",
                {"S-parameter Files", "*.s2p *.s3p *.s4p *.sNp"}).result();
            if (!result.empty()) {
                engine.setSParamFilepath(result[0]);
            }
        }
        if (engine.sparamLoaded()) {
            ImGui::TextDisabled("Points: %zu | Ports: %d",
                engine.sparamData().freqs().size(),
                engine.sparamData().numPorts());
        } else if (!engine.sparamFilepath().empty()) {
            ImGui::TextColored(ImVec4(1,0,0,1), "Failed to load file");
        }
        ImGui::BeginDisabled();
    }

    double ref_gain = engine.refGain_dB();
    if (utils::inputDouble("Ref Gain (dB)", ref_gain, 1, 10, "%.1f", -40.0, 40.0))
        engine.setRefGain_dB(ref_gain);

    double ref_freq = engine.refFreq_Hz();
    if (utils::inputFrequency("Ref Freq (MHz)", ref_freq, 1.0, 100.0, "%.0f", 1.0, 100e9))
        engine.setRefFreq_Hz(ref_freq);

    double slope = engine.slope_dBPerDecade();
    if (utils::inputDouble("Slope (dB/dec)", slope, 0.1, 1.0, "%.1f", -100.0, 100.0))
        engine.setSlope_dBPerDecade(slope);

    if (engine.sparamMode())
        ImGui::EndDisabled();

    if (ImGui::Button("Delete") && onRemoveNode)
        onRemoveNode(engine.graphNodeId());
}
```

- [ ] **Step 8: Create equalizer tests**

Create `tests/test_equalizer.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "equalizer_engine.h"
#include "signal_generator_engine.h"
#include "node_graph_engine.h"
#include <cmath>

using Catch::Approx;

TEST_CASE("Equalizer ideal mode applies flat gain", "[equalizer]") {
    NodeGraphEngine graph;
    EqualizerEngine eq(0, graph);
    eq.setRefGain_dB(10.0);

    SignalGeneratorEngine gen(1, graph);
    gen.addTone(100e6, -20.0);
    gen.update(0.0);

    eq.node().inputs[0] = &gen.node().outputs[0];
    eq.update(0.0);

    const auto& out = eq.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    REQUIRE(out.tones[0].power_dBm == Approx(-10.0).margin(0.01));
}

TEST_CASE("Equalizer ideal mode applies slope", "[equalizer]") {
    NodeGraphEngine graph;
    EqualizerEngine eq(0, graph);
    eq.setRefGain_dB(0.0);
    eq.setRefFreq_Hz(100e6);
    eq.setSlope_dBPerDecade(10.0); // +10 dB per decade

    SignalGeneratorEngine gen(1, graph);
    gen.addTone(100e6, -20.0);   // at ref freq: gain = 0 dB
    gen.addTone(1e9, -20.0);     // 1 decade up: gain = +10 dB
    gen.update(0.0);

    eq.node().inputs[0] = &gen.node().outputs[0];
    eq.update(0.0);

    const auto& out = eq.node().outputs[0];
    REQUIRE(out.tones.size() == 2);

    for (const auto& t : out.tones) {
        if (std::abs(t.freq_Hz - 100e6) < 1.0)
            REQUIRE(t.power_dBm == Approx(-20.0).margin(0.01));
        else if (std::abs(t.freq_Hz - 1e9) < 1.0)
            REQUIRE(t.power_dBm == Approx(-10.0).margin(0.01));
    }
}

TEST_CASE("Equalizer ideal mode applies ref gain + slope combined", "[equalizer]") {
    NodeGraphEngine graph;
    EqualizerEngine eq(0, graph);
    eq.setRefGain_dB(5.0);
    eq.setRefFreq_Hz(50e6);
    eq.setSlope_dBPerDecade(-6.0); // -6 dB/decade (falling)

    SignalGeneratorEngine gen(1, graph);
    gen.addTone(50e6, -30.0);       // ref: gain = +5 dB
    gen.addTone(500e6, -30.0);      // 1 decade up: gain = +5 + (-6) = -1 dB
    gen.update(0.0);

    eq.node().inputs[0] = &gen.node().outputs[0];
    eq.update(0.0);

    const auto& out = eq.node().outputs[0];
    REQUIRE(out.tones.size() == 2);

    for (const auto& t : out.tones) {
        if (std::abs(t.freq_Hz - 50e6) < 1.0)
            REQUIRE(t.power_dBm == Approx(-25.0).margin(0.01));
        else if (std::abs(t.freq_Hz - 500e6) < 1.0)
            REQUIRE(t.power_dBm == Approx(-31.0).margin(0.5));
    }
}

static std::string s2p_path() {
    return std::string(PROJECT_SOURCE_DIR) +
        "/amplifier/data_files/adm-8344psm-s_parameters/"
        "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
}

TEST_CASE("Equalizer S-param mode applies S21 gain", "[equalizer][sparam]") {
    NodeGraphEngine graph;
    EqualizerEngine eq(0, graph);
    eq.setSParamFilepath(s2p_path());

    REQUIRE(eq.sparamLoaded());
    REQUIRE(eq.sparamMode());

    SignalGeneratorEngine gen(1, graph);
    gen.addTone(1e9, -20.0);
    gen.update(0.0);

    eq.node().inputs[0] = &gen.node().outputs[0];
    eq.update(0.0);

    const auto& out = eq.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    auto S21 = eq.sparamData().interpolate(1e9, 2);
    double expected = 20.0 * std::log10(std::abs(S21));
    REQUIRE(out.tones[0].power_dBm == Approx(-20.0 + expected).margin(0.5));
}
```

Add to `tests/CMakeLists.txt`: add `test_equalizer.cpp` to sources, link `simulator::equalizer_engine`.

- [ ] **Step 9: Update root CMakeLists.txt**

Add `add_subdirectory("equalizer")` before `add_subdirectory("src")`.

- [ ] **Step 10: Build and run all tests**

```bash
cmake -S . -B build --fresh 2>&1 | tail -5
cmake --build build --config Debug 2>&1 | tail -20
cd build && ctest --test-dir build -V 2>&1 | tail -40
```
Expected: All tests pass. No SParamEngine references remain.
