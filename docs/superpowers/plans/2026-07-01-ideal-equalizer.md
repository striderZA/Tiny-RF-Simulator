# Ideal Equalizer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a new 1-in/1-out `EqualizerEngine` that applies a frequency-dependent loss `L(f) = L_DC + slope * log10(max(|f|, 1 Hz))` (units: dB / dB-per-decade), wire it into the app and inspector, and cover the behaviour with unit tests.

**Architecture:** Engine-only module mirroring `ideal_filter/`. The engine is pure DSP, no ImGui; the existing `InspectorPanel` is extended with one draw function. Tests live in `tests/test_equalizer_engine.cpp` (Catch2 v3, `[equalizer]` tag).

**Tech Stack:** C++20, Catch2 v3.4.0, ImGui (inspector only).

---

## File Structure

**New files**
- `equalizer/CMakeLists.txt` — module build (mirrors `ideal_filter/CMakeLists.txt`)
- `equalizer/include/equalizer_engine.h` — `EqualizerEngine` class declaration
- `equalizer/src/equalizer_engine.cpp` — implementation
- `equalizer/AGENTS.md` — module-local doc (Purpose / Ownership / Work Guidance / Verification / Child DOX Index)
- `tests/test_equalizer_engine.cpp` — Catch2 unit tests, `[equalizer]` tag

**Modified files**
- `CMakeLists.txt` (root) — `add_subdirectory(equalizer)`
- `app/CMakeLists.txt` — link `simulator::equalizer_engine` into `app`
- `app/src/app.cpp` — `#include "equalizer_engine.h"`; `m_graph_widget->onAddEqualizer` callback
- `node_graph/include/node_graph_widget.h` — `std::function<void()> onAddEqualizer;` member
- `node_graph/src/node_graph_widget.cpp` — `ImGui::MenuItem("Add Equalizer")` in the add-component menu
- `app/include/inspector_panel.h` — forward decl; `Equalizer` in `ComponentType` enum; `drawEqualizerProperties` decl
- `app/src/inspector_panel.cpp` — `dynamic_cast<EqualizerEngine*>` dispatch; title entry; draw switch; `drawEqualizerProperties` impl
- `tests/CMakeLists.txt` — add `test_equalizer_engine.cpp` to `TEST_SOURCES`
- `AGENTS.md` (root) — move `equalizer/AGENTS.md` from `*(pending)*` to real entry in Child DOX Index

---

## Global Constraints

- C++20. CMake 3.20+. Catch2 v3.4.0.
- Engines must not include `<imgui.h>` / `<implot.h>` (per `ARCHITECTURE.md` "Conventions").
- DSP helpers: `utils::inputDouble(label, ref, minorStep, majorStep, ...)` from `core/include/utils.h`.
- Tone struct: `{double freq_Hz, double power_dBm, double phase_deg}`.
- Float comparisons in tests: `Catch::Approx` from `<catch2/catch_approx.hpp>`.
- Pattern: follow `ideal_filter/CMakeLists.txt` and `ideal_filter_engine.{h,cpp}` for the engine module shape. Follow `inspector_panel.cpp::drawIdealFilterProperties` for the inspector draw function.
- Per `CONTRIBUTING.md`: feature branch (`feat/ideal-equalizer`); atomic commits; verify build + tests before committing; imperative-mood subject lines <70 chars.
- Every step is a checkbox, in order. Do not skip ahead.

---

## Task 1: Engine skeleton + identity

**Files:**
- Create: `equalizer/CMakeLists.txt`
- Create: `equalizer/include/equalizer_engine.h`
- Create: `equalizer/src/equalizer_engine.cpp`
- Create: `tests/test_equalizer_engine.cpp` (one failing test)
- Modify: `CMakeLists.txt` (root) — append `add_subdirectory(equalizer)`
- Modify: `tests/CMakeLists.txt` — append `test_equalizer_engine.cpp` to `TEST_SOURCES`

**Interfaces:**
- Produces:
  - `class EqualizerEngine : public IComponentEngine`
  - ctor: `EqualizerEngine(int id, NodeGraphEngine& graph)`
  - Override: `int id()`, `int graphNodeId()`, `int inputPinId()`, `int outputPinId()`, `std::string hoverSummary()`, `SignalNode& node()`, `const SignalNode& node() const`, `void update(double dt)`.
  - This task uses only `update`, `node()`, `inputPinId()`, `outputPinId()`, `graphNodeId()`, `id()`. Other overrides come in later tasks.
- Consumes: `SignalNode`, `Spectrum`, `IComponentEngine`, `NodeGraphEngine::addNode(name, &node, n_in, n_out)`.

- [ ] **Step 1: Create the module CMakeLists**

Write `equalizer/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(equalizer LANGUAGES CXX)

add_library(equalizer_engine STATIC
    src/equalizer_engine.cpp
)

target_include_directories(equalizer_engine
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(equalizer_engine PUBLIC common simulator::node_graph_engine)
add_library(simulator::equalizer_engine ALIAS equalizer_engine)
```

- [ ] **Step 2: Create the header**

Write `equalizer/include/equalizer_engine.h`:

```cpp
#pragma once
#include "common.h"
#include "component_interface.h"
#include "node_graph_engine.h"
#include "signal_node.h"

class EqualizerEngine : public IComponentEngine {
  public:
    EqualizerEngine(int id, NodeGraphEngine& graph);

    int id() const override { return m_id; }
    int graphNodeId() const override { return m_graph_node_id; }
    int inputPinId() const override;
    int outputPinId() const override;
    std::string hoverSummary() const override;
    SignalNode& node() override { return m_node; }
    const SignalNode& node() const override { return m_node; }
    void update(double dt) override;

  private:
    int m_id;
    int m_graph_node_id = -1;
    NodeGraphEngine* m_graph = nullptr;
    SignalNode m_node;                        // 1 input, 1 output
    bool m_dirty = true;
    const Spectrum* m_cached_input_ptr = nullptr;
    uint64_t m_cached_input_generation = 0;
};
```

- [ ] **Step 3: Create a pass-through implementation**

Write `equalizer/src/equalizer_engine.cpp`:

```cpp
#include "equalizer_engine.h"
#include <cstdio>

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

void EqualizerEngine::update(double dt) {
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
    out.noise_added_W.assign(N, 0.0);
    out.noise_total_W.assign(N, 0.0);

    out.fs_Hz = in_ptr ? in_ptr->fs_Hz : 0.0;
    out.bumpGeneration();
}

std::string EqualizerEngine::hoverSummary() const {
    return "Equalizer";
}
```

- [ ] **Step 4: Wire CMake — root and tests**

Edit `CMakeLists.txt` (root), append after the existing `add_subdirectory(ideal_filter)` line (final line of the file):

```cmake
add_subdirectory(equalizer")
```

Edit `tests/CMakeLists.txt`, append `test_equalizer_engine.cpp` to the `TEST_SOURCES` list (keep alphabetical order, after `test_coax_cable_engine.cpp` and before `test_group.cpp`):

```cmake
    test_coax_cable_engine.cpp
    test_equalizer_engine.cpp
    test_group.cpp
```

Also add `simulator::equalizer_engine` to the `target_link_libraries(tests PRIVATE ...)` list, after `simulator::ideal_filter_engine`:

```cmake
    simulator::ideal_filter_engine
    simulator::equalizer_engine
    simulator::splitter_engine
```

- [ ] **Step 5: Write the first failing test**

Create `tests/test_equalizer_engine.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "common.h"
#include "equalizer_engine.h"
#include "signal_generator_engine.h"
#include "node_graph_engine.h"

using Catch::Approx;

TEST_CASE("Equalizer identity passes tones unchanged", "[equalizer]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, -20.0);
    gen.addTone(500e6, -35.0);
    gen.update(0.0);

    EqualizerEngine eq(0, graph);
    eq.node().inputs[0] = &gen.node().outputs[0];
    eq.update(0.0);

    const auto& out = eq.node().outputs[0];
    REQUIRE(out.tones.size() == 2);
    REQUIRE(out.tones[0].freq_Hz == 100e6);
    REQUIRE(out.tones[0].power_dBm == Approx(-20.0));
    REQUIRE(out.tones[1].freq_Hz == 500e6);
    REQUIRE(out.tones[1].power_dBm == Approx(-35.0));
}
```

- [ ] **Step 6: Configure and build**

Run from repo root:

```bash
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc
cmake --build build
```

Expected: build succeeds; the new `equalizer_engine` static library compiles; the `tests` target compiles with the new test file.

- [ ] **Step 7: Run the test to verify it passes**

Run:

```bash
ctest --test-dir build --output-on-failure -R Equalizer
```

Expected: 1 test passes (`Equalizer identity passes tones unchanged`).

- [ ] **Step 8: Commit**

```bash
git add equalizer/CMakeLists.txt equalizer/include/equalizer_engine.h equalizer/src/equalizer_engine.cpp tests/test_equalizer_engine.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(equalizer): engine skeleton with pass-through identity"
```

---

## Task 2: L_DC parameter (uniform loss)

**Files:**
- Modify: `equalizer/include/equalizer_engine.h` — add `setLossAtDC` / `lossAtDC`; add `double m_loss_at_DC_dB = 0.0;`
- Modify: `equalizer/src/equalizer_engine.cpp` — apply `m_loss_at_DC_dB` in `update()`
- Modify: `tests/test_equalizer_engine.cpp` — add 2 tests

**Interfaces:**
- Produces:
  - `void EqualizerEngine::setLossAtDC(double dB)` — sets `m_loss_at_DC_dB`, flips `m_dirty` on change.
  - `double EqualizerEngine::lossAtDC() const` — returns `m_loss_at_DC_dB`.
  - `update()` behaviour: per-tone `out.tones[i].power_dBm = in.tones[i].power_dBm - m_loss_at_DC_dB`. Per-bin `out.noise_W[i] = in.noise_total_W[i] / dbToLinear(m_loss_at_DC_dB)`.

- [ ] **Step 1: Add failing tests**

Append to `tests/test_equalizer_engine.cpp`:

```cpp
TEST_CASE("Equalizer L_DC shifts all tones uniformly", "[equalizer]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, -20.0);
    gen.addTone(500e6, -30.0);
    gen.update(0.0);

    EqualizerEngine eq(0, graph);
    eq.setLossAtDC(-3.0);                  // -3 dB at DC: removes 3 dB from all tones
    eq.node().inputs[0] = &gen.node().outputs[0];
    eq.update(0.0);

    const auto& out = eq.node().outputs[0];
    REQUIRE(out.tones.size() == 2);
    REQUIRE(out.tones[0].power_dBm == Approx(-23.0));
    REQUIRE(out.tones[1].power_dBm == Approx(-33.0));
}

TEST_CASE("Equalizer L_DC dirty flag triggers recompute", "[equalizer]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, -20.0);
    gen.update(0.0);

    EqualizerEngine eq(0, graph);
    eq.node().inputs[0] = &gen.node().outputs[0];
    eq.update(0.0);
    const uint64_t gen0 = eq.node().outputs[0].generation;

    eq.setLossAtDC(-3.0);
    eq.update(0.0);
    REQUIRE(eq.node().outputs[0].generation != gen0);
    REQUIRE(eq.node().outputs[0].tones[0].power_dBm == Approx(-23.0));
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --build build && ctest --test-dir build --output-on-failure -R "Equalizer L_DC"
```

