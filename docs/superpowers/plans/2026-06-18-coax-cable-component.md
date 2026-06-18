# Coax Cable Component Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a new `CoaxCableEngine` to the RF Simulator signal-chain, applying frequency-dependent insertion loss and phase shift to a `Spectrum`, parameterised by a MilTech-family preset, a length in metres, and an optional connector loss in dB. UI is rendered in `InspectorPanel` when the user selects the node. No new third-party dependencies.

**Architecture:** A new `coax/` module hosting `coax_cable_engine.{h,cpp}` (DSP only) and `coax_presets.h` (header-only data). The engine is added to the chain via the existing `ComponentRegistry` (mirroring the amplifier). Inspector properties are drawn by a new `InspectorPanel::drawCoaxCableProperties` method. A new `onAddCoaxCable` callback on `NodeGraphWidget` adds a menu entry, matching the pattern of the other `onAdd*` callbacks. Follows TDD throughout; ~12 unit tests verify the loss formula against the MilTech 340 datasheet plus behavioural coverage.

**Tech Stack:** C++20, Catch2 v3.4.0, ImGui (widget UI), Catch2::Catch2WithMain test runner. No new third-party deps.

**Specification:** `docs/superpowers/specs/2026-06-18-coax-cable-component-design.md`.

## Deviations from spec

The approved spec described a separate `coax_cable_widget.h/.cpp` library, but the **current code** in `app/src/inspector_panel.cpp` puts per-component UI inside `InspectorPanel::drawXxxProperties` methods (the amplifier has no widget library; the legacy `signal_generator_widget.cpp` is still linked but not rendered). This plan follows the current pattern: engine only, UI in InspectorPanel. The spec's other elements (preset table, processing model, datasheet parameters, node-graph menu, test list) are preserved verbatim.

## Global Constraints

- **C++20** standard. Compile flags inherited from top-level `CMakeLists.txt`.
- **Frequency in formula**: f in **MHz** (per datasheet `IL = (K1·√f + K2·f)·L`).
- **Length in metres only** — no unit toggle in this version.
- **Fidelity B**: loss + phase shift; **no thermal noise** added by the cable.
- **Impedance**: 50 Ω matched two-port; no VSWR / S11 / S22 in this version.
- **Default preset index**: 4 (MT 340). The presets array is fixed-size (`std::array<CableSpec, 6>`); indices 0–3 and 5 are stubs (K1=K2=delay=0) until populated from each cable's individual datasheet.
- **Engine name in graph**: `"Coax Cable <N>"`, matching `Amplifier N` / `Splitter N`.
- **All code in existing C++ style**: 4-space indent, 100-col line width, `PointerAlignment: Right` per `.clang-format`. Run `clang-format -i <file>` on every changed `.cpp`/`.h` before committing.
- **Test float comparisons** with `Catch::Approx` from `<catch2/catch_approx.hpp>`.
- **Do not commit documentation files** (`docs/**/*.md`); per the user preference, specs and plans stay on disk only.

## File Structure

```
coax/                                 # new
├── CMakeLists.txt                    # new
├── include/
│   ├── coax_presets.h                # new — CableSpec + kCoaxCablePresets
│   └── coax_cable_engine.h           # new — IComponentEngine impl
└── src/
    └── coax_cable_engine.cpp         # new

tests/
└── test_coax_cable_engine.cpp        # new — Catch2 unit tests

tests/CMakeLists.txt                  # modified — add test source + link

app/include/inspector_panel.h         # modified — add drawCoaxCableProperties
app/src/inspector_panel.cpp           # modified — dispatch + draw method
node_graph/include/node_graph_widget.h   # modified — add onAddCoaxCable callback
node_graph/src/node_graph_widget.cpp  # modified — add menu entry
app/src/app.cpp                       # modified — wire onAddCoaxCable
app/CMakeLists.txt                    # modified — link coax_cable_engine
CMakeLists.txt                        # modified — add_subdirectory(coax)
```

---

## Task 1: Preset data header with well-formedness test

**Files:**
- Create: `coax/include/coax_presets.h`
- Create: `tests/test_coax_cable_presets.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `<array>` (standard library)
- Produces: `struct CableSpec { const char* name; double K1_dB_per_m; double K2_dB_per_m; double delay_ns_per_m; double max_freq_GHz; double diameter_mm; }` and `inline const std::array<CableSpec, 6> kCoaxCablePresets`

- [ ] **Step 1: Write the failing test**

Create `tests/test_coax_cable_presets.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "coax_presets.h"

using Catch::Approx;

TEST_CASE("Coax preset table has six MilTech entries", "[coax][presets]") {
    REQUIRE(kCoaxCablePresets.size() == 6);
    REQUIRE(std::string(kCoaxCablePresets[0].name) == "MT 210");
    REQUIRE(std::string(kCoaxCablePresets[1].name) == "MT 230");
    REQUIRE(std::string(kCoaxCablePresets[2].name) == "MT 265");
    REQUIRE(std::string(kCoaxCablePresets[3].name) == "MT 300");
    REQUIRE(std::string(kCoaxCablePresets[4].name) == "MT 340");
    REQUIRE(std::string(kCoaxCablePresets[5].name) == "MT 480");
}

TEST_CASE("MT 340 preset matches datasheet", "[coax][presets]") {
    const CableSpec& mt340 = kCoaxCablePresets[4];
    REQUIRE(mt340.K1_dB_per_m == Approx(0.004710));
    REQUIRE(mt340.K2_dB_per_m == Approx(0.000004));
    REQUIRE(mt340.delay_ns_per_m == Approx(0.4));
    REQUIRE(mt340.max_freq_GHz == Approx(18.5));
    REQUIRE(mt340.diameter_mm == Approx(8.6));
}
```

- [ ] **Step 2: Add to tests/CMakeLists.txt**

In `tests/CMakeLists.txt`, append `test_coax_cable_presets.cpp` to `TEST_SOURCES` and add the link. The new `TEST_SOURCES` becomes:

```cmake
set(TEST_SOURCES
    test_main.cpp
    test_node_graph_engine.cpp
    test_touchstone.cpp
    test_s_parameter_amplifier.cpp
    test_adc.cpp
    test_pfb.cpp
    test_bench_dsp.cpp
    test_ideal_filter.cpp
    test_component_registry.cpp
    test_coax_cable_presets.cpp
)
```

`target_link_libraries(tests PRIVATE ...)` gets a new line after the `simulator::pfb_channelizer_engine` line:

```cmake
    simulator::coax_cable_engine
