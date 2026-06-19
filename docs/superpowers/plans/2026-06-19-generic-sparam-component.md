# Generic S-Parameter Component Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace `SParameterFilterEngine` and `SParameterAmplifierEngine` with a single unified `SParamEngine` that can represent any N-port device (filter, equalizer, attenuator, amplifier) with optional noise figure and nonlinearity.

**Architecture:** One `SParamEngine` class implementing `IComponentEngine` owns `SParameterData` for Touchstone file loading/interpolation, plus optional `NonlinearModel` and noise figure. All existing S-parameter behavior is preserved in a single module. The node graph context menu and inspector panel are simplified from two entries to one.

**Tech Stack:** C++20, CMake, Catch2, ImNodes, imgui

## Global Constraints

- C++20 standard enforced via `CMAKE_CXX_STANDARD 20`
- Engines must not include `<imgui.h>` or `<implot.h>`
- All public API surfaces must use the same type signatures as the components they replace
- `ComponentRegistry` template `add<T>(args...)` usage pattern unchanged
- `IComponentEngine` interface unchanged
- All existing tests must pass (updated to use `SParamEngine`)

---

### Task 1: Create unified `SParamEngine` module

**Files:**
- Create: `s_parametric_component/CMakeLists.txt`
- Create: `s_parametric_component/include/s_param_engine.h`
- Create: `s_parametric_component/src/s_param_engine.cpp`
- Modify: `tests/test_s_parameter_amplifier.cpp` — replace all `SParameterFilterEngine` / `SParameterAmplifierEngine` with `SParamEngine`

**Interfaces:**
- Consumes: `IComponentEngine`, `NodeGraphEngine`, `SParameterData`, `NonlinearModel`, `Spectrum`, `SignalNode` (all existing — no changes)
- Produces: `SParamEngine` class with same constructor signature pattern as old engines (`int id, NodeGraphEngine& graph, const std::string& filepath`)

- [ ] **Step 1: Create `s_parametric_component/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.20)
project(s_param_engine LANGUAGES CXX)

add_library(s_param_engine STATIC
    src/s_param_engine.cpp
)

target_include_directories(s_param_engine
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(s_param_engine
    PUBLIC common simulator::node_graph_engine simulator::touchstone_parser
)
add_library(simulator::s_param_engine ALIAS s_param_engine)
```

- [ ] **Step 2: Create `s_parametric_component/include/s_param_engine.h`**

```cpp
#pragma once

#include "common.h"
#include "component_interface.h"
#include "node_graph_engine.h"
#include "nonlinear_model.h"
#include "signal_node.h"
#include "s_parameter_data.h"
#include <string>

class SParamEngine : public IComponentEngine {
public:
    SParamEngine(int id, NodeGraphEngine& graph, const std::string& filepath);

    // IComponentEngine
    int id() const override { return m_id; }
    int graphNodeId() const override { return m_graph_node_id; }
    std::string hoverSummary() const override;
    int inputPinId() const override;
    int outputPinId() const override;
    SignalNode& node() override { return m_node; }
    const SignalNode& node() const override { return m_node; }
    void update(double dt) override;

    void reload(const std::string& filepath);

    // S-parameter access
    const std::string& filepath() const { return m_filepath; }
    bool loaded() const { return m_data.loaded(); }
    const SParameterData& data() const { return m_data; }
    int forwardParamIdx() const { return m_forward_param_idx; }
    void setForwardParamIdx(int idx);

    // Optional: noise figure (0.0 = off/passive)
    double nf_dB() const { return m_nf_dB; }
    void setNF_dB(double nf) {
        if (nf != m_nf_dB) { m_nf_dB = nf; m_dirty = true; }
    }

    // Optional: nonlinearity (disabled by default)
    bool enableNonlinear() const { return m_nonlinear.enabled(); }
    double oip2_dBm() const { return m_nonlinear.oip2_dBm(); }
    double oip3_dBm() const { return m_nonlinear.oip3_dBm(); }
    void setEnableNonlinear(bool en) {
        if (en != m_nonlinear.enabled()) {
            m_nonlinear.setEnabled(en);
            m_dirty = true;
        }
    }
    void setOIP2_dBm(double oip2) {
        m_nonlinear.setOIP2_dBm(oip2);
        if (m_nonlinear.enabled()) m_dirty = true;
    }
    void setOIP3_dBm(double oip3) {
        m_nonlinear.setOIP3_dBm(oip3);
        if (m_nonlinear.enabled()) m_dirty = true;
    }

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

- [ ] **Step 3: Create `s_parametric_component/src/s_param_engine.cpp`**

```cpp
#include "s_param_engine.h"
#include "common.h"
#include "logging_core.h"
#include <algorithm>
#include <cmath>

