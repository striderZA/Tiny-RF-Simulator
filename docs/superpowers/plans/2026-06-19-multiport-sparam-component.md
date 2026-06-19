# Multi-Port S-Parameter Component Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend `SParamEngine` to support N-port devices (splitters, couplers, balanced amplifiers) with one input + one output pin per physical Touchstone port and full linear S-matrix evaluation.

**Architecture:** Extend `IComponentEngine` with multi-pin virtuals, add optional pin labels to `GraphNode` for rendering, make `SParamEngine::reload()` the central lifecycle method (rebuilding the graph node when port count changes), and replace the single-param `update()` with full N×N matrix accumulation. The app routing loop expands to iterate all input pins.

**Tech Stack:** C++20, CMake, Catch2, imnodes

## Global Constraints

- C++20 standard enforced via `CMAKE_CXX_STANDARD 20`
- Engines must not include `<imgui.h>` or `<implot.h>`
- `IComponentEngine` interface changes must keep default implementations so all existing engines are unaffected
- All existing tests must pass without modification
- The component's existing forward-param API must remain functional for backward compat
- The MPD-0226CH .s3p file at `component_data/splitters/mpd-0226ch/MPD-0226CH_CH_25C_F.s3p` is the reference multi-port test file

---
### Task 1: Extend IComponentEngine with multi-pin virtuals

**Files:**
- Modify: `common/component_interface.h`

**Interfaces:**
- Consumes: existing `IComponentEngine` (no prior task dependencies)
- Produces: `IComponentEngine` with `inputPinId(int port)` and `outputPinId(int port)` virtual methods, both defaulting to `return -1`

- [ ] **Step 1: Add multi-pin virtuals to `component_interface.h`**

```cpp
// common/component_interface.h
#pragma once

#include "signal_node.h"
#include <string>

class IComponentEngine {
public:
    virtual ~IComponentEngine() = default;
    virtual int id() const = 0;
    virtual int graphNodeId() const = 0;
    virtual int outputPinId() const = 0;
    virtual std::string hoverSummary() const = 0;
    virtual SignalNode& node() = 0;
    virtual const SignalNode& node() const = 0;
    virtual void update(double dt) = 0;

    // Single-pin accessors (existing, unchanged)
    virtual int inputPinId() const { return -1; }

    // Multi-pin accessors (new — default return -1 for single-pin engines)
    virtual int inputPinId(int /*port*/) const { return -1; }
    virtual int outputPinId(int /*port*/) const { return -1; }
};
```

- [ ] **Step 2: Verify build**

```bash
cd build && cmake .. -G Ninja && ninja tests
```

Expected: Build succeeds — all existing components compile against the new interface with default `return -1`.

- [ ] **Step 3: Commit**

```bash
git add common/component_interface.h
git commit -m "feat: add multi-pin virtuals to IComponentEngine"
```

---
### Task 2: Add pin label support to GraphNode

**Files:**
- Modify: `node_graph/include/node_graph_engine.h`

**Interfaces:**
- Consumes: `GraphNode` struct (no prior task dependency)
- Produces: `GraphNode` with optional `input_labels` and `output_labels` vectors

- [ ] **Step 1: Add label vectors to GraphNode**

```cpp
// node_graph/include/node_graph_engine.h
struct GraphNode {
    int node_id;
    std::vector<int> input_pin_ids;
    std::vector<int> output_pin_ids;
    SignalNode *signal_node;
    std::string label;

    // Per-pin labels for rendering (empty → default "IN"/"OUT")
    std::vector<std::string> input_labels;
    std::vector<std::string> output_labels;
};
```

- [ ] **Step 2: Build to verify**

```bash
cd build && cmake .. -G Ninja && ninja tests
```

Expected: Build succeeds.

- [ ] **Step 3: Commit**

```bash
git add node_graph/include/node_graph_engine.h
git commit -m "feat: add pin label vectors to GraphNode"
```

---
### Task 3: Render pin labels in NodeGraphWidget

**Files:**
- Modify: `node_graph/src/node_graph_widget.cpp`

**Interfaces:**
- Consumes: `GraphNode::input_labels` / `output_labels` from Task 2
- Produces: Multi-port nodes show "Port N" labels; single-pin nodes continue showing "IN"/"OUT"

- [ ] **Step 1: Update `drawNodes()` to use pin labels**

Replace the input pin loop (around line 56-62):