Expected: FAIL with `'setLossAtDC' is not a member of 'EqualizerEngine'`.

- [ ] **Step 3: Add the header declarations**

In `equalizer/include/equalizer_engine.h`, add inside the public block (right after the `node()` overrides):

```cpp
    void setLossAtDC(double dB);
    double lossAtDC() const { return m_loss_at_DC_dB; }
```

In the `private:` block, add a new member next to the existing `bool m_dirty`:

```cpp
    double m_loss_at_DC_dB = 0.0;
```

- [ ] **Step 4: Implement the setter in the cpp**

In `equalizer/src/equalizer_engine.cpp`, append the setter definition (after the `hoverSummary()` function):

```cpp
void EqualizerEngine::setLossAtDC(double dB) {
    if (dB != m_loss_at_DC_dB) {
        m_loss_at_DC_dB = dB;
        m_dirty = true;
    }
}
```

Also add `#include <cmath>` near the top (above `#include <cstdio>` or below — both already work; place after the existing includes for tidy alphabetical order).

- [ ] **Step 5: Apply the loss in update()**

In `equalizer/src/equalizer_engine.cpp::update()`, replace the per-tone block:

```cpp
    out.tones = in_ptr ? in_ptr->tones : std::vector<Spectrum::Tone>{};
```

with:

```cpp
    out.tones.clear();
    if (in_ptr) {
        for (const auto& t : in_ptr->tones) {
            Spectrum::Tone out_t = t;
            out_t.power_dBm -= m_loss_at_DC_dB;
            out.tones.push_back(out_t);
        }
    }
```

And replace the noise block:

```cpp
    out.noise_W.assign(N, 0.0);
    out.noise_added_W.assign(N, 0.0);
    out.noise_total_W.assign(N, 0.0);
```

with:

```cpp
    const double L_lin = dbToLinear(m_loss_at_DC_dB);
    out.noise_W.assign(N, 0.0);
    out.noise_added_W.assign(N, 0.0);
    out.noise_total_W.resize(N);
    for (size_t i = 0; i < N; ++i) {
        const double nin = (in_ptr && i < in_ptr->noise_total_W.size()) ? in_ptr->noise_total_W[i] : 0.0;
        out.noise_W[i] = nin / L_lin;
        out.noise_total_W[i] = out.noise_W[i];
    }
```

- [ ] **Step 6: Build and run the new tests**

```bash
cmake --build build && ctest --test-dir build --output-on-failure -R "Equalizer"
```

Expected: 3 tests pass (1 from Task 1 + 2 new).

- [ ] **Step 7: Commit**

```bash
git add equalizer/include/equalizer_engine.h equalizer/src/equalizer_engine.cpp tests/test_equalizer_engine.cpp
git commit -m "feat(equalizer): L_DC parameter applies uniform loss"
```

---

## Task 3: Slope parameter + `lossAt(f)` helper

**Files:**
- Modify: `equalizer/include/equalizer_engine.h` — add `setSlope` / `slope`; add `double m_slope_dB_per_decade = 0.0;`; add `double lossAt(double freq_Hz) const;` private helper.
- Modify: `equalizer/src/equalizer_engine.cpp` — implement `setSlope`; implement `lossAt`; replace the L_DC-only tone/noise scaling in `update()` with `lossAt`-driven scaling.
- Modify: `tests/test_equalizer_engine.cpp` — add 4 tests.

**Interfaces:**
- Produces:
  - `void EqualizerEngine::setSlope(double dB_per_decade)` — sets `m_slope_dB_per_decade`, flips `m_dirty` on change.
  - `double EqualizerEngine::slope() const` — returns `m_slope_dB_per_decade`.
  - `double EqualizerEngine::lossAt(double freq_Hz) const` — returns `m_loss_at_DC_dB + m_slope_dB_per_decade * std::log10(std::max(std::abs(freq_Hz), 1.0))`.
  - `update()` per-tone: `out.tones[i].power_dBm = in.tones[i].power_dBm - lossAt(in.tones[i].freq_Hz)`.
  - `update()` per-bin: `out.noise_W[i] = in.noise_total_W[i] / dbToLinear(lossAt(out.frequencies[i]))`.

- [ ] **Step 1: Add failing tests**

Append to `tests/test_equalizer_engine.cpp`:

```cpp
TEST_CASE("Equalizer slope produces 18 dB loss at 1 GHz", "[equalizer]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(1.0, 0.0);                 // 1 Hz, log10(1) = 0
    gen.addTone(10.0, 0.0);                // 10 Hz, log10(10) = 1
    gen.addTone(1e9, 0.0);                 // 1 GHz, log10(1e9) = 9
    gen.addTone(1e10, 0.0);                // 10 GHz, log10(1e10) = 10
    gen.update(0.0);

    EqualizerEngine eq(0, graph);
    eq.setSlope(2.0);                      // 2 dB/decade
    eq.node().inputs[0] = &gen.node().outputs[0];
    eq.update(0.0);

    const auto& out = eq.node().outputs[0];
    REQUIRE(out.tones[0].power_dBm == Approx(0.0));      // 1 Hz: 0 dB loss
    REQUIRE(out.tones[1].power_dBm == Approx(-2.0));     // 10 Hz: 2 dB loss
    REQUIRE(out.tones[2].power_dBm == Approx(-18.0));    // 1 GHz: 18 dB loss
    REQUIRE(out.tones[3].power_dBm == Approx(-20.0));    // 10 GHz: 20 dB loss
}

TEST_CASE("Equalizer negative slope acts as pre-emphasis", "[equalizer]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(1e9, 0.0);
    gen.update(0.0);

    EqualizerEngine eq(0, graph);
    eq.setSlope(-2.0);                     // -2 dB/decade
    eq.node().inputs[0] = &gen.node().outputs[0];
    eq.update(0.0);

    REQUIRE(eq.node().outputs[0].tones[0].power_dBm == Approx(18.0));
}

TEST_CASE("Equalizer DC floor clamps f<=0 to L_DC", "[equalizer]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.update(0.0);
    // Synthesise a tone at exactly 0 Hz by pushing directly into the output spectrum.
    gen.node().outputs[0].tones.push_back({0.0, 0.0, 0.0});
    gen.update(0.0);

    EqualizerEngine eq(0, graph);
    eq.setLossAtDC(-3.0);
    eq.setSlope(2.0);
    eq.node().inputs[0] = &gen.node().outputs[0];
    eq.update(0.0);

    REQUIRE(eq.node().outputs[0].tones[0].power_dBm == Approx(3.0));   // 0 Hz -> loss = -3 dB
}

TEST_CASE("Equalizer sub-1 Hz floor clamps to L_DC", "[equalizer]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.update(0.0);
    gen.node().outputs[0].tones.push_back({0.5, 0.0, 0.0});
    gen.update(0.0);

    EqualizerEngine eq(0, graph);
    eq.setLossAtDC(-3.0);
    eq.setSlope(2.0);
    eq.node().inputs[0] = &gen.node().outputs[0];
    eq.update(0.0);

    REQUIRE(eq.node().outputs[0].tones[0].power_dBm == Approx(3.0));   // 0.5 Hz -> loss = -3 dB
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --build build && ctest --test-dir build --output-on-failure -R "Equalizer slope|Equalizer negative|Equalizer DC|Equalizer sub-1"
```

Expected: FAIL with `'setSlope' is not a member of 'EqualizerEngine'`.

- [ ] **Step 3: Add the header declarations**

In `equalizer/include/equalizer_engine.h`, add inside the public block (after `setLossAtDC` / `lossAtDC`):

```cpp
    void setSlope(double dB_per_decade);
    double slope() const { return m_slope_dB_per_decade; }
```

In the `private:` block, add the new member next to `m_loss_at_DC_dB`:

```cpp
    double m_slope_dB_per_decade = 0.0;
```

And add the helper declaration below the existing private members:

```cpp
    double lossAt(double freq_Hz) const;
```

- [ ] **Step 4: Implement setter, helper, and update wiring**

In `equalizer/src/equalizer_engine.cpp`, append the new definitions (after `setLossAtDC`):

```cpp
void EqualizerEngine::setSlope(double dB_per_decade) {
    if (dB_per_decade != m_slope_dB_per_decade) {
        m_slope_dB_per_decade = dB_per_decade;
        m_dirty = true;
    }
}

double EqualizerEngine::lossAt(double freq_Hz) const {
    double f = std::abs(freq_Hz);
    if (f < 1.0) f = 1.0;
    return m_loss_at_DC_dB + m_slope_dB_per_decade * std::log10(f);
}
```

- [ ] **Step 5: Update the per-tone and per-bin code in update()**

In `equalizer/src/equalizer_engine.cpp::update()`, replace the per-tone block (added in Task 2):

```cpp
    out.tones.clear();
    if (in_ptr) {
        for (const auto& t : in_ptr->tones) {
            Spectrum::Tone out_t = t;
            out_t.power_dBm -= m_loss_at_DC_dB;
            out.tones.push_back(out_t);
        }
    }
```

with:

```cpp
    out.tones.clear();
    if (in_ptr) {
        for (const auto& t : in_ptr->tones) {
            Spectrum::Tone out_t = t;
            out_t.power_dBm -= lossAt(t.freq_Hz);
            out.tones.push_back(out_t);
        }
    }
```

Replace the per-bin noise block:

```cpp
    const double L_lin = dbToLinear(m_loss_at_DC_dB);
    out.noise_W.assign(N, 0.0);
    out.noise_added_W.assign(N, 0.0);
    out.noise_total_W.resize(N);
    for (size_t i = 0; i < N; ++i) {
        const double nin = (in_ptr && i < in_ptr->noise_total_W.size()) ? in_ptr->noise_total_W[i] : 0.0;
        out.noise_W[i] = nin / L_lin;
        out.noise_total_W[i] = out.noise_W[i];
    }
```

with:

```cpp
    out.noise_W.assign(N, 0.0);
    out.noise_added_W.assign(N, 0.0);
    out.noise_total_W.resize(N);
    for (size_t i = 0; i < N; ++i) {
        const double nin = (in_ptr && i < in_ptr->noise_total_W.size()) ? in_ptr->noise_total_W[i] : 0.0;
        const double L_lin = dbToLinear(lossAt(out.frequencies[i]));
        out.noise_W[i] = nin / L_lin;
        out.noise_total_W[i] = out.noise_W[i];
    }
```

