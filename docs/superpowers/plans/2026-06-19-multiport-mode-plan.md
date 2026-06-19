# Multi-Port S-Parameter Mode-Aware Pins Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `Splitter` / `Combiner` / `FullMatrix` modes to `SParamEngine` so devices with >2 ports show the right number of pins for their role (1 in + N−1 out for splitter, N−1 in + 1 out for combiner) instead of N input + N output pins.

**Architecture:** Add a `Mode` enum and `commonPort` setting to `SParamEngine`. `rebuildNode()` creates role-appropriate pins. `update()` has mode-specific evaluation paths. `IComponentEngine` gains `numInputPins()`/`numOutputPins()` virtuals defaulting to 1. The app routing loop uses `numInputPins()` generically.

**Tech Stack:** C++20, CMake, Catch2

## Global Constraints

- C++20 standard enforced via `CMAKE_CXX_STANDARD 20`
- Engines must not include `<imgui.h>` or `<implot.h>`
- All existing tests must pass without modification
- Default mode is FullMatrix (preserves backward compat)

---
### Task 1: Add numInputPins/numOutputPins to IComponentEngine

**Files:**
- Modify: `common/component_interface.h`

- [ ] **Step 1: Add virtuals with defaults**

```cpp
// common/component_interface.h
class IComponentEngine {
public:
    // ... existing ...

    virtual int numInputPins() const { return 1; }
    virtual int numOutputPins() const { return 1; }
};
```

- [ ] **Step 2: Build**

```bash
cd build && cmake .. -G Ninja && ninja tests
```

Expected: Clean build.

- [ ] **Step 3: Commit**

```bash
git add common/component_interface.h
git commit -m "feat: add numInputPins/numOutputPins to IComponentEngine"
```

---
### Task 2: Add Mode support to SParamEngine

**Files:**
- Modify: `s_parametric_component/include/s_param_engine.h`
- Modify: `s_parametric_component/src/s_param_engine.cpp`

- [ ] **Step 1: Add Mode enum and config to header**

```cpp
// s_parametric_component/include/s_param_engine.h

// In the public section, before the constructor:
    enum class Mode { Splitter, Combiner, FullMatrix };

    Mode mode() const { return m_mode; }
    void setMode(Mode mode);

    // 0-based port index for the "common" port (only used in Splitter/Combiner)
    int commonPort() const { return m_common_port; }
    void setCommonPort(int port);

    int numInputPins() const override;
    int numOutputPins() const override;
```

Add private members:
```cpp
    Mode m_mode = Mode::FullMatrix;
    int m_common_port = 0; // 0-based
```

- [ ] **Step 2: Add mode string helpers**

```cpp
// In an anonymous namespace or as a public static:
    static const char* modeName(Mode m) {
        switch (m) {
            case Mode::Splitter: return "Splitter";
            case Mode::Combiner: return "Combiner";
            case Mode::FullMatrix: return "Full Matrix";
        }
        return "Unknown";
    }
```

- [ ] **Step 3: Implement setMode and setCommonPort**

```cpp
// s_parametric_component/src/s_param_engine.cpp

void SParamEngine::setMode(Mode mode) {
    if (mode != m_mode) {
        m_mode = mode;
        rebuildNode();
        m_dirty = true;
    }
}

void SParamEngine::setCommonPort(int port) {
    int np = m_data.numPorts();
    if (port >= 0 && port < np && port != m_common_port) {
        m_common_port = port;
        rebuildNode();
        m_dirty = true;
    }
}
```

- [ ] **Step 4: Implement numInputPins/numOutputPins**

```cpp
int SParamEngine::numInputPins() const {
    if (!m_data.loaded()) return 1; // placeholder
    int np = m_data.numPorts();
    switch (m_mode) {
        case Mode::Splitter:   return 1;          // just the common port
        case Mode::Combiner:   return np - 1;     // all non-common ports
        case Mode::FullMatrix: return np;          // all ports
    }
    return 1;
}

int SParamEngine::numOutputPins() const {
    if (!m_data.loaded()) return 1; // placeholder
    int np = m_data.numPorts();
    switch (m_mode) {
        case Mode::Splitter:   return np - 1;     // all non-common ports
        case Mode::Combiner:   return 1;          // just the common port
        case Mode::FullMatrix: return np;          // all ports
    }
    return 1;
}
```

- [ ] **Step 5: Update rebuildNode() for mode-aware pin layout**