```cpp
// Replace:
for (int pin : node.input_pin_ids) {
    ImNodes::BeginInputAttribute(pin);
    ImGui::Text("IN");
    ImNodes::EndInputAttribute();
}

// With:
for (size_t i = 0; i < node.input_pin_ids.size(); ++i) {
    ImNodes::BeginInputAttribute(node.input_pin_ids[i]);
    const char* label = (i < node.input_labels.size() && !node.input_labels[i].empty())
        ? node.input_labels[i].c_str() : "IN";
    ImGui::Text("%s", label);
    ImNodes::EndInputAttribute();
}
```

Replace the output pin loop (around line 69-84):

```cpp
// Replace:
for (int pin : node.output_pin_ids) {
    int slot = m_engine.probeSlotForPin(pin);
    if (slot >= 0) {
        ImNodes::PushColorStyle(ImNodesCol_Pin, probe_colors[slot]);
        ImNodes::PushColorStyle(ImNodesCol_PinHovered, probe_colors[slot]);
    }
    ImNodes::BeginOutputAttribute(pin);
    ImGui::Text("OUT");
    ImNodes::EndOutputAttribute();
    if (slot >= 0) {
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
    }
}

// With:
for (size_t i = 0; i < node.output_pin_ids.size(); ++i) {
    int pin = node.output_pin_ids[i];
    int slot = m_engine.probeSlotForPin(pin);
    if (slot >= 0) {
        ImNodes::PushColorStyle(ImNodesCol_Pin, probe_colors[slot]);
        ImNodes::PushColorStyle(ImNodesCol_PinHovered, probe_colors[slot]);
    }
    ImNodes::BeginOutputAttribute(pin);
    const char* label = (i < node.output_labels.size() && !node.output_labels[i].empty())
        ? node.output_labels[i].c_str() : "OUT";
    ImGui::Text("%s", label);
    ImNodes::EndOutputAttribute();
    if (slot >= 0) {
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
    }
}
```

- [ ] **Step 2: Build to verify**

```bash
cd build && cmake .. -G Ninja && ninja tests
```

Expected: Build succeeds. Visual appearance unchanged for existing components (single-pin nodes still show "IN"/"OUT").

- [ ] **Step 3: Commit**

```bash
git add node_graph/src/node_graph_widget.cpp
git commit -m "feat: render GraphNode pin labels in node widget"
```

---
### Task 4: Add multi-port support to SParamEngine

**Files:**
- Modify: `s_parametric_component/include/s_param_engine.h`
- Modify: `s_parametric_component/src/s_param_engine.cpp`

**Interfaces:**
- Consumes: `IComponentEngine::inputPinId(int)` / `outputPinId(int)` from Task 1, `GraphNode::input_labels` / `output_labels` from Task 2
- Produces: `SParamEngine` with `numPorts()`, `inputPinId(int port)`, `outputPinId(int port)`, rebuild-on-reload lifecycle, multi-port `update()`

- [ ] **Step 1: Add multi-port declarations to header**

After the existing single-pin overrides in `s_param_engine.h`:

```cpp
// s_parametric_component/include/s_param_engine.h

// In the public section, add:
    // Multi-port API
    int numPorts() const { return m_data.numPorts(); }
    int inputPinId(int port) const override;
    int outputPinId(int port) const override;
    bool fullMatrixMode() const { return m_forward_param_idx < 0; }
    void setFullMatrixMode(bool full) {
        int new_idx = full ? -1 : (m_data.numPorts() > 1 ? m_data.numPorts() : 0);
        if (new_idx != m_forward_param_idx) {
            m_forward_param_idx = new_idx;
            m_dirty = true;
        }
    }

// In the private section, add:
    void rebuildNode();
    std::vector<const Spectrum*> m_cached_input_ptrs;
    std::vector<uint64_t> m_cached_input_generations;
```

- [ ] **Step 2: Add rebuildNode() to s_param_engine.cpp**

```cpp
// s_parametric_component/src/s_param_engine.cpp

// Add after the constructor:
void SParamEngine::rebuildNode() {
    int np = m_data.numPorts();
    if (np < 1) return;

    // Remove old graph node if it exists
    if (m_graph_node_id >= 0) {
        m_graph->removeNode(m_graph_node_id);
    }

    // Create new node with np inputs and np outputs
    m_graph_node_id = m_graph->addNode("S-Param " + std::to_string(m_id), &m_node, np, np);
    m_node.inputs.resize(np);
    m_node.outputs.resize(np);

    // Set pin labels
    for (auto& node : m_graph->nodes()) {
        if (node.node_id == m_graph_node_id) {
            node.input_labels.resize(np);
            node.output_labels.resize(np);
            for (int p = 0; p < np; ++p) {
                node.input_labels[p] = "Port " + std::to_string(p + 1);
                node.output_labels[p] = "Port " + std::to_string(p + 1);
            }
            break;
        }
    }

    m_cache_valid = false;
    LOG_INFO("Rebuilt S-param node %d with %d ports", m_id, np);
}
```