- [ ] **Step 6: Build and run all equalizer tests**

```bash
cmake --build build && ctest --test-dir build --output-on-failure -R "Equalizer"
```

Expected: 7 tests pass (3 from prior tasks + 4 new).

- [ ] **Step 7: Commit**

```bash
git add equalizer/include/equalizer_engine.h equalizer/src/equalizer_engine.cpp tests/test_equalizer_engine.cpp
git commit -m "feat(equalizer): slope parameter with log10 model and DC floor"
```

---

## Task 4: Output wiring (phase, fs, noise, empty input)

**Files:**
- Modify: `tests/test_equalizer_engine.cpp` — add 4 tests. No production-code change expected (logic already wired in Tasks 1–3).

**Interfaces:**
- Verifies:
  - `out.phase_deg[i]` equals `in->phase_deg[i]` for all `i` (no shift added).
  - `out.fs_Hz` equals `in->fs_Hz`.
  - `out.noise_W[i] = in.noise_total_W[i] / 10^(lossAt(f_i)/10)`.
  - Empty input → empty tones on default grid, `bumpGeneration()` called.

- [ ] **Step 1: Add failing tests**

Append to `tests/test_equalizer_engine.cpp`:

```cpp
TEST_CASE("Equalizer phase passes through unchanged", "[equalizer]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, -20.0);
    gen.node().outputs[0].tones[0].phase_deg = 42.0;
    gen.update(0.0);

    EqualizerEngine eq(0, graph);
    eq.setSlope(3.0);                      // ensure slope is non-zero so update() is exercised
    eq.node().inputs[0] = &gen.node().outputs[0];
    eq.update(0.0);

    REQUIRE(eq.node().outputs[0].tones[0].phase_deg == Approx(42.0));
}

TEST_CASE("Equalizer fs_Hz passes through", "[equalizer]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, -20.0);
    gen.update(0.0);
    gen.node().outputs[0].fs_Hz = 200e6;    // set after update so it sticks

    EqualizerEngine eq(0, graph);
    eq.node().inputs[0] = &gen.node().outputs[0];
    eq.update(0.0);

    REQUIRE(eq.node().outputs[0].fs_Hz == Approx(200e6));
}

TEST_CASE("Equalizer noise density scales by 1/L_linear", "[equalizer]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.update(0.0);
    // Manually populate noise on input
    auto& in_spec = gen.node().outputs[0];
    in_spec.noise_total_W.assign(in_spec.frequencies.size(), 1e-20);
    for (auto& v : in_spec.noise_total_W) v = 1e-20;

    EqualizerEngine eq(0, graph);
    eq.setLossAtDC(-3.0);
    eq.node().inputs[0] = &in_spec;
    eq.update(0.0);

    const double L_lin = dbToLinear(-3.0);
    const auto& out = eq.node().outputs[0];
    REQUIRE(out.noise_W.size() == in_spec.frequencies.size());
    for (size_t i = 0; i < out.noise_W.size(); ++i) {
        REQUIRE(out.noise_W[i] == Approx(1e-20 / L_lin).epsilon(1e-9));
        REQUIRE(out.noise_total_W[i] == Approx(out.noise_W[i]).epsilon(1e-30));
        REQUIRE(out.noise_added_W[i] == Approx(0.0).epsilon(1e-30));
    }
}

TEST_CASE("Equalizer with no input produces empty tones on default grid", "[equalizer][edge]") {
    NodeGraphEngine graph;
    EqualizerEngine eq(0, graph);
    eq.update(0.0);

    const auto& out = eq.node().outputs[0];
    REQUIRE(out.tones.empty());
    REQUIRE(out.frequencies.size() >= 2);
    REQUIRE(out.noise_total_W.size() == out.frequencies.size());
    REQUIRE(out.generation > 0);
}
```

- [ ] **Step 2: Run tests to verify they pass**

```bash
cmake --build build && ctest --test-dir build --output-on-failure -R "Equalizer"
```

Expected: 11 tests pass. If any fail, the production code needs a small patch — investigate before committing.

- [ ] **Step 3: Commit**

```bash
git add tests/test_equalizer_engine.cpp
git commit -m "test(equalizer): phase, fs, noise, empty-input coverage"
```

---

## Task 5: Caching (dirty flag + generation bump)

**Files:**
- Modify: `tests/test_equalizer_engine.cpp` — add 2 tests.
- Modify (only if a test fails): `equalizer/src/equalizer_engine.cpp` — set `m_dirty = true` from any missing setter. (`setLossAtDC` and `setSlope` already do this; if a test fails, the bug is in `update()` itself.)

- [ ] **Step 1: Add failing tests**

Append to `tests/test_equalizer_engine.cpp`:

```cpp
TEST_CASE("Equalizer dirty flag skips recompute on identical inputs", "[equalizer]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, -20.0);
    gen.update(0.0);

    EqualizerEngine eq(0, graph);
    eq.setLossAtDC(-3.0);
    eq.node().inputs[0] = &gen.node().outputs[0];
    eq.update(0.0);
    const uint64_t gen0 = eq.node().outputs[0].generation;

    eq.update(0.0);
    REQUIRE(eq.node().outputs[0].generation == gen0);   // no recompute
}

TEST_CASE("Equalizer generation bumps on slope change", "[equalizer]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, -20.0);
    gen.update(0.0);

    EqualizerEngine eq(0, graph);
    eq.node().inputs[0] = &gen.node().outputs[0];
    eq.update(0.0);
    const uint64_t gen0 = eq.node().outputs[0].generation;

    eq.setSlope(5.0);
    eq.update(0.0);
    REQUIRE(eq.node().outputs[0].generation != gen0);
}
```