SParamEngine::SParamEngine(int id, NodeGraphEngine& graph,
                           const std::string& filepath)
    : m_id(id), m_graph(&graph), m_filepath(filepath) {
    m_graph_node_id = graph.addNode("S-Param " + std::to_string(id), &m_node, 1, 1);
    m_node.inputs.resize(1);
    m_node.outputs.resize(1);
    reload(filepath);
}

void SParamEngine::reload(const std::string& filepath) {
    m_filepath = filepath;
    m_forward_param_idx = 0;

    if (!m_data.load(filepath))
        return;

    int np = m_data.numPorts();
    m_forward_param_idx = (np > 1) ? np : 0; // S21 for 2-port, S11 for 1-port
    m_dirty = true;
    LOG_INFO("Loaded S-parameter component %d from %s (%zu points, %d ports)",
             m_id, filepath.c_str(), m_data.freqs().size(), np);
}

int SParamEngine::inputPinId() const {
    return m_graph ? m_graph->inputPinId(m_graph_node_id) : -1;
}

int SParamEngine::outputPinId() const {
    return m_graph ? m_graph->outputPinId(m_graph_node_id) : -1;
}

void SParamEngine::setForwardParamIdx(int idx) {
    int total = m_data.paramCount();
    if (idx >= 0 && idx < total && idx != m_forward_param_idx) {
        m_forward_param_idx = idx;
        m_dirty = true;
    }
}

void SParamEngine::update(double dt) {
    (void)dt;
    const Spectrum* in_ptr = m_node.inputs.empty() ? nullptr : m_node.inputs[0];
    if (!m_dirty && in_ptr == m_cached_input_ptr && (!in_ptr || in_ptr->generation == m_cached_input_generation))
        return;
    m_dirty = false;
    m_cached_input_ptr = in_ptr;
    if (in_ptr)
        m_cached_input_generation = in_ptr->generation;

    Spectrum empty;
    const Spectrum& in = in_ptr ? *in_ptr : empty;
    auto& out = m_node.outputs[0];

    // Apply S-parameter transfer
    m_data.applyToSpectrum(in, out, m_forward_param_idx);

    if (!m_data.loaded() || out.frequencies.empty()) {
        out.bumpGeneration();
        return;
    }

    const size_t N = out.frequencies.size();
    if (N < 2) {
        out.bumpGeneration();
        return;
    }

    // Add noise figure (if enabled via NF > 0.0)
    if (m_nf_dB > 0.0) {
        double Te = calculateNoiseTemp(m_nf_dB);
        out.noise_added_W.resize(N);
        for (size_t i = 0; i < N; ++i) {
            auto S = m_data.interpolate(out.frequencies[i], m_forward_param_idx);
            double gain_linear = std::norm(S);
            out.noise_added_W[i] = k * Te * gain_linear;
            out.noise_total_W[i] = out.noise_W[i] + out.noise_added_W[i];
        }
    }

    // Nonlinear processing (if enabled)
    if (m_nonlinear.enabled() && in_ptr && !in_ptr->tones.empty()) {
        size_t n_fund = out.tones.size();
        auto result = m_nonlinear.process(in_ptr->tones,
            [this](double freq) {
                auto S = this->m_data.interpolate(freq, m_forward_param_idx);
                return std::abs(S);
            });

        for (const auto& t : result.extra_tones)
            out.tones.push_back(t);

        if (result.compression_dB < -1e8) {
            for (size_t k = 0; k < n_fund; ++k)
                out.tones[k].power_dBm = MIN_POWER;
        } else {
            for (size_t k = 0; k < n_fund; ++k)
                out.tones[k].power_dBm += result.compression_dB;
        }
    }

    out.bumpGeneration();
}