```cpp
void SParamEngine::rebuildNode() {
    int np = m_data.numPorts();
    if (np < 1) return;

    if (m_graph_node_id >= 0)
        m_graph->removeNode(m_graph_node_id);

    int n_in = numInputPins();
    int n_out = numOutputPins();

    m_graph_node_id = m_graph->addNode("S-Param " + std::to_string(m_id), &m_node, n_in, n_out);
    m_node.inputs.resize(n_in);
    m_node.outputs.resize(n_out);

    // Set pin labels
    m_graph->setNodePinLabels(m_graph_node_id, computeInputLabels(), computeOutputLabels());

    m_cache_valid = false;
    LOG_INFO("Rebuilt S-param node %d as %s (%d in, %d out)",
             m_id, modeName(m_mode), n_in, n_out);
}
```

- [ ] **Step 6: Implement computeInputLabels / computeOutputLabels helpers**

```cpp
// In the private section of the header, add:
    std::vector<std::string> computeInputLabels() const;
    std::vector<std::string> computeOutputLabels() const;

// In the .cpp:
std::vector<std::string> SParamEngine::computeInputLabels() const {
    int np = m_data.numPorts();
    std::vector<std::string> labels;
    switch (m_mode) {
        case Mode::Splitter:
            labels.push_back("Common (Port " + std::to_string(m_common_port + 1) + ")");
            break;
        case Mode::Combiner:
            for (int p = 0; p < np; ++p) {
                if (p == m_common_port) continue;
                labels.push_back("Port " + std::to_string(p + 1));
            }
            break;
        case Mode::FullMatrix:
            for (int p = 0; p < np; ++p)
                labels.push_back("Port " + std::to_string(p + 1));
            break;
    }
    return labels;
}

std::vector<std::string> SParamEngine::computeOutputLabels() const {
    int np = m_data.numPorts();
    std::vector<std::string> labels;
    switch (m_mode) {
        case Mode::Splitter:
            for (int p = 0; p < np; ++p) {
                if (p == m_common_port) continue;
                labels.push_back("Port " + std::to_string(p + 1));
            }
            break;
        case Mode::Combiner:
            labels.push_back("Common (Port " + std::to_string(m_common_port + 1) + ")");
            break;
        case Mode::FullMatrix:
            for (int p = 0; p < np; ++p)
                labels.push_back("Port " + std::to_string(p + 1));
            break;
    }
    return labels;
}
```

- [ ] **Step 7: Update inputPinId/outputPinId for mode-awareness**

The existing `inputPinId(int port)` and `outputPinId(int port)` already iterate the graph node's pins. Since the graph node now has the correct pin count per mode, these work correctly as-is — `port` is just a 0-based index into the actual pins on the node. No change needed to the accessors.

But add a clarifying comment that `port` is the *pin index* (0..numInputPins-1), not the Touchstone port number.

- [ ] **Step 8: Update update() for mode-specific signal paths**

In the FullMatrix path, replace the inner loop with mode-specific allocation.

Currently the full matrix path iterates j=0..N-1 and k=0..N-1. For Splitter/Combiner, we need smaller specific loops.

Add a new private method `applySplitter(Spectrum* outputs, const Spectrum* inputs)`:

```cpp
// In the full-matrix block of update(), add a branch at the top:
if (m_mode != Mode::FullMatrix) {
    // Mode-specific evaluation (much simpler than full matrix)
    if (m_mode == Mode::Splitter) {
        const Spectrum* in = m_node.inputs.empty() ? nullptr : m_node.inputs[0];
        if (!in) {
            for (auto& out : m_node.outputs) out.bumpGeneration();
            m_node.outputs[0].bumpGeneration();
            return;
        }

        int out_idx = 0;
        for (int p = 0; p < m_data.numPorts(); ++p) {
            if (p == m_common_port) continue;
            int param_idx = p * m_data.numPorts() + m_common_port;  // S_p,C
            m_data.applyToSpectrum(*in, m_node.outputs[out_idx], param_idx);
            ++out_idx;
        }
    } else if (m_mode == Mode::Combiner) {
        // Accumulate from all non-common ports into the common port output
        auto& out = m_node.outputs[0];
        // Determine frequency grid from first connected input
        const Spectrum* first = nullptr;
        for (auto* in_k : m_node.inputs) { if (in_k) { first = in_k; break; } }
        if (!first) { out.bumpGeneration(); return; }

        out.frequencies = first->frequencies;
        out.tones.clear();
        out.noise_W.assign(out.frequencies.size(), 0.0);
        out.phase_deg = first->phase_deg;

        int in_idx = 0;
        for (int p = 0; p < m_data.numPorts(); ++p) {
            if (p == m_common_port) continue;
            const Spectrum* in_k = m_node.inputs[in_idx];
            if (in_k) {
                int param_idx = m_common_port * m_data.numPorts() + p; // S_C,p
                // Apply S_C,p to input at in_idx and accumulate into out
                applyParamAndAccumulate(*in_k, out, param_idx);
            }
            ++in_idx;
        }
    }

    // Apply NF (same as before, on all outputs)
    // NL only on output[0] (same as before)
    for (auto& o : m_node.outputs) o.bumpGeneration();
    return;
}
```