- [ ] **Step 3: Update the constructor**

Replace the constructor body:

```cpp
SParamEngine::SParamEngine(int id, NodeGraphEngine& graph,
                           const std::string& filepath)
    : m_id(id), m_graph(&graph), m_filepath(filepath) {
    // Create placeholder node — will be rebuilt on first successful reload
    m_graph_node_id = graph.addNode("S-Param " + std::to_string(id), &m_node, 1, 1);
    m_node.inputs.resize(1);
    m_node.outputs.resize(1);
    reload(filepath);
}
```

- [ ] **Step 4: Update reload() for dynamic pin count**

Replace the `reload()` method body:

```cpp
void SParamEngine::reload(const std::string& filepath) {
    m_filepath = filepath;
    m_forward_param_idx = -1; // -1 = full matrix mode (new default)

    if (!m_data.load(filepath))
        return;

    int np = m_data.numPorts();

    // Rebuild graph node if pin count changed
    bool need_rebuild = (static_cast<int>(m_node.inputs.size()) != np);
    if (need_rebuild) {
        rebuildNode();
    }

    m_forward_param_idx = -1; // default: full matrix
    m_dirty = true;
    m_cache_valid = false;

    LOG_INFO("Loaded S-parameter component %d from %s (%zu points, %d ports)",
             m_id, filepath.c_str(), m_data.freqs().size(), np);
}
```

- [ ] **Step 5: Add multi-port pin accessor implementations**

```cpp
int SParamEngine::inputPinId(int port) const {
    if (!m_graph || m_graph_node_id < 0) return -1;
    for (const auto& node : m_graph->nodes()) {
        if (node.node_id == m_graph_node_id) {
            if (port >= 0 && static_cast<size_t>(port) < node.input_pin_ids.size())
                return node.input_pin_ids[port];
            break;
        }
    }
    return -1;
}

int SParamEngine::outputPinId(int port) const {
    if (!m_graph || m_graph_node_id < 0) return -1;
    for (const auto& node : m_graph->nodes()) {
        if (node.node_id == m_graph_node_id) {
            if (port >= 0 && static_cast<size_t>(port) < node.output_pin_ids.size())
                return node.output_pin_ids[port];
            break;
        }
    }
    return -1;
}
```

- [ ] **Step 6: Update `setForwardParamIdx` to accept -1 for full matrix**

```cpp
void SParamEngine::setForwardParamIdx(int idx) {
    int total = m_data.paramCount();
    if ((idx >= 0 && idx < total) || idx == -1) {
        if (idx != m_forward_param_idx) {
            m_forward_param_idx = idx;
            m_dirty = true;
        }
    }
}
```

- [ ] **Step 7: Replace update() with multi-port capable version**

Full replacement of `update()`:

```cpp
void SParamEngine::update(double dt) {
    (void)dt;

    if (!m_data.loaded()) {
        for (auto& out : m_node.outputs)
            out.bumpGeneration();
        return;
    }

    int N = m_data.numPorts();
    if (N < 1) {
        for (auto& out : m_node.outputs)
            out.bumpGeneration();
        return;
    }

    // --- Cache check ---
    bool inputs_unchanged = !m_dirty && m_cache_valid;
    if (inputs_unchanged) {
        for (int k = 0; k < N; ++k) {
            const Spectrum* in_k = (static_cast<size_t>(k) < m_node.inputs.size())
                ? m_node.inputs[k] : nullptr;
            if (in_k != m_cached_input_ptrs[k] ||
                (in_k && in_k->generation != m_cached_input_generations[k])) {
                inputs_unchanged = false;
                break;
            }
        }
    }
    if (inputs_unchanged)
        return;

    m_dirty = false;
    m_cache_valid = true;

    // Update cache
    m_cached_input_ptrs.resize(N);
    m_cached_input_generations.resize(N);
    for (int k = 0; k < N; ++k) {
        const Spectrum* in_k = (static_cast<size_t>(k) < m_node.inputs.size())
            ? m_node.inputs[k] : nullptr;
        m_cached_input_ptrs[k] = in_k;
        m_cached_input_generations[k] = in_k ? in_k->generation : 0;
    }

    // --- Legacy single-param mode ---
    if (m_forward_param_idx >= 0) {
        const Spectrum* in_ptr = m_node.inputs.empty() ? nullptr : m_node.inputs[0];
        Spectrum empty_in;
        const Spectrum& in = in_ptr ? *in_ptr : empty_in;
        m_data.applyToSpectrum(in, m_node.outputs[0], m_forward_param_idx);

        // Clear other outputs
        for (int j = 1; j < N; ++j) {
            m_node.outputs[j].frequencies.clear();
            m_node.outputs[j].tones.clear();
            m_node.outputs[j].noise_W.clear();
            m_node.outputs[j].noise_added_W.clear();
            m_node.outputs[j].noise_total_W.clear();
            m_node.outputs[j].phase_deg.clear();
            m_node.outputs[j].bumpGeneration();
        }

        // Noise figure
        if (m_nf_dB > 0.0 && !m_node.outputs[0].frequencies.empty() && m_node.outputs[0].frequencies.size() >= 2) {
            size_t Nf = m_node.outputs[0].frequencies.size();
            double Te = calculateNoiseTemp(m_nf_dB);
            m_node.outputs[0].noise_added_W.resize(Nf);
            for (size_t i = 0; i < Nf; ++i) {
                auto S = m_data.interpolate(m_node.outputs[0].frequencies[i], m_forward_param_idx);
                double gain_linear = std::norm(S);
                m_node.outputs[0].noise_added_W[i] = k * Te * gain_linear;
                m_node.outputs[0].noise_total_W[i] = m_node.outputs[0].noise_W[i] + m_node.outputs[0].noise_added_W[i];
            }
        }

        // Nonlinear processing
        if (m_nonlinear.enabled() && in_ptr && !in_ptr->tones.empty()) {
            size_t n_fund = m_node.outputs[0].tones.size();
            auto result = m_nonlinear.process(in_ptr->tones,
                [this](double freq) {
                    auto S = this->m_data.interpolate(freq, m_forward_param_idx);
                    return std::abs(S);
                });

            for (const auto& t : result.extra_tones)
                m_node.outputs[0].tones.push_back(t);

            if (result.compression_dB < -1e8) {
                for (size_t kk = 0; kk < n_fund; ++kk)
                    m_node.outputs[0].tones[kk].power_dBm = MIN_POWER;
            } else {
                for (size_t kk = 0; kk < n_fund; ++kk)
                    m_node.outputs[0].tones[kk].power_dBm += result.compression_dB;
            }
        }

        m_node.outputs[0].bumpGeneration();
        return;
    }

    // --- Full matrix mode ---
    for (int j = 0; j < N; ++j) {
        Spectrum& out = m_node.outputs[j];

        // Determine frequency grid from first connected input
        const Spectrum* grid_source = nullptr;
        for (int k = 0; k < N; ++k) {
            if (m_node.inputs[k]) {
                grid_source = m_node.inputs[k];
                break;
            }
        }

        if (grid_source && !grid_source->frequencies.empty()) {
            out.frequencies = grid_source->frequencies;
        } else if (out.frequencies.size() < 2) {
            buildDefaultFrequencyGrid(out.frequencies);
        }

        const size_t n_bins = out.frequencies.size();

        // Clear output accumulators
        out.tones.clear();

        if (grid_source && !grid_source->phase_deg.empty()) {
            out.phase_deg = grid_source->phase_deg;
        } else {
            out.phase_deg.assign(n_bins, 0.0);
        }

        out.noise_W.assign(n_bins, 0.0);
        out.noise_added_W.assign(n_bins, 0.0);

        // Accumulate contributions from all connected input ports
        for (int k = 0; k < N; ++k) {
            const Spectrum* in_k = m_node.inputs[k];
            if (!in_k) continue;

            int param_idx = j * N + k; // row-major: S_jk

            // Tones: apply S_jk complex gain to each tone
            for (const auto& tone : in_k->tones) {
                auto S = m_data.interpolate(tone.freq_Hz, param_idx);
                double mag = std::abs(S);
                double phase_shift = std::arg(S) * 180.0 / std::numbers::pi;

                Spectrum::Tone t_out;
                t_out.freq_Hz = tone.freq_Hz;
                t_out.power_dBm = tone.power_dBm + 20.0 * std::log10(mag);
                t_out.phase_deg = tone.phase_deg + phase_shift;

                // Merge with existing tone at same frequency (coherent addition)
                bool merged = false;
                for (auto& existing : out.tones) {
                    if (std::abs(existing.freq_Hz - t_out.freq_Hz) < 1.0) {
                        // Convert to complex amplitude, add, convert back
                        double a1 = std::pow(10.0, existing.power_dBm / 20.0);
                        double p1 = existing.phase_deg * std::numbers::pi / 180.0;
                        double a2 = std::pow(10.0, t_out.power_dBm / 20.0);
                        double p2 = t_out.phase_deg * std::numbers::pi / 180.0;

                        double re = a1 * std::cos(p1) + a2 * std::cos(p2);
                        double im = a1 * std::sin(p1) + a2 * std::sin(p2);

                        existing.power_dBm = 20.0 * std::log10(std::sqrt(re * re + im * im));
                        existing.phase_deg = std::atan2(im, re) * 180.0 / std::numbers::pi;
                        merged = true;
                        break;
                    }
                }
                if (!merged) {
                    out.tones.push_back(t_out);
                }
            }

            // Noise: uncorrelated → add power (|S_jk|² × input_noise)
            if (in_k->noise_total_W.empty())
                continue;

            for (size_t i = 0; i < n_bins && i < in_k->noise_total_W.size(); ++i) {
                auto S = m_data.interpolate(out.frequencies[i], param_idx);
                double gain_linear = std::norm(S);
                out.noise_W[i] += gain_linear * in_k->noise_total_W[i];
            }
        }

        // Noise figure (applied per output port)
        if (m_nf_dB > 0.0 && n_bins >= 2) {
            double Te = calculateNoiseTemp(m_nf_dB);
            for (size_t i = 0; i < n_bins; ++i) {
                // Compute total gain into this output port
                double sum_gain = 0.0;
                for (int k = 0; k < N; ++k) {
                    if (!m_node.inputs[k]) continue;
                    auto S = m_data.interpolate(out.frequencies[i], j * N + k);
                    sum_gain += std::norm(S);
                }
                out.noise_added_W[i] = k * Te * sum_gain;
            }
        } else if (n_bins >= 2) {
            out.noise_added_W.assign(n_bins, 0.0);
        }

        if (n_bins >= 2) {
            out.noise_total_W.resize(n_bins);
            for (size_t i = 0; i < n_bins; ++i)
                out.noise_total_W[i] = out.noise_W[i] + out.noise_added_W[i];
        }

        out.bumpGeneration();
    }

    // Apply nonlinearity on primary output only (port 0)
    // Multi-port nonlinearity is future work
    if (m_nonlinear.enabled() && !m_node.inputs.empty()) {
        const Spectrum* in_ptr = m_node.inputs[0];
        if (in_ptr && !in_ptr->tones.empty()) {
            size_t n_fund = m_node.outputs[0].tones.size();
            auto result = m_nonlinear.process(in_ptr->tones,
                [this](double freq) {
                    auto S = this->m_data.interpolate(freq, 0);
                    return std::abs(S);
                });

            for (const auto& t : result.extra_tones)
                m_node.outputs[0].tones.push_back(t);

            if (result.compression_dB < -1e8) {
                for (size_t kk = 0; kk < n_fund; ++kk)
                    m_node.outputs[0].tones[kk].power_dBm = MIN_POWER;
            } else {
                for (size_t kk = 0; kk < n_fund; ++kk)
                    m_node.outputs[0].tones[kk].power_dBm += result.compression_dB;
            }
        }
    }
}
```