std::string SParamEngine::hoverSummary() const {
    if (!m_data.loaded()) return "Not loaded";
    int np = m_data.numPorts();
    std::string s = std::to_string(np) + "-port | Forward: S"
        + std::to_string((m_forward_param_idx / np) + 1)
        + std::to_string((m_forward_param_idx % np) + 1);
    if (m_nf_dB > 0.0)
        s += " | NF: " + std::to_string(m_nf_dB) + " dB";
    if (m_nonlinear.enabled())
        s += " | OIP2: " + std::to_string(m_nonlinear.oip2_dBm())
           + " OIP3: " + std::to_string(m_nonlinear.oip3_dBm());
    return s;
}
```

- [ ] **Step 4: Update `tests/test_s_parameter_amplifier.cpp` to test `SParamEngine`**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "s_param_engine.h"
#include "signal_generator_engine.h"
#include "node_graph_engine.h"
#include <cmath>
#include <numbers>

using Catch::Approx;

TEST_CASE("SParamEngine loads real .s2p and applies frequency-dependent gain",
          "[sparam]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/amplifier/data_files/adm-8344psm-s_parameters/"
        "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    SParamEngine sp(0, graph, path);

    REQUIRE(sp.loaded());
    REQUIRE(sp.data().numPorts() == 2);
    REQUIRE(sp.data().freqs().size() > 10);
    REQUIRE(sp.data().params().size() == sp.data().freqs().size());
    REQUIRE(sp.data().params()[0].size() == 4); // 4 S-params for 2-port
    REQUIRE(sp.forwardParamIdx() == 2); // default S21

    // S21 should have > 10 dB gain at mid-band
    int mid = static_cast<int>(sp.data().freqs().size()) / 2;
    double s21_mag = std::abs(sp.data().params()[mid][2]); // idx 2 = S21
    REQUIRE(s21_mag > 3.0);
}

TEST_CASE("SParamEngine applies phase rotation to tones", "[sparam]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/amplifier/data_files/adm-8344psm-s_parameters/"
        "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    SParamEngine sp(0, graph, path);
    REQUIRE(sp.loaded());

    SignalGeneratorEngine gen(0, graph);
    gen.addTone(1e9, -20.0);
    gen.update(0.0);

    sp.node().inputs[0] = &gen.node().outputs[0];
    sp.update(0.0);

    const auto& out = sp.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    // Values from .s2p file line at 1 GHz: S21 = 19.588779 dB, 145.23813 deg
    // -20 dBm input + 19.588779 dB gain = -0.411221 dBm, phase rotated 145.23813 deg
    REQUIRE(out.tones[0].power_dBm == Approx(-0.411221).margin(0.5));
    REQUIRE(out.tones[0].phase_deg == Approx(145.23813).margin(1.0));
}

TEST_CASE("SParamEngine interpolates gain between data points", "[sparam]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/amplifier/data_files/adm-8344psm-s_parameters/"
        "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    SParamEngine sp(0, graph, path);
    REQUIRE(sp.loaded());

    SignalGeneratorEngine gen(0, graph);
    gen.addTone(1.005e9, -20.0);
    gen.update(0.0);

    sp.node().inputs[0] = &gen.node().outputs[0];
    sp.update(0.0);

    const auto& out = sp.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    REQUIRE(out.tones[0].power_dBm == Approx(-20.0 + 19.585).margin(0.5));
}

TEST_CASE("SParamEngine handles out-of-band frequency", "[sparam]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/amplifier/data_files/adm-8344psm-s_parameters/"
        "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    SParamEngine sp(0, graph, path);
    REQUIRE(sp.loaded());

    double f_outside = sp.data().freqs().back() + 10e9;
    Spectrum in_spec;
    in_spec.tones = {{f_outside, -20.0, 0.0}};
    sp.node().inputs[0] = &in_spec;
    sp.update(0.0);

    REQUIRE(sp.node().outputs[0].tones.size() == 1);
    // Should clamp to last point — phase should match last S21 phase
    auto S_last = sp.data().params().back()[2];
    double expected_phase = std::arg(S_last) * 180.0 / std::numbers::pi;
    REQUIRE(sp.node().outputs[0].tones[0].phase_deg == Approx(expected_phase).margin(0.1));
}

TEST_CASE("SParamEngine fails gracefully for bad file", "[sparam]") {
    NodeGraphEngine graph;
    SParamEngine sp(0, graph, "/nonexistent/file.s2p");
    REQUIRE(!sp.loaded());

    // update() should not crash
    sp.update(0.0);
}

TEST_CASE("SParamEngine reloads new file at runtime", "[sparam]") {
    NodeGraphEngine graph;
    SParamEngine sp(0, graph, "/nonexistent/file.s2p");
    REQUIRE(!sp.loaded());

    std::string valid_path = std::string(PROJECT_SOURCE_DIR) +
        "/amplifier/data_files/adm-8344psm-s_parameters/"
        "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    sp.reload(valid_path);
    REQUIRE(sp.loaded());
    REQUIRE(sp.data().numPorts() == 2);
    REQUIRE(sp.forwardParamIdx() == 2);
}

TEST_CASE("SParamEngine forward param index controls gain selection", "[sparam]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/amplifier/data_files/adm-8344psm-s_parameters/"
        "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    SParamEngine sp(0, graph, path);
    REQUIRE(sp.loaded());
    REQUIRE(sp.forwardParamIdx() == 2); // default S21

    // Switching to S11 (idx 0) should produce different gain
    sp.setForwardParamIdx(0);
    REQUIRE(sp.forwardParamIdx() == 0);

    double f_mid = (sp.data().freqs().front() + sp.data().freqs().back()) / 2.0;
    Spectrum in_spec;
    in_spec.tones = {{f_mid, -20.0, 0.0}};
    sp.node().inputs[0] = &in_spec;
    sp.update(0.0);
    REQUIRE(sp.node().outputs[0].tones.size() == 1);

    // S11 gain should be much lower than S21 (S11 is reflection, ~0 dB)
    REQUIRE(sp.node().outputs[0].tones[0].power_dBm < -15.0);
}

TEST_CASE("SParamEngine NF adds noise power correctly", "[sparam][nf]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/amplifier/data_files/adm-8344psm-s_parameters/"
        "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    SParamEngine sp(0, graph, path);
    REQUIRE(sp.loaded());

    // Default NF=0 — noise_added_W should be zero (passive behavior)
    SignalGeneratorEngine gen(0, graph);
    gen.update(0.0);
    sp.node().inputs[0] = &gen.node().outputs[0];
    sp.update(0.0);

    const auto& out0 = sp.node().outputs[0];
    REQUIRE(!out0.noise_added_W.empty());
    for (double n : out0.noise_added_W)
        REQUIRE(n == Approx(0.0).margin(1e-30));

    // NF = 3 dB — noise_added_W should be positive
    sp.setNF_dB(3.0);
    sp.update(0.0);
    const auto& out3 = sp.node().outputs[0];
    REQUIRE(!out3.noise_added_W.empty());
    bool any_positive = false;
    for (double n : out3.noise_added_W) {
        if (n > 0.0) { any_positive = true; break; }
    }
    REQUIRE(any_positive);
}

TEST_CASE("SParamEngine nonlinear disabled = no harmonics", "[sparam][nonlinear]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/amplifier/data_files/adm-8344psm-s_parameters/"
        "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    SParamEngine sp(0, graph, path);
    REQUIRE(sp.loaded());

    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, -20.0);
    gen.update(0.0);

    sp.setEnableNonlinear(false);
    sp.node().inputs[0] = &gen.node().outputs[0];
    sp.update(0.0);

    // Only the fundamental should be present
    const auto& out = sp.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    REQUIRE(out.tones[0].freq_Hz == 100e6);
}

TEST_CASE("SParamEngine generates harmonics when nonlinear enabled",
          "[sparam][nonlinear]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/amplifier/data_files/adm-8344psm-s_parameters/"
        "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    SParamEngine sp(0, graph, path);
    REQUIRE(sp.loaded());

    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, -40.0);
    gen.update(0.0);

    sp.setOIP2_dBm(40.0);
    sp.setOIP3_dBm(30.0);
    sp.setEnableNonlinear(true);
    sp.node().inputs[0] = &gen.node().outputs[0];
    sp.update(0.0);

    const auto& out = sp.node().outputs[0];
    // Fundamental + 2nd harmonic + 3rd harmonic = 3
    REQUIRE(out.tones.size() >= 3);

    bool found_fund = false, found_h2 = false, found_h3 = false;
    for (const auto& t : out.tones) {
        if (std::abs(t.freq_Hz - 100e6) < 1.0) found_fund = true;
        if (std::abs(t.freq_Hz - 200e6) < 1.0) found_h2 = true;
        if (std::abs(t.freq_Hz - 300e6) < 1.0) found_h3 = true;
    }
    REQUIRE(found_fund);
    REQUIRE(found_h2);
    REQUIRE(found_h3);
}

TEST_CASE("SParamEngine generates IMD products for two tones",
          "[sparam][nonlinear]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/amplifier/data_files/adm-8344psm-s_parameters/"
        "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    SParamEngine sp(0, graph, path);
    REQUIRE(sp.loaded());

    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, -30.0);
    gen.addTone(101e6, -30.0);
    gen.update(0.0);

    sp.setOIP2_dBm(40.0);
    sp.setOIP3_dBm(30.0);
    sp.setEnableNonlinear(true);
    sp.node().inputs[0] = &gen.node().outputs[0];
    sp.update(0.0);

    const auto& out = sp.node().outputs[0];
    REQUIRE(out.tones.size() > 4);

    bool found_im3_lower = false, found_im3_upper = false;
    for (const auto& t : out.tones) {
        if (std::abs(t.freq_Hz - 99e6) < 1.0) found_im3_lower = true;
        if (std::abs(t.freq_Hz - 102e6) < 1.0) found_im3_upper = true;
    }
    REQUIRE(found_im3_lower);
    REQUIRE(found_im3_upper);
}

TEST_CASE("SParamEngine shows compression at high input power",
          "[sparam][nonlinear]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/amplifier/data_files/adm-8344psm-s_parameters/"
        "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    SParamEngine sp(0, graph, path);
    REQUIRE(sp.loaded());

    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, 0.0);
    gen.update(0.0);

    // Low OIP3 to force compression
    sp.setOIP2_dBm(30.0);
    sp.setOIP3_dBm(10.0);
    sp.setEnableNonlinear(true);
    sp.node().inputs[0] = &gen.node().outputs[0];
    sp.update(0.0);

    const auto& out = sp.node().outputs[0];
    REQUIRE(out.tones.size() >= 1);
    REQUIRE(out.tones[0].power_dBm < 15.0);
}

TEST_CASE("SParamEngine default is passive (no NF, no nonlinear)", "[sparam]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/amplifier/data_files/adm-8344psm-s_parameters/"
        "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    SParamEngine sp(0, graph, path);
    REQUIRE(sp.loaded());

    // Default state: NF=0, nonlinear disabled
    REQUIRE(sp.nf_dB() == Approx(0.0));
    REQUIRE(!sp.enableNonlinear());

    // Apply a tone — should behave as pure S-parameter passive
    int mid = static_cast<int>(sp.data().freqs().size()) / 2;
    double f_mid = sp.data().freqs()[mid];
    Spectrum in_spec;
    in_spec.tones = {{f_mid, -20.0, 0.0}};
    sp.node().inputs[0] = &in_spec;
    sp.update(0.0);

    const auto& out = sp.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    // no added noise (NF=0)
    if (!out.noise_added_W.empty()) {
        for (double n : out.noise_added_W)
            REQUIRE(n == Approx(0.0));
    }
}
```