```

(Added in Task 2; the link will resolve once Task 2's target exists. If a parallel build complains, run the test after Task 2 is complete.)

- [ ] **Step 3: Build to confirm test fails to compile (header missing)**

Run:
```bash
cmake --build build
```

Expected: a compile error `fatal error: 'coax_presets.h' file not found` (header does not exist yet).

- [ ] **Step 4: Write `coax/include/coax_presets.h`**

```cpp
#pragma once

#include <array>
#include <cstddef>

struct CableSpec {
    const char* name;          // e.g. "MT 340"
    double K1_dB_per_m;        // sqrt(f) coefficient, dB/m with f in MHz
    double K2_dB_per_m;        // f coefficient, dB/m with f in MHz
    double delay_ns_per_m;     // signal propagation delay
    double max_freq_GHz;       // upper limit of the datasheet model
    double diameter_mm;        // informational; for widget display
};

inline const std::array<CableSpec, 6> kCoaxCablePresets = {{
    // name,      K1,        K2,        delay, max_f,  diam
    {"MT 210",  0.0,       0.0,       0.0,   0.0,    0.0},   // TODO: populate from MT 210 datasheet
    {"MT 230",  0.0,       0.0,       0.0,   0.0,    0.0},   // TODO: populate from MT 230 datasheet
    {"MT 265",  0.0,       0.0,       0.0,   0.0,    0.0},   // TODO: populate from MT 265 datasheet
    {"MT 300",  0.0,       0.0,       0.0,   0.0,    0.0},   // TODO: populate from MT 300 datasheet
    {"MT 340",  0.004710,  0.000004,  0.4,   18.5,   8.6},
    {"MT 480",  0.0,       0.0,       0.0,   0.0,    0.0},   // TODO: populate from MT 480 datasheet
}};
```

- [ ] **Step 5: Build & run test to confirm it passes**

Run:
```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "Coax preset"
```

Expected: 2 tests pass. (The link in `tests/CMakeLists.txt` will fail until Task 2 creates the `coax_cable_engine` target — this is expected; the test for presets does not need that target. If CMake complains, temporarily remove the `simulator::coax_cable_engine` line from `tests/CMakeLists.txt` for this task and re-add it in Task 2. Alternatively, complete Tasks 1 and 2 in the same commit, as listed in the commit step below.)

- [ ] **Step 6: Commit**

```bash
git add coax/include/coax_presets.h tests/test_coax_cable_presets.cpp tests/CMakeLists.txt
git commit -m "feat(coax): add CableSpec and MilTech preset table"
```

---

## Task 2: Engine class skeleton with pass-through update

**Files:**
- Create: `coax/include/coax_cable_engine.h`
- Create: `coax/src/coax_cable_engine.cpp`
- Create: `coax/CMakeLists.txt`
- Modify: `CMakeLists.txt`
- Modify: `app/CMakeLists.txt`

**Interfaces:**
- Consumes: `IComponentEngine` from `common/component_interface.h`; `NodeGraphEngine` from `node_graph_engine.h`; `Spectrum` from `common/spectrum.h`; `CableSpec` from `coax_presets.h`
- Produces: `class CoaxCableEngine : public IComponentEngine` with `id()`, `graphNodeId()`, `inputPinId()`, `outputPinId()`, `node()`, `hoverSummary()`, `update(double)`; setters `setPresetIndex(int)`, `setLengthM(double)`, `setConnectorsLossDB(double)`; getters `presetIndex()`, `lengthM()`, `connectorsLossDB()`, `preset()`.

- [ ] **Step 1: Write `coax/include/coax_cable_engine.h`**

```cpp
#pragma once

#include "common.h"
#include "component_interface.h"
#include "coax_presets.h"
#include "node_graph_engine.h"
#include "signal_node.h"
#include <string>

class CoaxCableEngine : public IComponentEngine {
  public:
    CoaxCableEngine(int id, NodeGraphEngine& graph);

    int id() const override { return m_id; }
    int graphNodeId() const override { return m_graph_node_id; }
    int inputPinId() const override;
    int outputPinId() const override;

    void update(double dt) override;

    SignalNode& node() override { return m_node; }
    const SignalNode& node() const override { return m_node; }

    std::string hoverSummary() const override;

    void setPresetIndex(int idx);
    void setLengthM(double m);
    void setConnectorsLossDB(double db);

    int presetIndex() const { return m_preset_index; }
    double lengthM() const { return m_length_m; }
    double connectorsLossDB() const { return m_connectors_loss_dB; }
    const CableSpec& preset() const { return kCoaxCablePresets[m_preset_index]; }

  private:
    int m_id;
    int m_graph_node_id = -1;
    NodeGraphEngine* m_graph = nullptr;

    SignalNode m_node;                  // 1 input, 1 output
    int m_preset_index = 4;             // default to MT 340
    double m_length_m = 1.0;
    double m_connectors_loss_dB = 0.0;
    bool m_dirty = true;
    const Spectrum* m_cached_input_ptr = nullptr;
    uint64_t m_cached_input_generation = 0;
    bool m_warned_above_max = false;    // rate-limit flag for over-max_freq warning
};
```

- [ ] **Step 2: Write `coax/src/coax_cable_engine.cpp` (pass-through `update()` only)**

```cpp
#include "coax_cable_engine.h"
#include "logging_core.h"
#include <algorithm>
#include <cmath>

CoaxCableEngine::CoaxCableEngine(int id, NodeGraphEngine& graph)
    : m_id(id), m_graph(&graph) {
    m_graph_node_id = graph.addNode("Coax Cable " + std::to_string(id), &m_node, 1, 1);
    m_node.inputs.resize(1);
    m_node.outputs.resize(1);
}