- [ ] **Step 8: Add member fields for multi-port caching**

Add to the private section of the header:

```cpp
    // Multi-port input caching
    bool m_cache_valid = false;
    std::vector<const Spectrum*> m_cached_input_ptrs;
    std::vector<uint64_t> m_cached_input_generations;
```

No static member needed — we handle the empty-spectrum case inline with a local `Spectrum empty;`.

- [ ] **Step 9: Build to verify**

```bash
cd build && cmake .. -G Ninja && ninja tests 2>&1 | head -50
```

Expected: Build succeeds. May have linker errors for `s_empty_spectrum` or undeclared `applyNFAndNoise` — fix and rebuild.

- [ ] **Step 13: Commit**

```bash
git add s_parametric_component/include/s_param_engine.h
git add s_parametric_component/src/s_param_engine.cpp
git commit -m "feat: multi-port SParamEngine with dynamic pins and full matrix evaluation"
```

---
### Task 5: Update app routing loop for multi-port

**Files:**
- Modify: `app/src/app.cpp`

**Interfaces:**
- Consumes: `SParamEngine::numPorts()`, `IComponentEngine::inputPinId(int)` from Task 1, `IComponentEngine::node()` (existing)
- Produces: All input pins of multi-port engines get routed from upstream sources

- [ ] **Step 1: Expand `update_dsp()` routing loop**

In `app/src/app.cpp`, find the routing loop (around line 95-105):