- [ ] **Step 5: Run the tests to verify they fail (no SParamEngine yet)**

Run: `cd build && cmake .. -G Ninja && ninja tests && ctest --test-dir . --output-on-failure 2>&1 | head -80`
Expected: Build fails — `s_param_engine.h` not found, or linking errors.

- [ ] **Step 6: Build and run tests to verify they pass**
```
cd build && cmake .. -G Ninja && ninja tests && ctest --test-dir . --output-on-failure
```
Expected: All tests compile and pass (the new `SParamEngine` + its unit tests).

- [ ] **Step 7: Commit**

```bash
git add s_parametric_component/
git add tests/test_s_parameter_amplifier.cpp
git rm -r s_parameter_filter/
git rm -r s_parameter_amplifier/
git add -u
git commit -m "feat: replace SParamFilter/SParamAmp with unified SParamEngine"
```

---

### Task 2: Wire unified engine into app and UI

**Files:**
- Modify: `CMakeLists.txt` (root) — swap subdirectories
- Modify: `node_graph/include/node_graph_widget.h` — replace callbacks
- Modify: `node_graph/src/node_graph_widget.cpp` — replace context menu entries
- Modify: `app/include/app.h` — update include
- Modify: `app/src/app.cpp` — update callback
- Modify: `app/include/inspector_panel.h` — update enum + method
- Modify: `app/src/inspector_panel.cpp` — update `findSelected`, `labelForHit`, replace draw methods
- Modify: `tests/CMakeLists.txt` — swap linked targets