Actually, this is getting complex. Let me simplify: the existing `applyToSpectrum` is already a "apply one S-param to one input → one output". We can reuse it directly.

For Splitter:
```
for each non-common port p:
    m_data.applyToSpectrum(m_node.inputs[0], m_node.outputs[out_idx], p * N + common_port)
```

For Combiner:
```
start with output = empty
for each non-common port p with connected input:
    temp = applyToSpectrum(m_node.inputs[in_idx], S_common,p)
    output.tones += temp.tones (coherent merge)
    output.noise += temp.noise (incoherent)
```

But `applyToSpectrum` overwrites the output. So for combiner we can't call it N-1 times. We need to do the accumulation manually, which is what the full matrix path already does.

Simplest approach: for Splitter mode, just call applyToSpectrum once per output (each gets a different S-param, they're independent). For Combiner mode, run the accumulation inline in update().

- [ ] **Step 9: Update reload() to preserve mode when file changes**

The current `reload()` calls `rebuildNode()` which resets pins. Since the mode setting persists across reloads, `rebuildNode()` already uses the current `m_mode`, so no change needed — it'll create the right pin layout for whatever mode is currently set.

- [ ] **Step 10: Update hoverSummary to show mode**

```cpp
std::string SParamEngine::hoverSummary() const {
    if (!m_data.loaded()) return "Not loaded";
    int np = m_data.numPorts();
    std::string s = modeName(m_mode);
    s += " | " + std::to_string(np) + "-port";
    if (m_mode != Mode::FullMatrix)
        s += " | Common: " + std::to_string(m_common_port + 1);
    // ... rest unchanged
}
```

- [ ] **Step 11: Build to verify**

```bash
cd build && cmake .. -G Ninja && ninja tests
```

Expected: Clean build. All existing tests pass (default mode = FullMatrix).

- [ ] **Step 12: Commit**

```bash
git add s_parametric_component/include/s_param_engine.h
git add s_parametric_component/src/s_param_engine.cpp
git commit -m "feat: add Splitter/Combiner modes with mode-aware pin layout"
```

---
### Task 3: Update app routing to use numInputPins

**Files:**
- Modify: `app/src/app.cpp`

- [ ] **Step 1: Replace dynamic_cast loop with generic numInputPins loop**

```cpp
// Replace:
for (auto* comp : m_components.all()) {
    int num_inputs = 1;
    SParamEngine* sp = dynamic_cast<SParamEngine*>(comp);
    if (sp) {
        num_inputs = sp->numPorts();
    }
    for (int k = 0; k < num_inputs; ++k) {
        int pid;
        if (sp) {
            pid = sp->inputPinId(k);
        } else {
            pid = (k == 0) ? comp->inputPinId() : -1;
        }
        if (pid >= 0) {
            auto* source = m_graph_engine.getSourceForInput(pid);
            comp->node().inputs[k] = source ? &source->outputs[0] : nullptr;
        } else if (static_cast<size_t>(k) < comp->node().inputs.size()) {
            comp->node().inputs[k] = nullptr;
        }
    }
    updates[comp->graphNodeId()] = [comp]() { comp->update(0.0); };
}

// With:
for (auto* comp : m_components.all()) {
    int N = comp->numInputPins();
    for (int k = 0; k < N; ++k) {
        int pid = comp->inputPinId(k);
        if (pid >= 0) {
            auto* source = m_graph_engine.getSourceForInput(pid);
            comp->node().inputs[k] = source ? &source->outputs[0] : nullptr;
        } else if (static_cast<size_t>(k) < comp->node().inputs.size()) {
            comp->node().inputs[k] = nullptr;
        }
    }
    updates[comp->graphNodeId()] = [comp]() { comp->update(0.0); };
}
```

Wait — this won't work for legacy engines. `inputPinId(0)` returns -1 (the base default). They'd need to override `numInputPins()` to return 0, which is worse.

Actually, the current fix (from earlier) is:
```cpp
int pid;
if (sp) {
    pid = sp->inputPinId(k);
} else {
    pid = (k == 0) ? comp->inputPinId() : -1;
}
```

This still uses `dynamic_cast`. The proper fix is to override `inputPinId(int)` on legacy engines to forward to `inputPinId()` for port 0. But that's 7 files.

Better approach: change the base default:
```cpp
// In IComponentEngine:
virtual int inputPinId(int port) const { return port == 0 ? inputPinId() : -1; }
```

This way:
- Legacy engines inherit: `inputPinId(0)` → returns their existing inputPinId(), `inputPinId(k>0)` → -1 ✓
- SParamEngine overrides: returns the multi-pin layout based on mode ✓

Then the routing loop simplifies to:
```cpp
int N = comp->numInputPins();
for (int k = 0; k < N; ++k) {
    int pid = comp->inputPinId(k);
    ...
}
```

And `numInputPins()` default is 1, which matches all legacy engines.

This is the cleanest approach. Change the default on `IComponentEngine::inputPinId(int)` from `return -1` to `return port == 0 ? inputPinId() : -1`.

- [ ] **Step 2: Build and verify**

```bash
cd build && cmake .. -G Ninja && ninja tests && ctest --test-dir . --output-on-failure
```

Expected: All existing tests pass (legacy engines now get correct pin id through the default forwarding).

- [ ] **Step 3: Commit**

```bash
git add app/src/app.cpp common/component_interface.h
git commit -m "feat: generic numInputPins routing, base inputPinId(int) forwards to inputPinId()"
```

---
### Task 4: Update inspector panel

**Files:**
- Modify: `app/src/inspector_panel.cpp`

- [ ] **Step 1: Replace combo/checkbox with mode dropdown**

In `drawSParamProperties()`, after the file section and loaded check, replace the current forward-param combo with:

```cpp
if (engine.loaded()) {
    int np = engine.data().numPorts();

    // Mode selector (only show for N > 2? or always show)
    auto current_mode = engine.mode();
    const char* mode_items[] = {"Splitter", "Combiner", "Full Matrix"};
    int mode_idx = static_cast<int>(current_mode);
    if (ImGui::Combo("Mode", &mode_idx, mode_items, IM_ARRAYSIZE(mode_items))) {
        engine.setMode(static_cast<SParamEngine::Mode>(mode_idx));
    }

    // Common port selector (only when not FullMatrix)
    if (current_mode != SParamEngine::Mode::FullMatrix) {
        int common = engine.commonPort();
        // Build items: "Port 1", "Port 2", ...
        std::string preview = "Port " + std::to_string(common + 1);
        if (ImGui::BeginCombo("Common Port", preview.c_str())) {
            for (int p = 0; p < np; ++p) {
                std::string lbl = "Port " + std::to_string(p + 1);
                if (ImGui::Selectable(lbl.c_str(), p == common))
                    engine.setCommonPort(p);
            }
            ImGui::EndCombo();
        }
    }

    ImGui::Text("Ports: %d | Data points: %zu | Max: %.0f MHz",
                np, engine.data().freqs().size(), engine.data().freqs().back() / 1e6);
}
```

Additionally, only show the Forward-Param single-param override when in FullMatrix mode (it's meaningless in Splitter/Combiner).

- [ ] **Step 2: Build**

```bash
cd build && cmake .. -G Ninja && ninja app
```

- [ ] **Step 3: Commit**

```bash
git add app/src/inspector_panel.cpp
git commit -m "feat: inspector shows mode/common port dropdowns"
```

---
### Task 5: Update tests

**Files:**
- Modify: `tests/test_s_parameter_amplifier.cpp`

- [ ] **Step 1: Add splitter mode test**

```cpp
TEST_CASE("SParamEngine splitter mode produces N-1 outputs", "[sparam][multiport]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/component_data/splitters/mpd-0226ch/MPD-0226CH_CH_25C_F.s3p";
    SParamEngine sp(0, graph, path);
    REQUIRE(sp.loaded());
    REQUIRE(sp.numPorts() == 3);

    // Default is FullMatrix — switch to Splitter
    sp.setCommonPort(1); // Port 2 is COMMON on this splitter (0-based = 1)
    sp.setMode(SParamEngine::Mode::Splitter);

    REQUIRE(sp.numInputPins() == 1);
    REQUIRE(sp.numOutputPins() == 2);

    // Wire a signal to the single input pin
    Spectrum in_spec;
    in_spec.tones = {{2e9, -10.0, 0.0}};
    sp.node().inputs[0] = &in_spec;

    sp.update(0.0);

    // Should have 2 outputs (Port 1 OUT, Port 3 OUT)
    REQUIRE(sp.node().outputs[0].tones.size() == 1);
    REQUIRE(sp.node().outputs[1].tones.size() == 1);

    // Both should be at roughly -3.6 dB below input (the split ratio)
    REQUIRE(sp.node().outputs[0].tones[0].power_dBm == Approx(-13.63).margin(0.5));
    REQUIRE(sp.node().outputs[1].tones[0].power_dBm == Approx(-13.95).margin(0.5));
}
```

- [ ] **Step 2: Add combiner mode test**

```cpp
TEST_CASE("SParamEngine combiner mode sums inputs onto common port",
          "[sparam][multiport]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/component_data/splitters/mpd-0226ch/MPD-0226CH_CH_25C_F.s3p";
    SParamEngine sp(0, graph, path);
    REQUIRE(sp.loaded());

    sp.setCommonPort(1); // Port 2
    sp.setMode(SParamEngine::Mode::Combiner);

    REQUIRE(sp.numInputPins() == 2);
    REQUIRE(sp.numOutputPins() == 1);

    // Two inputs at different frequencies
    Spectrum in_a, in_b;
    in_a.tones = {{2e9, -10.0, 0.0}};
    in_b.tones = {{2.001e9, -10.0, 0.0}};
    // Set up frequency grids for the spectrum apply
    in_a.frequencies = {2e9};
    in_a.noise_W = {1e-12};
    in_a.noise_total_W = {1e-12};
    in_b.frequencies = {2.001e9};
    in_b.noise_W = {1e-12};
    in_b.noise_total_W = {1e-12};

    sp.node().inputs[0] = &in_a; // Port 1
    sp.node().inputs[1] = &in_b; // Port 3

    sp.update(0.0);

    // Single output (Port 2) should have both tones
    const auto& out = sp.node().outputs[0];
    REQUIRE(out.tones.size() >= 2);

    bool found_a = false, found_b = false;
    for (const auto& t : out.tones) {
        if (std::abs(t.freq_Hz - 2e9) < 1.0) found_a = true;
        if (std::abs(t.freq_Hz - 2.001e9) < 1.0) found_b = true;
    }
    REQUIRE(found_a);
    REQUIRE(found_b);
}
```

- [ ] **Step 3: Add mode switching test**

```cpp
TEST_CASE("SParamEngine mode switch rebuilds pin layout", "[sparam][multiport]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/component_data/splitters/mpd-0226ch/MPD-0226CH_CH_25C_F.s3p";
    SParamEngine sp(0, graph, path);
    REQUIRE(sp.loaded());

    // Default FullMatrix: 3 in, 3 out
    REQUIRE(sp.numInputPins() == 3);
    REQUIRE(sp.numOutputPins() == 3);

    // Switch to Splitter: 1 in, 2 out
    sp.setMode(SParamEngine::Mode::Splitter);
    REQUIRE(sp.numInputPins() == 1);
    REQUIRE(sp.numOutputPins() == 2);

    // Switch to Combiner: 2 in, 1 out
    sp.setMode(SParamEngine::Mode::Combiner);
    REQUIRE(sp.numInputPins() == 2);
    REQUIRE(sp.numOutputPins() == 1);

    // Switch back to FullMatrix: 3 in, 3 out
    sp.setMode(SParamEngine::Mode::FullMatrix);
    REQUIRE(sp.numInputPins() == 3);
    REQUIRE(sp.numOutputPins() == 3);
}
```

- [ ] **Step 4: Add common port switch test**

```cpp
TEST_CASE("SParamEngine common port change rebuilds pin layout", "[sparam][multiport]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/component_data/splitters/mpd-0226ch/MPD-0226CH_CH_25C_F.s3p";
    SParamEngine sp(0, graph, path);
    REQUIRE(sp.loaded());

    sp.setMode(SParamEngine::Mode::Splitter);
    sp.setCommonPort(0); // Port 1 as common
    REQUIRE(sp.numInputPins() == 1);
    REQUIRE(sp.numOutputPins() == 2);

    // Change common port
    sp.setCommonPort(2); // Port 3 as common
    REQUIRE(sp.numInputPins() == 1);
    REQUIRE(sp.numOutputPins() == 2);
    // Output ports should now be Port 1 and Port 2
}
```

- [ ] **Step 5: Build and run all tests**

```bash
cd build && cmake .. -G Ninja && ninja tests && ctest --test-dir . --output-on-failure
```

Expected: All existing tests + 4 new multiport tests pass.

- [ ] **Step 6: Commit**

```bash
git add tests/test_s_parameter_amplifier.cpp
git commit -m "test: add splitter/combiner mode tests"
```