```cpp
// Replace:
for (auto* comp : m_components.all()) {
    int pid = comp->inputPinId();
    if (pid >= 0) {
        auto* source = m_graph_engine.getSourceForInput(pid);
        comp->node().inputs[0] = source ? &source->outputs[0] : nullptr;
    }
    updates[comp->graphNodeId()] = [comp]() { comp->update(0.0); };
}

// With:
for (auto* comp : m_components.all()) {
    // Determine number of input pins (default 1 for legacy engines)
    int num_inputs = 1;
    if (auto* sp = dynamic_cast<SParamEngine*>(comp)) {
        num_inputs = sp->numPorts();
    }

    for (int k = 0; k < num_inputs; ++k) {
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

- [ ] **Step 2: Remove old test that relied on single-input routing**

No change needed — existing tests set inputs manually, not through the app routing loop.

- [ ] **Step 3: Build to verify**

```bash
cd build && cmake .. -G Ninja && ninja tests && ctest --test-dir . --output-on-failure
```

Expected: Build succeeds, all existing tests pass.

- [ ] **Step 4: Commit**

```bash
git add app/src/app.cpp
git commit -m "feat: route all input pins for multi-port S-param engines"
```

---
### Task 6: Update inspector panel for multi-port

**Files:**
- Modify: `app/src/inspector_panel.cpp`

**Interfaces:**
- Consumes: `SParamEngine::numPorts()`, `fullMatrixMode()`, `setFullMatrixMode()` from Task 4
- Produces: Inspector shows port count, full matrix mode toggle; forward param combo includes "Full Matrix" default

- [ ] **Step 1: Update `drawSParamProperties()` to show multi-port info**

In `app/src/inspector_panel.cpp`, find the `drawSParamProperties` method. Modify the forward param combo:

After the `ImGui::Text("File: ...")` and Browse button, update the combo:

```cpp
// Replace the existing combo:
int np = engine.data().numPorts();
int fwd_idx = engine.forwardParamIdx();

if (np <= 2) {
    // Legacy combo for 1/2-port: show specific S-params
    std::string preview = (fwd_idx < 0)
        ? "Full Matrix"
        : "S" + std::to_string((fwd_idx / np) + 1) + std::to_string((fwd_idx % np) + 1);
    if (ImGui::BeginCombo("Mode", preview.c_str())) {
        bool is_full = (fwd_idx < 0);
        if (ImGui::Selectable("Full Matrix", is_full))
            engine.setFullMatrixMode(true);
        for (int pi = 0; pi < np * np; ++pi) {
            std::string lbl = "S" + std::to_string((pi / np) + 1) + std::to_string((pi % np) + 1);
            if (ImGui::Selectable(lbl.c_str(), pi == fwd_idx))
                engine.setForwardParamIdx(pi);
        }
        ImGui::EndCombo();
    }
} else {
    // Multi-port: show port count info, full matrix mode by default
    ImGui::Text("Ports: %d", np);
    bool is_full = (fwd_idx < 0);
    if (ImGui::Checkbox("Full Matrix Mode", &is_full)) {
        engine.setFullMatrixMode(is_full);
    }
    if (!is_full) {
        std::string lbl = "S" + std::to_string((fwd_idx / np) + 1)
                        + std::to_string((fwd_idx % np) + 1);
        ImGui::Text("Forward Param: %s", lbl.c_str());
    }
}