**Interfaces:**
- Consumes: `SParamEngine` from Task 1
- Produces: Updated app/UI that uses `SParamEngine` everywhere

- [ ] **Step 1: Update root `CMakeLists.txt`**

Replace:
```cmake
add_subdirectory("s_parameter_amplifier")
...
add_subdirectory("s_parameter_filter")
```
With:
```cmake
add_subdirectory("s_parametric_component")
```

- [ ] **Step 2: Update `node_graph/include/node_graph_widget.h`**

Replace `onAddSParamFilter` and `onAddSParamAmp` with single `onAddSParamComponent`:
```cpp
// Before:
std::function<void()> onAddSParamAmp;
std::function<void()> onAddSParamFilter;

// After:
std::function<void()> onAddSParamComponent;
```

- [ ] **Step 3: Update `node_graph/src/node_graph_widget.cpp`**

In `handleContextMenu`, replace:
```cpp
if (ImGui::MenuItem("Add S-Param Amp")) {
    if (onAddSParamAmp) onAddSParamAmp();
}
if (ImGui::MenuItem("Add S-Param Filter")) {
    if (onAddSParamFilter) onAddSParamFilter();
}
```
With:
```cpp
if (ImGui::MenuItem("Add S-Param Component")) {
    if (onAddSParamComponent) onAddSParamComponent();
}
```