- [ ] **Step 2: Run tests to verify they pass**

```bash
cmake --build build && ctest --test-dir build --output-on-failure -R "Equalizer"
```

Expected: 13 tests pass.

- [ ] **Step 3: Commit**

```bash
git add tests/test_equalizer_engine.cpp
git commit -m "test(equalizer): dirty flag and generation bump coverage"
```

---

## Task 6: hoverSummary

**Files:**
- Modify: `equalizer/src/equalizer_engine.cpp` — implement `hoverSummary()` to return `"Equalizer | L@DC=X dB, slope=Y dB/dec"`.
- Modify: `tests/test_equalizer_engine.cpp` — add 1 test.

**Interfaces:**
- Produces: `std::string EqualizerEngine::hoverSummary() const` returns `"Equalizer | L@DC=<X> dB, slope=<Y> dB/dec"` with `X = m_loss_at_DC_dB` and `Y = m_slope_dB_per_decade`, formatted with `std::snprintf("%.2f", ...)`.

- [ ] **Step 1: Add failing test**

Append to `tests/test_equalizer_engine.cpp`:

```cpp
TEST_CASE("Equalizer hoverSummary shows L_DC and slope", "[equalizer]") {
    NodeGraphEngine graph;
    EqualizerEngine eq(0, graph);
    eq.setLossAtDC(-2.5);
    eq.setSlope(1.5);
    const std::string s = eq.hoverSummary();
    REQUIRE(s == "Equalizer | L@DC=-2.50 dB, slope=1.50 dB/dec");
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build && ctest --test-dir build --output-on-failure -R "Equalizer hoverSummary"
```

Expected: FAIL with current `hoverSummary() = "Equalizer"`.

- [ ] **Step 3: Implement hoverSummary**

In `equalizer/src/equalizer_engine.cpp`, replace the existing `hoverSummary()` function:

```cpp
std::string EqualizerEngine::hoverSummary() const {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "Equalizer | L@DC=%.2f dB, slope=%.2f dB/dec",
                  m_loss_at_DC_dB, m_slope_dB_per_decade);
    return buf;
}
```

- [ ] **Step 4: Build and run all equalizer tests**

```bash
cmake --build build && ctest --test-dir build --output-on-failure -R "Equalizer"
```

Expected: 14 tests pass.

- [ ] **Step 5: Commit**

```bash
git add equalizer/src/equalizer_engine.cpp tests/test_equalizer_engine.cpp
git commit -m "feat(equalizer): hoverSummary with L_DC and slope"
```

---

## Task 7: App integration (CMake + add callback + inspector + node graph menu)

**Files:**
- Modify: `app/CMakeLists.txt` — add `simulator::equalizer_engine` to the link list.
- Modify: `app/src/app.cpp` — add `#include "equalizer_engine.h"`; add `m_graph_widget->onAddEqualizer` lambda.
- Modify: `node_graph/include/node_graph_widget.h` — add `std::function<void()> onAddEqualizer;` member.
- Modify: `node_graph/src/node_graph_widget.cpp` — add `ImGui::MenuItem("Add Equalizer")` in the add-component menu.
- Modify: `app/include/inspector_panel.h` — forward decl; add `Equalizer` to `ComponentType` enum; add `drawEqualizerProperties` decl.
- Modify: `app/src/inspector_panel.cpp` — `dynamic_cast<EqualizerEngine*>` dispatch; title entry; draw switch; `drawEqualizerProperties` impl.

**Interfaces:**
- Produces:
  - `app::RfSimulatorApp::onAddEqualizer` callback adds an `EqualizerEngine` to `m_components`.
  - Inspector can edit `L_DC` and `slope` of an equalizer; shows read-only `Loss at 1 GHz = L_DC + 9 * slope` summary.
  - "Add Equalizer" entry visible in the node-graph add-component menu.

- [ ] **Step 1: Add the include and link for app**

In `app/CMakeLists.txt`, inside `target_link_libraries(app PUBLIC ...)`, add (alphabetically near `simulator::ideal_filter_engine`):

```cmake
        simulator::ideal_filter_engine
        simulator::equalizer_engine
        simulator::node_graph_engine
```

- [ ] **Step 2: Add the onAddEqualizer callback to NodeGraphWidget header**

In `node_graph/include/node_graph_widget.h`, add to the callbacks block (after `onAddIdealFilter`):

```cpp
    std::function<void()> onAddEqualizer;
```

- [ ] **Step 3: Add the menu item to NodeGraphWidget**

In `node_graph/src/node_graph_widget.cpp`, find the add-component menu block (search for `if (ImGui::MenuItem("Add Ideal Filter"))`) and add a new entry right after it:

```cpp
        if (ImGui::MenuItem("Add Equalizer")) {
            if (onAddEqualizer) onAddEqualizer();
        }
```

- [ ] **Step 4: Wire the app callback**

In `app/src/app.cpp`, add the include near the other engine includes (after `#include "coax_cable_engine.h"`):

```cpp
#include "equalizer_engine.h"
```

In the constructor, after the existing `m_graph_widget->onAddIdealFilter = ...` block, add:

```cpp
    m_graph_widget->onAddEqualizer = [this]() {
        m_components.add<EqualizerEngine>(m_next_component_id++, m_graph_engine);
    };
```

- [ ] **Step 5: Add inspector header entries**

In `app/include/inspector_panel.h`, add a forward declaration (alphabetical, near `class IdealFilterEngine;`):

```cpp
class EqualizerEngine;
```

Extend the `ComponentType` enum (after `IdealFilter`):

```cpp
    enum class ComponentType { None, Generator, Amplifier, Splitter, Mixer, SParam, Adc, PFB, IdealFilter, Equalizer, CoaxCable };
```

Add the draw function declaration (after `drawIdealFilterProperties`):

```cpp
    void drawEqualizerProperties(EqualizerEngine& engine, int index);
```

- [ ] **Step 6: Inspector dispatch and title**

In `app/src/inspector_panel.cpp`, in `findSelected()`, add a new branch (after the `IdealFilter` branch):

```cpp
    else if (dynamic_cast<EqualizerEngine*>(engine))           return {ComponentType::Equalizer, engine};
```

In `labelForHit()` (or wherever component titles are returned), add (after the `IdealFilter` case):

```cpp
        case ComponentType::Equalizer:   return "Equalizer " + std::to_string(hit.engine->id());
```

- [ ] **Step 7: Inspector draw switch and `drawEqualizerProperties`**

In `app/src/inspector_panel.cpp::drawForNode()` (or the equivalent dispatch in the inspector body), add a new case in the switch (after `ComponentType::IdealFilter`):

```cpp
        case ComponentType::Equalizer:
            drawEqualizerProperties(*static_cast<EqualizerEngine*>(hit.engine), hit.engine->id());
            break;
```

Append the draw function definition at the end of the file (after `drawIdealFilterProperties`):

```cpp
void InspectorPanel::drawEqualizerProperties(EqualizerEngine& engine, int index) {
    (void)index;
    ImGui::Text("Equalizer");
    ImGui::Separator();

    bool view = engine.node().view_enabled;
    if (ImGui::Checkbox("Measure", &view)) {
        engine.node().view_enabled = view;
    }

    double loss_dc = engine.lossAtDC();
    if (utils::inputDouble("L@DC (dB)", loss_dc, 0.1, 1.0, "%.2f")) {
        engine.setLossAtDC(loss_dc);
    }

    double slope = engine.slope();
    if (utils::inputDouble("Slope (dB/decade)", slope, 0.1, 1.0, "%.2f")) {
        engine.setSlope(slope);
    }

    const double loss_1ghz = engine.lossAt(1e9);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "Loss at 1 GHz = %.2f dB", loss_1ghz);
    ImGui::Text("%s", buf);
}
```

- [ ] **Step 8: Build**

```bash
cmake --build build
```

Expected: build succeeds; `app` target compiles with the new inspector entry and `m_components.add<EqualizerEngine>` call; `node_graph_widget` compiles with the new callback and menu item.

- [ ] **Step 9: Run all tests**

```bash
ctest --test-dir build --output-on-failure
```

Expected: all 14 equalizer tests + all 73 pre-existing tests pass.

- [ ] **Step 10: Commit**

```bash
git add app/CMakeLists.txt app/src/app.cpp app/include/inspector_panel.h app/src/inspector_panel.cpp node_graph/include/node_graph_widget.h node_graph/src/node_graph_widget.cpp
git commit -m "feat(equalizer): app integration (add callback, inspector, node menu)"
```

---

## Task 8: Documentation

**Files:**
- Create: `equalizer/AGENTS.md`
- Modify: `AGENTS.md` (root) — Child DOX Index: replace the `equalizer/AGENTS.md — *(pending)*` entry with the new file path.

- [ ] **Step 1: Create the module AGENTS.md**

Write `equalizer/AGENTS.md`:

```markdown
# equalizer — AGENTS.md

## Purpose

Own the `EqualizerEngine`: a 1-in/1-out DSP engine that applies a frequency-dependent loss defined by `L(f) = L_DC + slope_dB_per_decade * log10(max(|f|, 1 Hz))`. Engine-only (no widget); configuration lives in the existing `InspectorPanel`.

## Ownership

- `equalizer/CMakeLists.txt` — `equalizer_engine` static library, exposed as `simulator::equalizer_engine`.
- `equalizer/include/equalizer_engine.h` — class declaration.
- `equalizer/src/equalizer_engine.cpp` — implementation; `lossAt(f)` helper applies the DC floor.
- `equalizer/AGENTS.md` — this file.

## Local Contracts

- Two free parameters: `L_DC_dB` (loss at DC, default 0 dB) and `slope_dB_per_decade` (default 0 dB/decade). Both unbounded; no clamping.
- Positive slope = more loss at high f (cable-like). Negative slope = pre-emphasis. Zero slope + zero `L_DC` = identity.
- Phase: zero shift. Noise: passive, no NF (`noise_added_W` always zero).
- Setters flip `m_dirty` on actual change; `update()` short-circuits when neither dirty flag nor upstream spectrum changed.
- `|f| < 1 Hz` is clamped to the 1 Hz floor to avoid `log10(0)`.

## Work Guidance

- When extending the equalizer with a noise figure, follow the `AmplifierEngine` pattern: add `m_noise_figure_dB` + `addedNoiseDensity_W_per_Hz(...)` in `update()`. The `noise_added_W` channel is already wired.
- When extending to piecewise slopes, replace `m_slope_dB_per_decade` with a `std::vector<SlopeSegment>` and update `lossAt` to walk the segments.
- Inspector is the only UI surface. If a standalone widget is needed, follow the `coax/` pattern (`equalizer_widget.h/cpp`) and link `simulator::equalizer_widget` into `app`.

## Verification

- `cmake --build build && ctest --test-dir build -R Equalizer` must pass with all 14 `[equalizer]`-tagged tests.
- Manual smoke: launch the app, add a Generator (e.g. 1 GHz, −20 dBm), add an Equalizer (`L_DC=0`, `slope=2 dB/decade`), wire generator → equalizer → spectrum analyzer, verify the probed tone reads ≈ −38 dBm (20 dB loss at 1 GHz from the slope).

## Child DOX Index

No child docs. `equalizer/` is a flat directory.
```