ImGui::Text("Data points: %zu", engine.data().freqs().size());
ImGui::Text("Max freq: %.0f MHz", engine.data().freqs().back() / 1e6);
```

- [ ] **Step 2: Build to verify**

```bash
cd build && cmake .. -G Ninja && ninja app
```

Expected: Build succeeds.

- [ ] **Step 3: Commit**

```bash
git add app/src/inspector_panel.cpp
git commit -m "feat: inspector shows multi-port info for SParamEngine"
```

---
### Task 7: Add multi-port unit tests

**Files:**
- Modify: `tests/test_s_parameter_amplifier.cpp`

**Interfaces:**
- Consumes: `SParamEngine` with multi-port `update()`, `numPorts()`, pin accessors from Task 4
- Produces: Tests that verify 3-port splitter behavior, combiner mode, and cache invalidation

- [ ] **Step 1: Add test for 3-port loading and pin count**

```cpp
TEST_CASE("SParamEngine loads 3-port .s3p file correctly", "[sparam][multiport]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/component_data/splitters/mpd-0226ch/MPD-0226CH_CH_25C_F.s3p";
    SParamEngine sp(0, graph, path);

    REQUIRE(sp.loaded());
    REQUIRE(sp.data().numPorts() == 3);
    REQUIRE(sp.numPorts() == 3);
    REQUIRE(sp.data().params()[0].size() == 9); // 3x3 = 9 S-params

    // Default mode is full matrix
    REQUIRE(sp.fullMatrixMode());
}
```

- [ ] **Step 2: Run to verify it fails (test not discovered yet)**

```bash
cd build && cmake .. -G Ninja && ninja tests && ctest --test-dir . --output-on-failure -R sparam
```

Expected: New test not found or missing include. Actually the test is in the same file so it should be discovered once the build completes.

- [ ] **Step 3: Add test for splitter mode (one input, two outputs)**

```cpp
TEST_CASE("SParamEngine 3-port splitter: single input produces two outputs",
          "[sparam][multiport]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/component_data/splitters/mpd-0226ch/MPD-0226CH_CH_25C_F.s3p";
    SParamEngine sp(0, graph, path);
    REQUIRE(sp.loaded());
    REQUIRE(sp.numPorts() == 3);

    // Wire generator to Port 2 input (COMMON port on this splitter)
    Spectrum in_spec;
    in_spec.tones = {{2e9, -10.0, 0.0}};  // 2 GHz, -10 dBm, 0° phase
    in_spec.frequencies = {2e9};
    in_spec.noise_W = {1e-12};
    in_spec.noise_added_W = {0.0};
    in_spec.noise_total_W = {1e-12};

    // Wire only Port 2
    sp.node().inputs[0] = nullptr;  // Port 1: Z₀
    sp.node().inputs[1] = &in_spec; // Port 2: generator
    sp.node().inputs[2] = nullptr;  // Port 3: Z₀

    sp.update(0.0);

    // Port 1 output should have a tone (from S₁₂ × input₂)
    const auto& out1 = sp.node().outputs[0];
    REQUIRE(out1.tones.size() == 1);
    REQUIRE(out1.tones[0].freq_Hz == Approx(2e9));

    // Port 2 output should also have a tone (from S₂₂ × input₂)
    const auto& out2 = sp.node().outputs[1];
    REQUIRE(out2.tones.size() >= 1);

    // Port 3 output should have a tone (from S₃₂ × input₂)
    const auto& out3 = sp.node().outputs[2];
    REQUIRE(out3.tones.size() == 1);
    REQUIRE(out3.tones[0].freq_Hz == Approx(2e9));

    // For a 3-way splitter, all outputs should be at roughly the same level
    // (within a few dB — the datasheet shows ~-3.6 dB coupling)
    REQUIRE(out1.tones[0].power_dBm == Approx(-10.0 - 3.63).margin(0.5));
    REQUIRE(out3.tones[0].power_dBm == Approx(-10.0 - 3.95).margin(0.5));
}
```

- [ ] **Step 4: Run to verify it fails (missing implementation)**

```bash
cd build && cmake .. -G Ninja && ninja tests && ctest --test-dir . --output-on-failure -R "3-port|multiport"
```

- [ ] **Step 5: Add test for combiner mode (two inputs, one output)**

```cpp
TEST_CASE("SParamEngine 3-port combiner: two inputs sum at common port",
          "[sparam][multiport]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/component_data/splitters/mpd-0226ch/MPD-0226CH_CH_25C_F.s3p";
    SParamEngine sp(0, graph, path);
    REQUIRE(sp.loaded());

    // Wire two generators at different frequencies into Port 1 and Port 3
    Spectrum in_a, in_b;
    in_a.tones = {{2e9, -10.0, 0.0}};
    in_a.frequencies = {2e9};
    in_a.noise_W = {1e-12};
    in_a.noise_added_W = {0.0};
    in_a.noise_total_W = {1e-12};

    in_b.tones = {{2.001e9, -10.0, 0.0}};
    in_b.frequencies = {2.001e9};
    in_b.noise_W = {1e-12};
    in_b.noise_added_W = {0.0};
    in_b.noise_total_W = {1e-12};

    sp.node().inputs[0] = &in_a; // Port 1: gen A
    sp.node().inputs[1] = nullptr; // Port 2: Z₀
    sp.node().inputs[2] = &in_b; // Port 3: gen B

    sp.update(0.0);

    // Port 2 output should have both tones (S₂₁ × input₁ + S₂₃ × input₃)
    const auto& out = sp.node().outputs[1]; // Port 2 output
    REQUIRE(out.tones.size() >= 2);

    bool found_2g = false, found_2001 = false;
    for (const auto& t : out.tones) {
        if (std::abs(t.freq_Hz - 2e9) < 1.0) found_2g = true;
        if (std::abs(t.freq_Hz - 2.001e9) < 1.0) found_2001 = true;
    }
    REQUIRE(found_2g);
    REQUIRE(found_2001);
}
```

- [ ] **Step 6: Add test for unused input ports**

```cpp
TEST_CASE("SParamEngine disconnected input ports produce no contribution",
          "[sparam][multiport]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/component_data/splitters/mpd-0226ch/MPD-0226CH_CH_25C_F.s3p";
    SParamEngine sp(0, graph, path);
    REQUIRE(sp.loaded());

    // No inputs connected at all
    sp.node().inputs[0] = nullptr;
    sp.node().inputs[1] = nullptr;
    sp.node().inputs[2] = nullptr;

    sp.update(0.0);

    // All outputs should be empty (no tones)
    for (int j = 0; j < 3; ++j) {
        REQUIRE(sp.node().outputs[j].tones.empty());
    }
}
```

- [ ] **Step 7: Add test for cache invalidation with multiple inputs**

```cpp
TEST_CASE("SParamEngine multi-port cache invalidates correctly",
          "[sparam][multiport][cache]") {
    NodeGraphEngine graph;
    std::string path = std::string(PROJECT_SOURCE_DIR) +
        "/component_data/splitters/mpd-0226ch/MPD-0226CH_CH_25C_F.s3p";
    SParamEngine sp(0, graph, path);
    REQUIRE(sp.loaded());

    // Wire two inputs
    Spectrum in_a, in_b;
    in_a.tones = {{2e9, -10.0, 0.0}};
    in_a.frequencies = {2e9};
    in_a.noise_W = {1e-12};
    in_a.noise_added_W = {0.0};
    in_a.noise_total_W = {1e-12};
    in_a.generation = 1;

    in_b.tones = {{3e9, -20.0, 0.0}};
    in_b.frequencies = {3e9};
    in_b.noise_W = {1e-12};
    in_b.noise_added_W = {0.0};
    in_b.noise_total_W = {1e-12};
    in_b.generation = 1;

    sp.node().inputs[0] = &in_a;
    sp.node().inputs[2] = &in_b;

    sp.update(0.0);

    auto tone_count = sp.node().outputs[1].tones.size();
    REQUIRE(tone_count > 0);

    // Second update with unchanged inputs should hit cache (same generation)
    sp.update(0.0);
    REQUIRE(sp.node().outputs[1].tones.size() == tone_count);

    // Bump one input's generation — cache should miss
    in_a.generation = 2;
    sp.update(0.0);
    REQUIRE(sp.node().outputs[1].tones.size() == tone_count); // same tone count but should re-evaluate

    // Swap to new input pointer — cache should miss
    Spectrum in_c;
    in_c.tones = {{4e9, -5.0, 0.0}};
    in_c.frequencies = {4e9};
    in_c.generation = 1;
    sp.node().inputs[0] = &in_c;
    sp.update(0.0);
    REQUIRE(sp.node().outputs[1].tones.size() == 1);
    REQUIRE(sp.node().outputs[1].tones[0].freq_Hz == Approx(4e9));
}
```

- [ ] **Step 8: Build and run all tests**

```bash
cd build && cmake .. -G Ninja && ninja tests && ctest --test-dir . --output-on-failure
```

Expected: All existing tests pass, all new multi-port tests pass.

- [ ] **Step 9: Commit**

```bash
git add tests/test_s_parameter_amplifier.cpp
git commit -m "test: add multi-port SParamEngine tests with .s3p data"
```

---
### Task 8: Wire in the node graph context menu and app defaults

**Files:**
- Modify: `app/src/app.cpp`

**Interfaces:**
- Consumes: `SParamEngine` from Task 4
- Produces: "Add S-Param Component" creates a multi-port-ready engine

- [ ] **Step 1: Update the S-Param component creation callback**

In `app/src/app.cpp`, the existing callback:

```cpp
m_graph_widget->onAddSParamComponent = [this]() {
    m_components.add<SParamEngine>(m_next_component_id++, m_graph_engine, "");
};
```

This already works — no code changes needed. The engine starts with an empty filepath, stays unloaded, and the user browses a file in the inspector. When they load a multi-port .s3p/.s4p file, `reload()` triggers `rebuildNode()` with the correct pin count.

- [ ] **Step 2: No changes needed — verify by running existing test**

```bash
cd build && cmake .. -G Ninja && ninja tests && ctest --test-dir . --output-on-failure
```

Expected: All tests pass, no regression.

- [ ] **Step 3: Commit**

```bash
git add app/src/app.cpp
git commit -m "chore: no-op — app wiring already supports multi-port SParamEngine"
```