- [ ] **Step 4: Update `app/include/app.h`**

Replace:
```cpp
#include "s_parameter_amplifier_engine.h"
#include "s_parameter_filter_engine.h"
```
With:
```cpp
#include "s_param_engine.h"
```

- [ ] **Step 5: Update `app/src/app.cpp`**

Replace the two callback bindings:
```cpp
// Remove these two:
m_graph_widget->onAddSParamAmp = [this]() {
    m_components.add<SParameterAmplifierEngine>(m_next_component_id++, m_graph_engine,
        std::string(PROJECT_SOURCE_DIR) + "/amplifier/data_files/adm-8344psm-s_parameters/ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p");
};
m_graph_widget->onAddSParamFilter = [this]() {
    m_components.add<SParameterFilterEngine>(m_next_component_id++, m_graph_engine, "");
};

// Add:
m_graph_widget->onAddSParamComponent = [this]() {
    m_components.add<SParamEngine>(m_next_component_id++, m_graph_engine, "");
};
```

- [ ] **Step 6: Update `app/include/inspector_panel.h`**

In the `ComponentType` enum, replace `SParamAmp, SParamFilter` with `SParam`.

Replace method declarations:
```cpp
// Remove:
void drawSParamAmpProperties(SParameterAmplifierEngine& engine, int index);
void drawSParamFilterProperties(SParameterFilterEngine& engine, int index);

// Add:
void drawSParamProperties(SParamEngine& engine, int index);
```

Remove forward declarations of `SParameterAmplifierEngine` and `SParameterFilterEngine`; add `SParamEngine`.

- [ ] **Step 7: Update `app/src/inspector_panel.cpp`**

**Include change:** Replace:
```cpp
#include "s_parameter_amplifier_engine.h"
#include "s_parameter_filter_engine.h"
```
With:
```cpp
#include "s_param_engine.h"
```

**`findSelected()` change:** Replace the two `dynamic_cast` lines:
```cpp
// Remove:
else if (dynamic_cast<SParameterAmplifierEngine*>(engine))   return {ComponentType::SParamAmp, engine};
else if (dynamic_cast<SParameterFilterEngine*>(engine))      return {ComponentType::SParamFilter, engine};

// Add:
else if (dynamic_cast<SParamEngine*>(engine))                return {ComponentType::SParam, engine};
```

**`labelForHit()` change:** Replace the two S-param label cases:
```cpp
// Remove:
case ComponentType::SParamAmp:     return "S-Param Amp " + std::to_string(hit.engine->id());
case ComponentType::SParamFilter:  return "S-Param Filter " + std::to_string(hit.engine->id());

// Add:
case ComponentType::SParam:        return "S-Param " + std::to_string(hit.engine->id());
```