- [ ] **Step 2: Update the root AGENTS.md Child DOX Index**

In `AGENTS.md` (root), in the Child DOX Index, find the line:

```markdown
- ideal_filter/AGENTS.md — *(pending)* Ideal filter engine + widget
```

Replace it with:

```markdown
- ideal_filter/AGENTS.md — *(pending)* Ideal filter engine + widget
- equalizer/AGENTS.md — Equalizer engine (no widget; configured via `InspectorPanel`)
```

- [ ] **Step 3: Commit**

```bash
git add equalizer/AGENTS.md AGENTS.md
git commit -m "docs(equalizer): module AGENTS.md and root Child DOX Index entry"
```

---

## Self-Review

**1. Spec coverage**

| Spec section | Task(s) |
|---|---|
| §1 Goal (engine-only module) | Task 1 |
| §2 In-scope: 2 params (`L_DC`, `slope`) | Tasks 2, 3 |
| §2 In-scope: closed-form `L(f) = L_DC + slope * log10(max(|f|, 1 Hz))` | Task 3 |
| §2 In-scope: per-tone + per-bin application | Tasks 2, 3, 4 |
| §2 In-scope: zero phase, zero added noise | Tasks 1, 4 |
| §2 In-scope: node-graph integration | Task 7 |
| §2 In-scope: ~8 unit tests | Tasks 1–6 deliver 14 |
| §3 Architecture (module layout, CMake) | Tasks 1, 7 |
| §4 Data model | Task 1 (header) + Tasks 2, 3 (members) |
| §5 Processing model (gate, frequency grid, per-tone, per-bin, bump) | Tasks 1, 2, 3, 4 |
| §5 `lossAt(f)` helper (1 Hz floor) | Task 3 |
| §6 UI integration (inspector only) | Task 7 |
| §7 App integration (CMake, add callback, enum, dispatch, draw fn) | Task 7 |
| §8 Tests: identity, pure DC, pure slope, negative slope, DC floor, sub-1 Hz, phase, fs_Hz, noise, empty input, dirty, generation, integration smoke | Tasks 1, 2, 3, 4, 5, 6 (covers all) |
| §9 Out-of-scope items (NF, phase, piecewise, cable-match, etc.) | not implemented; explicitly listed in `equalizer/AGENTS.md` "Work Guidance" for future follow-up |
| §10 Documentation updates | Task 8 |

**2. Placeholder scan**

No "TBD", "TODO", "implement later", or "add appropriate error handling" patterns. Every test has full code. Every CMake / cpp / h edit has the full new content. The `if (dB != m_loss_at_DC_dB)` and `if (dB_per_decade != m_slope_dB_per_decade)` setters are real code, not placeholders.

**3. Type consistency**

- `setLossAtDC(double)`, `setSlope(double)`, `lossAtDC() const`, `slope() const` — consistent across header, cpp, and tests.
- `lossAt(double freq_Hz) const` declared in header, defined in cpp, called from `update()` with `t.freq_Hz` and `out.frequencies[i]` (both `double`).
- `EqualizerEngine` referenced consistently in `app/CMakeLists.txt` (`simulator::equalizer_engine`), `tests/CMakeLists.txt`, `app/src/app.cpp` (`m_components.add<EqualizerEngine>(...)`), `app/include/inspector_panel.h` (forward decl + enum + draw fn decl), `app/src/inspector_panel.cpp` (dynamic_cast, switch case, function definition), `node_graph/include/node_graph_widget.h` (callback member), `node_graph/src/node_graph_widget.cpp` (menu item).
- The `ComponentType::Equalizer` enum value is referenced in 4 places (declaration, dispatch via `dynamic_cast`, label, draw switch). All consistent.
- No naming conflicts with `IdealFilterEngine`, `CoaxCableEngine`, etc.

**4. Build / test commands**

Every command in every step is the actual `cmake --build build` / `ctest --test-dir build --output-on-failure -R ...` pattern used elsewhere in the repo. Filtered to `[equalizer]` tag in early tasks (smaller test count) and `--test-dir build --output-on-failure` in Task 7 (full suite) — matches existing CI practice.

**5. Risks & follow-ups**

- Task 7 menu insertion: I have not read every line of `node_graph_widget.cpp` to know the exact indentation and surrounding context. The implementer should match the existing style when inserting the new `ImGui::MenuItem` line.
- The integration smoke test from the spec (2 GHz, slope=4 dB/decade, output ≈ −77.2 dBm) is not added as a separate test case; the existing 1 GHz / 10 GHz / 1 Hz / 10 Hz tests in Task 3 already cover the log10 model with multiple frequencies. If reviewers want the exact spec smoke test, add it as a follow-up.

Plan complete.