int CoaxCableEngine::inputPinId() const {
    return m_graph ? m_graph->inputPinId(m_graph_node_id) : -1;
}

int CoaxCableEngine::outputPinId() const {
    return m_graph ? m_graph->outputPinId(m_graph_node_id) : -1;
}

void CoaxCableEngine::setPresetIndex(int idx) {
    if (idx < 0 || static_cast<size_t>(idx) >= kCoaxCablePresets.size()) return;
    if (idx != m_preset_index) {
        m_preset_index = idx;
        m_dirty = true;
        m_warned_above_max = false;  // reset warn flag on preset change
    }
}

void CoaxCableEngine::setLengthM(double m) {
    double clamped = std::clamp(m, 0.0, 1000.0);
    if (clamped != m_length_m) {
        m_length_m = clamped;
        m_dirty = true;
    }
}

void CoaxCableEngine::setConnectorsLossDB(double db) {
    if (db != m_connectors_loss_dB) {
        m_connectors_loss_dB = db;
        m_dirty = true;
    }
}

void CoaxCableEngine::update(double dt) {
    (void)dt;
    const Spectrum* in_ptr = m_node.inputs.empty() ? nullptr : m_node.inputs[0];
    if (!m_dirty && in_ptr == m_cached_input_ptr &&
        (!in_ptr || in_ptr->generation == m_cached_input_generation)) {
        return;
    }
    m_dirty = false;
    m_cached_input_ptr = in_ptr;
    if (in_ptr) m_cached_input_generation = in_ptr->generation;

    auto& out = m_node.outputs[0];

    // Pass-through: copy frequencies, tones, phase, noise from input. The
    // frequency-dependent loss and phase shift are added in Task 3+.
    if (in_ptr && !in_ptr->frequencies.empty()) {
        out.frequencies = in_ptr->frequencies;
    } else if (out.frequencies.size() < 2) {
        buildDefaultFrequencyGrid(out.frequencies);
    }

    out.tones = in_ptr ? in_ptr->tones : std::vector<Spectrum::Tone>{};

    const size_t N = out.frequencies.size();
    if (in_ptr && !in_ptr->phase_deg.empty()) {
        out.phase_deg = in_ptr->phase_deg;
    } else {
        out.phase_deg.assign(N, 0.0);
    }

    out.noise_W.assign(N, 0.0);
    for (size_t i = 0; i < N; ++i) {
        double nin = (in_ptr && i < in_ptr->noise_total_W.size()) ? in_ptr->noise_total_W[i] : 0.0;
        out.noise_W[i] = nin;  // pass-through; scaling added in Task 4
    }
    out.noise_added_W.assign(N, 0.0);
    out.noise_total_W.resize(N);
    for (size_t i = 0; i < N; ++i) {
        out.noise_total_W[i] = out.noise_W[i] + out.noise_added_W[i];
    }

    out.bumpGeneration();
}

std::string CoaxCableEngine::hoverSummary() const {
    return std::string("Coax Cable: ") + preset().name +
           " | L=" + std::to_string(m_length_m) + " m";
}
```

- [ ] **Step 3: Write `coax/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.20)
project(coax LANGUAGES CXX)

# ---- Engine (DSP only — UI lives in InspectorPanel) ----
add_library(coax_cable_engine STATIC
    src/coax_cable_engine.cpp
)