**Switch in `draw()`:** Replace the two cases:
```cpp
// Remove:
case ComponentType::SParamAmp:
    drawSParamAmpProperties(*static_cast<SParameterAmplifierEngine*>(hit.engine), hit.engine->id());
    break;
case ComponentType::SParamFilter:
    drawSParamFilterProperties(*static_cast<SParameterFilterEngine*>(hit.engine), hit.engine->id());
    break;

// Add:
case ComponentType::SParam:
    drawSParamProperties(*static_cast<SParamEngine*>(hit.engine), hit.engine->id());
    break;
```

**Replace the two old draw methods with one unified method:**

```cpp
void InspectorPanel::drawSParamProperties(SParamEngine& engine, int index) {
    (void)index;
    if (!engine.loaded()) {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "Failed to load S-parameter file");
    }

    ImGui::TextWrapped("File: %s", engine.filepath().c_str());
    if (ImGui::Button("Browse...")) {
        auto result = pfd::open_file("Select S-parameter file", "",
                                     {"S-parameter Files", "*.s2p *.s3p *.s4p *.sNp"})
                          .result();
        if (!result.empty()) {
            engine.reload(result[0]);
            LOG_INFO("S-param component reloaded: %s", result[0].c_str());
        }
    }

    if (engine.loaded()) {
        int np = engine.data().numPorts();
        int fwd_idx = engine.forwardParamIdx();
        std::string preview =
            "S" + std::to_string((fwd_idx / np) + 1) + std::to_string((fwd_idx % np) + 1);
        if (ImGui::BeginCombo("Forward Param", preview.c_str())) {
            for (int pi = 0; pi < np * np; ++pi) {
                std::string lbl =
                    "S" + std::to_string((pi / np) + 1) + std::to_string((pi % np) + 1);
                if (ImGui::Selectable(lbl.c_str(), pi == fwd_idx))
                    engine.setForwardParamIdx(pi);
            }
            ImGui::EndCombo();
        }

        ImGui::Text("Ports: %d | Data points: %zu", np, engine.data().freqs().size());
        ImGui::Text("Max freq: %.0f MHz", engine.data().freqs().back() / 1e6);
    }

    // Optional: Noise Figure
    bool has_nf = (engine.nf_dB() > 0.0);
    if (ImGui::Checkbox("Noise Figure", &has_nf)) {
        engine.setNF_dB(has_nf ? 3.0 : 0.0);
    }
    if (has_nf) {
        double nf = engine.nf_dB();
        if (utils::inputDouble("NF (dB)", nf, 0.1, 10, "%.1f", 0.0, 30.0))
            engine.setNF_dB(nf);
    }

    // Optional: Nonlinearity
    bool nonlin = engine.enableNonlinear();
    if (ImGui::Checkbox("Enable Nonlinearity", &nonlin))
        engine.setEnableNonlinear(nonlin);

    if (nonlin) {
        double oip2 = engine.oip2_dBm();
        if (utils::inputDouble("OIP2 (dBm)", oip2, 1, 10, "%.1f", -100.0, 200.0))
            engine.setOIP2_dBm(oip2);

        double oip3 = engine.oip3_dBm();
        if (utils::inputDouble("OIP3 (dBm)", oip3, 1, 10, "%.1f", -100.0, 200.0))
            engine.setOIP3_dBm(oip3);

        double p1dB_est = oip3 - 10.0;
        ImGui::TextDisabled("P1dB ~ %.1f dBm", p1dB_est);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Estimated 1 dB compression point");
    }

    if (ImGui::Button("Delete") && onRemoveNode)
        onRemoveNode(engine.graphNodeId());
}
```

- [ ] **Step 8: Update `tests/CMakeLists.txt`**

Replace:
```cmake
    simulator::s_parameter_amplifier_engine
    simulator::s_parameter_filter_engine
```
With:
```cmake
    simulator::s_param_engine
```

- [ ] **Step 9: Build and run all tests**

```bash
cd build && cmake .. -G Ninja && ninja tests && ctest --test-dir . --output-on-failure
```
Expected: Clean build, all 73+ tests pass. No references to old component types.

- [ ] **Step 10: Commit**

```bash
git add CMakeLists.txt
git add app/
git add node_graph/
git add tests/CMakeLists.txt
git commit -m "feat: wire SParamEngine into app, inspector, node graph"
```