target_include_directories(coax_cable_engine
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(coax_cable_engine
    PUBLIC
        common
        simulator::node_graph_engine
        simulator::logging_core
)

add_library(simulator::coax_cable_engine ALIAS coax_cable_engine)
```

- [ ] **Step 4: Add `add_subdirectory(coax)` to top-level `CMakeLists.txt`**

Insert this line into the block of `add_subdirectory(...)` calls (alphabetical, between `adc` and `core`):

```cmake
add_subdirectory("coax")
```

- [ ] **Step 5: Add `simulator::coax_cable_engine` to `app/CMakeLists.txt`**

In `app/CMakeLists.txt`, append a new entry inside `target_link_libraries(app PUBLIC ...)`:

```cmake
        simulator::coax_cable_engine
```

After this, the test link in `tests/CMakeLists.txt` (Task 1, Step 2) will resolve.

- [ ] **Step 6: Build to confirm the engine compiles**

Run:
```bash
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc
cmake --build build
```

Expected: clean build.

- [ ] **Step 7: Commit**

```bash
git add coax/CMakeLists.txt coax/include/coax_cable_engine.h coax/src/coax_cable_engine.cpp \
        CMakeLists.txt app/CMakeLists.txt
git commit -m "feat(coax): add CoaxCableEngine with pass-through update"
```

---

## Task 3: Per-tone insertion loss with datasheet spot-checks

**Files:**
- Modify: `coax/src/coax_cable_engine.cpp`
- Create: `tests/test_coax_cable_engine.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: the pass-through `update()` from Task 2
- Produces: per-tone insertion loss `IL(f) = (K1·√(f_MHz) + K2·f_MHz)·L + connectors_loss`; frequency clamping at `[1 Hz, preset.max_freq_GHz·1e9]`; rate-limited `LOG_WARN` when an out-of-band tone is processed

- [ ] **Step 1: Write the failing test in `tests/test_coax_cable_engine.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "coax_cable_engine.h"
#include "signal_generator_engine.h"
#include "node_graph_engine.h"

using Catch::Approx;

namespace {
// Drive a single tone through the cable; return the output tone power.
double runOneTone(double freq_Hz, double power_dBm, double length_m,
                  int preset_index, double conn_loss_dB = 0.0) {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(freq_Hz, power_dBm);
    gen.update(0.0);

    CoaxCableEngine cable(0, graph);
    cable.setPresetIndex(preset_index);
    cable.setLengthM(length_m);
    cable.setConnectorsLossDB(conn_loss_dB);
    cable.node().inputs[0] = &gen.node().outputs[0];
    cable.update(0.0);

    if (cable.node().outputs[0].tones.empty()) return -1e9;
    return cable.node().outputs[0].tones[0].power_dBm;
}
} // namespace

TEST_CASE("Coax cable loss matches MilTech 340 datasheet at 100 m", "[coax][datasheet]") {
    const int MT340 = 4;
    const double L = 100.0;

    REQUIRE(runOneTone(500e6,  0.0, L, MT340) == Approx(-10.7).margin(0.1));
    REQUIRE(runOneTone(2e9,    0.0, L, MT340) == Approx(-21.9).margin(0.1));
    REQUIRE(runOneTone(6e9,    0.0, L, MT340) == Approx(-39.6).margin(0.1));
    REQUIRE(runOneTone(10e9,   0.0, L, MT340) == Approx(-51.4).margin(0.1));
    REQUIRE(runOneTone(18e9,   0.0, L, MT340) == Approx(-70.9).margin(0.1));
}
```

- [ ] **Step 2: Add to `tests/CMakeLists.txt`**

Append `test_coax_cable_engine.cpp` to `TEST_SOURCES`:

```cmake
set(TEST_SOURCES
    ...
    test_coax_cable_presets.cpp
    test_coax_cable_engine.cpp
)
```

- [ ] **Step 3: Build & run the new test to confirm it fails**

Run:
```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "Coax cable loss matches"
```

Expected: FAIL — output tone power is still `0.0` (the pass-through `update()` doesn't subtract loss).

- [ ] **Step 4: Implement per-tone loss in `CoaxCableEngine::update()`**

In `coax/src/coax_cable_engine.cpp`, **replace** the block:

```cpp
    out.tones = in_ptr ? in_ptr->tones : std::vector<Spectrum::Tone>{};
```

with:

```cpp
    out.tones = in_ptr ? in_ptr->tones : std::vector<Spectrum::Tone>{};

    const CableSpec& p = preset();
    const double max_f_Hz = p.max_freq_GHz * 1e9;
    for (auto& t : out.tones) {
        const double f_Hz_raw = std::abs(t.freq_Hz);
        const double f_Hz = std::clamp(f_Hz_raw, 1.0, max_f_Hz);
        if (f_Hz_raw > max_f_Hz && !m_warned_above_max) {
            LOG_WARN("Coax cable %d: tone at %.3e Hz exceeds preset %s max freq (%.3e Hz); clamping.",
                     m_id, f_Hz_raw, p.name, max_f_Hz);
            m_warned_above_max = true;
        }
        const double f_MHz = f_Hz / 1e6;
        const double loss_dB =
            (p.K1_dB_per_m * std::sqrt(f_MHz) + p.K2_dB_per_m * f_MHz) * m_length_m
            + m_connectors_loss_dB;
        t.power_dBm -= loss_dB;
    }
```

Also add `#include "logging_core.h"` (already present) and `<cmath>` (already present).

- [ ] **Step 5: Build & run tests to confirm they pass**

Run:
```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "Coax cable"
```

Expected: all five datasheet cases pass within ±0.1 dB.

- [ ] **Step 6: Commit**

```bash
git add coax/src/coax_cable_engine.cpp tests/test_coax_cable_engine.cpp tests/CMakeLists.txt
git commit -m "feat(coax): per-tone insertion loss with MilTech 340 datasheet check"
```

---

## Task 4: Per-bin noise scaling by inverse linear loss

**Files:**
- Modify: `coax/src/coax_cable_engine.cpp`
- Modify: `tests/test_coax_cable_engine.cpp`

**Interfaces:**
- Consumes: per-tone loss from Task 3
- Produces: per-bin `out.noise_W[i] = in.noise_total_W[i] / dbToLinear(loss_dB_at_f_bin)`; `noise_added_W[i] = 0.0`; `noise_total_W[i] = out.noise_W[i]`

- [ ] **Step 1: Append failing tests to `tests/test_coax_cable_engine.cpp`**

```cpp
TEST_CASE("Coax cable scales per-bin noise by 1/L_linear", "[coax][noise]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.update(0.0);
    const auto& in = gen.node().outputs[0];

    CoaxCableEngine cable(0, graph);
    cable.setPresetIndex(4);             // MT 340
    cable.setLengthM(10.0);
    cable.node().inputs[0] = &gen.node().outputs[0];
    cable.update(0.0);

    const auto& out = cable.node().outputs[0];
    REQUIRE(out.noise_total_W.size() == in.noise_total_W.size());

    for (size_t i = 0; i < out.frequencies.size(); ++i) {
        const double f_Hz = std::abs(out.frequencies[i]);
        const double f_Hz_clamped = std::clamp(f_Hz, 1.0, 18.5e9);
        const double f_MHz = f_Hz_clamped / 1e6;
        const double loss_dB = (0.004710 * std::sqrt(f_MHz) + 0.000004 * f_MHz) * 10.0;
        const double L_lin = dbToLinear(loss_dB);
        REQUIRE(out.noise_W[i] == Approx(in.noise_total_W[i] / L_lin).epsilon(1e-30));
        REQUIRE(out.noise_added_W[i] == Approx(0.0));
        REQUIRE(out.noise_total_W[i] == Approx(out.noise_W[i]).epsilon(1e-30));
    }
}

TEST_CASE("Coax cable does not add thermal noise (fidelity B)", "[coax][noise]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.update(0.0);

    CoaxCableEngine cable(0, graph);
    cable.setPresetIndex(4);
    cable.setLengthM(2.0);
    cable.node().inputs[0] = &gen.node().outputs[0];
    cable.update(0.0);

    for (double d : cable.node().outputs[0].noise_added_W) {
        REQUIRE(d == Approx(0.0));
    }
}
```

- [ ] **Step 2: Build & run to confirm they fail**

Run:
```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "Coax cable scales per-bin noise"
```

Expected: FAIL — pass-through copies `in.noise_total_W` directly instead of scaling.

- [ ] **Step 3: Replace the pass-through noise block in `update()`**

In `coax/src/coax_cable_engine.cpp`, **replace** the block:

```cpp
    out.noise_W.assign(N, 0.0);
    for (size_t i = 0; i < N; ++i) {
        double nin = (in_ptr && i < in_ptr->noise_total_W.size()) ? in_ptr->noise_total_W[i] : 0.0;
        out.noise_W[i] = nin;  // pass-through; scaling added in Task 4
    }
    out.noise_added_W.assign(N, 0.0);
    out.noise_total_W.resize(N);
    for (size_t i = 0; i < N; ++i) {
        out.noise_total_W[i] = out.noise_W[i] + out.noise_added_W[i];
    }
```

with:

```cpp
    out.noise_W.assign(N, 0.0);
    out.noise_added_W.assign(N, 0.0);
    out.noise_total_W.assign(N, 0.0);
    for (size_t i = 0; i < N; ++i) {
        const double f_Hz = std::abs(out.frequencies[i]);
        const double f_Hz_c = std::clamp(f_Hz, 1.0, p.max_freq_GHz * 1e9);
        const double f_MHz = f_Hz_c / 1e6;
        const double loss_dB =
            (p.K1_dB_per_m * std::sqrt(f_MHz) + p.K2_dB_per_m * f_MHz) * m_length_m
            + m_connectors_loss_dB;
        const double L_lin = dbToLinear(loss_dB);
        const double nin = (in_ptr && i < in_ptr->noise_total_W.size()) ? in_ptr->noise_total_W[i] : 0.0;
        out.noise_W[i] = nin / L_lin;
        out.noise_total_W[i] = out.noise_W[i];  // noise_added_W is 0
    }
```

- [ ] **Step 4: Build & run to confirm tests pass**

Run:
```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "Coax cable"
```

Expected: all Coax tests pass.

- [ ] **Step 5: Commit**

```bash
git add coax/src/coax_cable_engine.cpp tests/test_coax_cable_engine.cpp
git commit -m "feat(coax): per-bin noise scaling by inverse linear loss"
```

---

## Task 5: Per-tone and per-bin phase shift

**Files:**
- Modify: `coax/src/coax_cable_engine.cpp`
- Modify: `tests/test_coax_cable_engine.cpp`

**Interfaces:**
- Consumes: per-tone loss from Task 3, per-bin loss from Task 4
- Produces: phase shift `Δφ = −360°·(f_Hz/1e9)·L·delay_ns_per_m·1e−3` applied to both tones and per-bin `phase_deg`

- [ ] **Step 1: Append failing tests to `tests/test_coax_cable_engine.cpp`**

```cpp
TEST_CASE("Coax cable applies per-tone phase shift", "[coax][phase]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(1e9, 0.0, 30.0);  // 1 GHz, 0 dBm, 30° initial phase
    gen.update(0.0);

    CoaxCableEngine cable(0, graph);
    cable.setPresetIndex(4);  // MT 340, delay 0.4 ns/m
    cable.setLengthM(1.0);
    cable.node().inputs[0] = &gen.node().outputs[0];
    cable.update(0.0);

    const auto& out = cable.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    // Expected shift: -360 * 1 * 1 * 0.4 * 1e-3 = -0.144 deg
    REQUIRE(out.tones[0].phase_deg == Approx(30.0 - 0.144).epsilon(1e-9));
}

TEST_CASE("Coax cable applies per-bin phase shift", "[coax][phase]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.update(0.0);
    // Initialise input per-bin phase to a known constant
    Spectrum* in = const_cast<Spectrum*>(&gen.node().outputs[0]);
    std::fill(in->phase_deg.begin(), in->phase_deg.end(), 0.0);

    CoaxCableEngine cable(0, graph);
    cable.setPresetIndex(4);
    cable.setLengthM(2.0);
    cable.node().inputs[0] = &gen.node().outputs[0];
    cable.update(0.0);

    const auto& out = cable.node().outputs[0];
    REQUIRE(out.phase_deg.size() == out.frequencies.size());
    for (size_t i = 0; i < out.frequencies.size(); ++i) {
        const double f_Hz_c = std::clamp(std::abs(out.frequencies[i]), 1.0, 18.5e9);
        const double expected_shift = -360.0 * (f_Hz_c / 1e9) * 2.0 * 0.4 * 1e-3;
        REQUIRE(out.phase_deg[i] == Approx(expected_shift).epsilon(1e-9));
    }
}
```

- [ ] **Step 2: Build & run to confirm they fail**

Run:
```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "Coax cable applies"
```

Expected: FAIL — phase is unchanged (still 30° on tone, 0° on bins).

- [ ] **Step 3: Add phase shift to the per-tone loop**

In `coax/src/coax_cable_engine.cpp`, **extend** the existing per-tone loop. After the `t.power_dBm -= loss_dB;` line, add:

```cpp
        const double phase_shift_deg =
            -360.0 * (f_Hz / 1e9) * m_length_m * p.delay_ns_per_m * 1e-3;
        t.phase_deg += phase_shift_deg;
```

- [ ] **Step 4: Replace the per-bin phase block**

In `coax/src/coax_cable_engine.cpp`, **replace** the block that initialises `out.phase_deg`:

```cpp
    if (in_ptr && !in_ptr->phase_deg.empty()) {
        out.phase_deg = in_ptr->phase_deg;
    } else {
        out.phase_deg.assign(N, 0.0);
    }
```

with:

```cpp
    if (in_ptr && !in_ptr->phase_deg.empty()) {
        out.phase_deg = in_ptr->phase_deg;
    } else {
        out.phase_deg.assign(N, 0.0);
    }
    for (size_t i = 0; i < N; ++i) {
        const double f_Hz = std::abs(out.frequencies[i]);
        const double f_Hz_c = std::clamp(f_Hz, 1.0, p.max_freq_GHz * 1e9);
        const double phase_shift_deg =
            -360.0 * (f_Hz_c / 1e9) * m_length_m * p.delay_ns_per_m * 1e-3;
        out.phase_deg[i] += phase_shift_deg;
    }
```

- [ ] **Step 5: Build & run to confirm tests pass**

Run:
```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "Coax cable"
```

Expected: all Coax tests pass.

- [ ] **Step 6: Commit**

```bash
git add coax/src/coax_cable_engine.cpp tests/test_coax_cable_engine.cpp
git commit -m "feat(coax): per-tone and per-bin phase shift"
```

---

## Task 6: Connector loss, length clamping, frequency clamping edge cases

**Files:**
- Modify: `tests/test_coax_cable_engine.cpp` (no engine change — behaviour already exists in setters and the per-tone/bin loops)

- [ ] **Step 1: Append tests to `tests/test_coax_cable_engine.cpp`**

```cpp
TEST_CASE("Coax cable connector loss adds flat 0.5 dB", "[coax][connectors]") {
    const double baseline = runOneTone(2e9, 0.0, 2.0, 4, 0.0);
    const double with_conn = runOneTone(2e9, 0.0, 2.0, 4, 0.5);
    REQUIRE(with_conn == Approx(baseline - 0.5).margin(0.001));
}

TEST_CASE("Coax cable length 0 leaves only connector loss", "[coax][edge]") {
    const double with_zero_len = runOneTone(2e9, 0.0, 0.0, 4, 1.0);
    REQUIRE(with_zero_len == Approx(-1.0).margin(0.001));
}

TEST_CASE("Coax cable clamps negative length to 0", "[coax][edge]") {
    NodeGraphEngine graph;
    CoaxCableEngine cable(0, graph);
    cable.setLengthM(-5.0);
    REQUIRE(cable.lengthM() == 0.0);
}

TEST_CASE("Coax cable clamps length to 1000 m", "[coax][edge]") {
    NodeGraphEngine graph;
    CoaxCableEngine cable(0, graph);
    cable.setLengthM(5000.0);
    REQUIRE(cable.lengthM() == 1000.0);
}

TEST_CASE("Coax cable tone above max_freq clamps loss", "[coax][edge]") {
    // 25 GHz exceeds MT 340's 18.5 GHz max
    const double at_max = runOneTone(18.5e9, 0.0, 1.0, 4, 0.0);
    const double above_max = runOneTone(25e9, 0.0, 1.0, 4, 0.0);
    // Both should compute loss at clamped f=18.5 GHz, so equal
    REQUIRE(above_max == Approx(at_max).margin(0.01));
}

TEST_CASE("Coax cable zero-K preset produces no loss", "[coax][edge]") {
    // Index 0 = MT 210, K1=K2=0 (stub)
    const double out_power = runOneTone(2e9, -10.0, 5.0, 0, 0.0);
    REQUIRE(out_power == Approx(-10.0).margin(0.001));
}
```

- [ ] **Step 2: Build & run to confirm they pass**

Run:
```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "Coax cable"
```

Expected: all six new tests pass (the engine already implements this behaviour from Tasks 3-5 + the setters in Task 2).

- [ ] **Step 3: Commit**

```bash
git add tests/test_coax_cable_engine.cpp
git commit -m "test(coax): connector loss, length clamping, freq clamping, zero-K preset"
```

---

## Task 7: Empty input, dirty-flag caching, generation bump

**Files:**
- Modify: `tests/test_coax_cable_engine.cpp`

- [ ] **Step 1: Append tests to `tests/test_coax_cable_engine.cpp`**

```cpp
TEST_CASE("Coax cable with no input produces empty tones and bumps generation", "[coax][edge]") {
    NodeGraphEngine graph;
    CoaxCableEngine cable(0, graph);
    cable.update(0.0);
    REQUIRE(cable.node().outputs[0].tones.empty());
    REQUIRE(cable.node().outputs[0].frequencies.size() >= 2);
}

TEST_CASE("Coax cable dirty flag skips when input unchanged", "[coax][caching]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(1e9, -20.0);
    gen.update(0.0);

    CoaxCableEngine cable(0, graph);
    cable.setPresetIndex(4);
    cable.setLengthM(1.0);
    cable.node().inputs[0] = &gen.node().outputs[0];
    cable.update(0.0);

    const uint64_t gen_after_first = cable.node().outputs[0].generation;
    cable.update(0.0);  // no setters, no input change → no recompute
    REQUIRE(cable.node().outputs[0].generation == gen_after_first);
}

TEST_CASE("Coax cable dirty flag triggers when length changes", "[coax][caching]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(1e9, -20.0);
    gen.update(0.0);

    CoaxCableEngine cable(0, graph);
    cable.setPresetIndex(4);
    cable.setLengthM(1.0);
    cable.node().inputs[0] = &gen.node().outputs[0];
    cable.update(0.0);

    const uint64_t gen_after_first = cable.node().outputs[0].generation;
    cable.setLengthM(2.0);
    cable.update(0.0);
    REQUIRE(cable.node().outputs[0].generation != gen_after_first);
}

TEST_CASE("Coax cable dirty flag triggers when input generation changes", "[coax][caching]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(1e9, -20.0);
    gen.update(0.0);

    CoaxCableEngine cable(0, graph);
    cable.setPresetIndex(4);
    cable.setLengthM(1.0);
    cable.node().inputs[0] = &gen.node().outputs[0];
    cable.update(0.0);

    const uint64_t gen_after_first = cable.node().outputs[0].generation;
    gen.updateTone(0, 1e9, -25.0);  // change input
    gen.update(0.0);
    cable.update(0.0);
    REQUIRE(cable.node().outputs[0].generation != gen_after_first);
}
```

- [ ] **Step 2: Build & run to confirm they pass**

Run:
```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "Coax cable"
```

Expected: all four new tests pass (the engine already implements this from Task 2's update-gating logic).

- [ ] **Step 3: Commit**

```bash
git add tests/test_coax_cable_engine.cpp
git commit -m "test(coax): empty input, dirty flag, generation bump"
```

---

## Task 8: Integration smoke test (gen → cable → amp)

**Files:**
- Modify: `tests/test_coax_cable_engine.cpp`

- [ ] **Step 1: Append integration test to `tests/test_coax_cable_engine.cpp`**

```cpp
TEST_CASE("Coax cable integrates in generator → cable → amplifier chain", "[coax][integration]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(2e9, -40.0);
    gen.update(0.0);

    CoaxCableEngine cable(0, graph);
    cable.setPresetIndex(4);   // MT 340
    cable.setLengthM(2.0);
    cable.node().inputs[0] = &gen.node().outputs[0];
    cable.update(0.0);

    AmplifierEngine amp(0, graph);
    amp.setGain_dB(10.0);
    amp.setNF_dB(0.0);  // disable added noise for a clean power check
    amp.node().inputs[0] = &cable.node().outputs[0];
    amp.update(0.0);

    // Cable loss at 2 GHz, 2 m: (0.004710*sqrt(2000) + 0.000004*2000) * 2 = 0.437 dB
    // Chain: -40 - 0.437 + 10 = -30.437 dBm
    REQUIRE(amp.node().outputs[0].tones[0].freq_Hz == 2e9);
    REQUIRE(amp.node().outputs[0].tones[0].power_dBm == Approx(-30.437).margin(0.05));
}
```

- [ ] **Step 2: Build & run to confirm it passes**

Run:
```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "Coax cable integrates"
```

Expected: PASS within ±0.05 dB.

- [ ] **Step 3: Commit**

```bash
git add tests/test_coax_cable_engine.cpp
git commit -m "test(coax): end-to-end gen → cable → amp chain"
```

---

## Task 9: InspectorPanel UI — `drawCoaxCableProperties`

**Files:**
- Modify: `app/include/inspector_panel.h`
- Modify: `app/src/inspector_panel.cpp`

**Interfaces:**
- Consumes: `CoaxCableEngine` (forward-declared in `inspector_panel.h`)
- Produces: a new `enum class ComponentType { ..., CoaxCable }` value; a new private method `void drawCoaxCableProperties(CoaxCableEngine& engine, int index)`; an updated `findSelected()` dispatch

- [ ] **Step 1: Add forward declaration and method to `inspector_panel.h`**

In `app/include/inspector_panel.h`, add the forward declaration alongside the existing ones (alphabetical, after `class AdcEngine;`):

```cpp
class CoaxCableEngine;
```

In the same file, add to the `enum class ComponentType`:

```cpp
    enum class ComponentType { None, Generator, Amplifier, Splitter, Mixer, SParamAmp, SParamFilter, Adc, PFB, IdealFilter, CoaxCable };
```

Add the new private method declaration alongside the other `drawXxxProperties` declarations:

```cpp
    void drawCoaxCableProperties(CoaxCableEngine& engine, int index);
```

- [ ] **Step 2: Update `findSelected()` dispatch in `inspector_panel.cpp`**

In `app/src/inspector_panel.cpp`, add a new include after the existing block of includes:

```cpp
#include "coax_cable_engine.h"
```

In `InspectorPanel::findSelected()`, add a new `else if` branch **before the final `return {ComponentType::None, nullptr};`**:

```cpp
    else if (dynamic_cast<CoaxCableEngine*>(engine))            return {ComponentType::CoaxCable, engine};
```

- [ ] **Step 3: Update `labelForHit()` for `CoaxCable`**

In `InspectorPanel::labelForHit()`, add a new `case` to the `switch` (alphabetical position, before `ComponentType::Generator`):

```cpp
        case ComponentType::CoaxCable:     return "Coax Cable " + std::to_string(hit.engine->id());
```

- [ ] **Step 4: Add `CoaxCable` case to the `draw()` switch**

In `InspectorPanel::draw()`, in the `switch (hit.type)` block, add a new case (alphabetical, before `case ComponentType::Generator:`):

```cpp
    case ComponentType::CoaxCable:
        drawCoaxCableProperties(*static_cast<CoaxCableEngine*>(hit.engine), hit.engine->id());
        break;
```

- [ ] **Step 5: Implement `drawCoaxCableProperties`**

In `app/src/inspector_panel.cpp`, add this method (alongside the other `drawXxxProperties` definitions):

```cpp
void InspectorPanel::drawCoaxCableProperties(CoaxCableEngine& engine, int index) {
    (void)index;
    const CableSpec& p = engine.preset();

    // Preset combo
    {
        const char* preview = p.name;
        if (ImGui::BeginCombo("Model", preview)) {
            for (int i = 0; i < static_cast<int>(kCoaxCablePresets.size()); ++i) {
                bool selected = (i == engine.presetIndex());
                if (ImGui::Selectable(kCoaxCablePresets[i].name, selected))
                    engine.setPresetIndex(i);
                if (selected) ImGui::SetItemDefaultFocus();
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("%s", kCoaxCablePresets[i].name);
                    ImGui::Text("K1 = %.6f dB/m", kCoaxCablePresets[i].K1_dB_per_m);
                    ImGui::Text("K2 = %.6f dB/m", kCoaxCablePresets[i].K2_dB_per_m);
                    ImGui::Text("Max freq: %.2f GHz", kCoaxCablePresets[i].max_freq_GHz);
                    ImGui::Text("Delay: %.3f ns/m", kCoaxCablePresets[i].delay_ns_per_m);
                    ImGui::EndTooltip();
                }
            }
            ImGui::EndCombo();
        }
    }

    // Length
    double L = engine.lengthM();
    if (utils::inputDouble("Length (m)", L, 0.01, 1.0, "%.3f", 0.0, 1000.0))
        engine.setLengthM(L);

    // Connector loss
    double conn = engine.connectorsLossDB();
    if (utils::inputDouble("Connector Loss (dB)", conn, 0.1, 1.0, "%.2f"))
        engine.setConnectorsLossDB(conn);

    // Read-out at input centre frequency
    if (!engine.node().inputs.empty() && engine.node().inputs[0] &&
        !engine.node().inputs[0]->frequencies.empty()) {
        const auto& fin = engine.node().inputs[0]->frequencies;
        const double fc = (fin.front() + fin.back()) / 2.0;
        const double fc_clamped = std::clamp(std::abs(fc), 1.0, p.max_freq_GHz * 1e9);
        const double fc_MHz = fc_clamped / 1e6;
        const double loss_dB =
            (p.K1_dB_per_m * std::sqrt(fc_MHz) + p.K2_dB_per_m * fc_MHz) * engine.lengthM()
            + engine.connectorsLossDB();
        const double phase_shift =
            -360.0 * (fc_clamped / 1e9) * engine.lengthM() * p.delay_ns_per_m * 1e-3;
        ImGui::TextDisabled("Loss @ fc: %.3f dB", loss_dB);
        ImGui::TextDisabled("Phase shift @ fc: %.3f deg", phase_shift);
    } else {
        ImGui::TextDisabled("Loss @ fc: --");
        ImGui::TextDisabled("Phase shift @ fc: --");
    }

    ImGui::TextDisabled("Max freq: %.2f GHz  |  Delay: %.3f ns/m  |  Diameter: %.1f mm",
                        p.max_freq_GHz, p.delay_ns_per_m, p.diameter_mm);

    if (ImGui::Button("Delete") && onRemoveNode)
        onRemoveNode(engine.graphNodeId());
}
```

- [ ] **Step 6: Build to confirm everything compiles**

Run:
```bash
cmake --build build
```

Expected: clean build.

- [ ] **Step 7: Commit**

```bash
git add app/include/inspector_panel.h app/src/inspector_panel.cpp
git commit -m "feat(coax): InspectorPanel properties for CoaxCable"
```

---

## Task 10: NodeGraphWidget menu entry + callback

**Files:**
- Modify: `node_graph/include/node_graph_widget.h`
- Modify: `node_graph/src/node_graph_widget.cpp`

**Interfaces:**
- Consumes: existing `onAdd*` callback pattern
- Produces: a new public member `std::function<void()> onAddCoaxCable;` and a new `ImGui::MenuItem("Add Coax Cable")` entry in the canvas context menu

- [ ] **Step 1: Add callback declaration to `node_graph_widget.h`**

In `node_graph/include/node_graph_widget.h`, add to the public callbacks block (after `onAddIdealFilter;`):

```cpp
    std::function<void()> onAddCoaxCable;
```

- [ ] **Step 2: Add menu item to `node_graph_widget.cpp`**

In `node_graph/src/node_graph_widget.cpp`, in `handleContextMenu`, add a new `ImGui::MenuItem` to the `canvas_context_menu` block (place it between `Add Splitter` and `Add Mixer` to keep alphabetical-by-display order with the spec):

```cpp
        if (ImGui::MenuItem("Add Coax Cable")) {
            if (onAddCoaxCable) onAddCoaxCable();
        }
```

- [ ] **Step 3: Commit**

```bash
git add node_graph/include/node_graph_widget.h node_graph/src/node_graph_widget.cpp
git commit -m "feat(coax): add 'Add Coax Cable' to node graph context menu"
```

---

## Task 11: Wire `onAddCoaxCable` in `RfSimulatorApp`

**Files:**
- Modify: `app/src/app.cpp`

- [ ] **Step 1: Add the include**

In `app/src/app.cpp`, add a new include after the existing component-engine includes:

```cpp
#include "coax_cable_engine.h"
```

- [ ] **Step 2: Add the callback in the `RfSimulatorApp` constructor**

In `RfSimulatorApp::RfSimulatorApp()`, add a new callback alongside the other `m_graph_widget->onAdd*` assignments (alphabetical, between `onAddAmplifier` and `onAddIdealFilter`):

```cpp
    m_graph_widget->onAddCoaxCable = [this]() {
        m_components.add<CoaxCableEngine>(m_next_component_id++, m_graph_engine);
    };
```

- [ ] **Step 3: Build and confirm the app still compiles**

Run:
```bash
cmake --build build
```

Expected: clean build.

- [ ] **Step 4: Commit**

```bash
git add app/src/app.cpp
git commit -m "feat(coax): wire onAddCoaxCable in RfSimulatorApp"
```

---

## Task 12: Run full test suite and verify clean state

**Files:** (no source changes)

- [ ] **Step 1: Run the full test suite**

```bash
ctest --test-dir build --output-on-failure
```

Expected: every existing test plus all 17 new Coax tests pass. Total count goes from 73 → ~90.

- [ ] **Step 2: Run clang-format on all changed files**

```bash
clang-format -i \
    coax/include/coax_presets.h \
    coax/include/coax_cable_engine.h \
    coax/src/coax_cable_engine.cpp \
    tests/test_coax_cable_presets.cpp \
    tests/test_coax_cable_engine.cpp \
    app/include/inspector_panel.h \
    app/src/inspector_panel.cpp \
    node_graph/include/node_graph_widget.h \
    node_graph/src/node_graph_widget.cpp \
    app/src/app.cpp
```

- [ ] **Step 3: If clang-format changed anything, commit the formatting**

```bash
git diff --stat
# if anything changed:
git add -u
git commit -m "style: clang-format on coax + integration files"
```

- [ ] **Step 4: Manual smoke check (optional, recommended)**

```bash
build/bin/rf_simulator   # or build/bin/rf_simulator.exe on Windows
```

In the GUI:
1. Right-click the canvas → confirm "Add Coax Cable" is present.
2. Add a Coax Cable; click on its node in the graph.
3. Verify the Inspector shows the preset combo, length, connector loss, and computed read-outs.
4. Add a Generator and an Amplifier; wire them around the cable; probe the output pin and confirm the spectrum analyzer shows the attenuated tone.

---

## Verification Checklist

- [ ] All 5 MilTech 340 datasheet spot-checks pass at L=100 m (Task 3).
- [ ] Per-bin noise scaling by `1/L_linear` verified (Task 4).
- [ ] No thermal noise added (`noise_added_W = 0`) verified (Task 4).
- [ ] Per-tone and per-bin phase shift verified at 1 GHz, 1 m, MT 340 (Task 5).
- [ ] Connector loss additive; length clamped to `[0, 1000]` m; freq clamped to `[1 Hz, max_freq_GHz]`; zero-K preset harmless (Task 6).
- [ ] Empty input safe; dirty flag skips; generation bumps on input change or setter (Task 7).
- [ ] End-to-end gen → cable → amp chain: -40 dBm at 2 GHz, 2 m MT 340, 10 dB amp → -30.44 dBm within 0.05 dB (Task 8).
- [ ] "Add Coax Cable" menu entry present in the node graph (Tasks 10-11).
- [ ] InspectorPanel renders the cable's controls and live read-outs (Task 9).
- [ ] Full `ctest` suite green; `clang-format -i` applied to all changed files (Task 12).
- [ ] No new third-party dependencies introduced.
- [ ] No documentation files (`docs/**/*.md`) committed to git.
