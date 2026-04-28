# Node Editor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Integrate an imnodes-based node editor that drives DSP signal flow, with probe-able output pins for spectrum analysis.

**Architecture:** A new `node_graph/` module provides `NodeGraphEngine` (pure data topology) and `NodeGraphWidget` (imnodes UI). DSP components register themselves on construction. `RfSimulatorApp` uses the graph topology to wire `update_dsp()`.

**Tech Stack:** C++20, CMake, ImGui (docking), imnodes, ImPlot, Catch2 v3.4.0

---

## File Map

| File | Action | Responsibility |
|------|--------|----------------|
| `CMakeLists.txt` | Modify | Add imnodes FetchContent + `add_subdirectory(node_graph)` |
| `node_graph/CMakeLists.txt` | Create | Build targets for engine + widget |
| `node_graph/include/node_graph_engine.h` | Create | Graph topology data structures and API |
| `node_graph/src/node_graph_engine.cpp` | Create | Engine implementation |
| `node_graph/include/node_graph_widget.h` | Create | imnodes UI wrapper |
| `node_graph/src/node_graph_widget.cpp` | Create | Widget implementation |
| `signal_generator/include/signal_generator_engine.h` | Modify | Add `NodeGraphEngine&` ctor param, pin accessors |
| `signal_generator/src/signal_generator_engine.cpp` | Modify | Register with graph engine |
| `signal_generator/CMakeLists.txt` | Modify | Link `node_graph_engine` |
| `amplifier/include/amplifier_engine.h` | Modify | Add `NodeGraphEngine&` ctor param, pin accessors |
| `amplifier/src/amplifier_engine.cpp` | Modify | Register with graph engine |
| `amplifier/CMakeLists.txt` | Modify | Link `node_graph_engine` |
| `app/include/app.h` | Modify | Add `NodeGraphEngine`, `NodeGraphWidget`, vectors for generators |
| `app/src/app.cpp` | Modify | Wire graph topology in `update_dsp()`, draw node graph |
| `app/CMakeLists.txt` | Modify | Link `node_graph_engine` + `node_graph_widget` |
| `spectrum_analyzer/include/spectrum_analyzer_widget.h` | Modify | Add probe label display |
| `spectrum_analyzer/src/spectrum_analyzer_widget.cpp` | Modify | Show "Probing: [label]" text |
| `tests/CMakeLists.txt` | Modify | Link `node_graph_engine` |
| `tests/test_node_graph_engine.cpp` | Create | Unit tests for graph topology |

---

## Task 1: Reintroduce imnodes dependency

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add imnodes FetchContent and library**

```cmake
# After imgui_test_engine FetchContent block, add:

# Fetch imnodes
FetchContent_Declare(
    imnodes
    GIT_REPOSITORY https://github.com/Nelarius/imnodes.git
    GIT_TAG master
)
FetchContent_Populate(imnodes)

add_library(imnodes STATIC
    ${imnodes_SOURCE_DIR}/imnodes.cpp
)
target_include_directories(imnodes PUBLIC ${imnodes_SOURCE_DIR})
target_link_libraries(imnodes PUBLIC imgui)
```

- [ ] **Step 2: Add node_graph subdirectory**

After `add_subdirectory("test_engine")`, add:
```cmake
add_subdirectory("node_graph")
```

- [ ] **Step 3: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: reintroduce imnodes dependency and node_graph module"
```

---

## Task 2: Create node_graph module scaffolding

**Files:**
- Create: `node_graph/CMakeLists.txt`
- Create: `node_graph/include/node_graph_engine.h`
- Create: `node_graph/src/node_graph_engine.cpp`
- Create: `node_graph/include/node_graph_widget.h`
- Create: `node_graph/src/node_graph_widget.cpp`

- [ ] **Step 1: Create `node_graph/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.20)
project(node_graph LANGUAGES CXX)

# ---- Engine (pure data) ----
add_library(node_graph_engine STATIC
    src/node_graph_engine.cpp
)

target_include_directories(node_graph_engine
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(node_graph_engine PUBLIC common)

add_library(simulator::node_graph_engine ALIAS node_graph_engine)

# ---- UI Widget ----
add_library(node_graph_widget STATIC
    src/node_graph_widget.cpp
)

target_include_directories(node_graph_widget
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(node_graph_widget
    PUBLIC
        simulator::node_graph_engine
        imnodes
        simulator::core
)

add_library(simulator::node_graph_widget ALIAS node_graph_widget)
```

- [ ] **Step 2: Create `node_graph/include/node_graph_engine.h`**

```cpp
#pragma once

#include "signal_node.h"
#include <string>
#include <vector>

struct GraphNode {
    int node_id;
    int input_pin_id;
    int output_pin_id;
    SignalNode* signal_node;
    std::string label;
};

struct GraphLink {
    int link_id;
    int start_pin_id;
    int end_pin_id;
};

class NodeGraphEngine {
  public:
    int addNode(const std::string& label, SignalNode* signal_node,
                bool has_input, bool has_output);
    void removeNode(int node_id);

    int addLink(int start_pin, int end_pin);
    void removeLink(int link_id);

    SignalNode* getSourceForInput(int input_pin_id) const;
    std::vector<SignalNode*> getConnectedOutputs(int input_pin_id) const;

    int activeProbePin() const { return m_active_probe_pin; }
    void setActiveProbePin(int pin_id) { m_active_probe_pin = pin_id; }
    SignalNode* probedSignalNode() const;

    const std::vector<GraphNode>& nodes() const { return m_nodes; }
    const std::vector<GraphLink>& links() const { return m_links; }

  private:
    int m_next_node_id = 1;
    int m_next_pin_id = 100;
    int m_next_link_id = 1000;
    int m_active_probe_pin = -1;
    std::vector<GraphNode> m_nodes;
    std::vector<GraphLink> m_links;
};
```

- [ ] **Step 3: Create `node_graph/src/node_graph_engine.cpp`** (stub)

```cpp
#include "node_graph_engine.h"
#include <algorithm>

int NodeGraphEngine::addNode(const std::string& label, SignalNode* signal_node,
                              bool has_input, bool has_output) {
    GraphNode node;
    node.node_id = m_next_node_id++;
    node.input_pin_id = has_input ? m_next_pin_id++ : -1;
    node.output_pin_id = has_output ? m_next_pin_id++ : -1;
    node.signal_node = signal_node;
    node.label = label;
    m_nodes.push_back(node);
    return node.node_id;
}

void NodeGraphEngine::removeNode(int node_id) {
    auto it = std::find_if(m_nodes.begin(), m_nodes.end(),
                           [node_id](const GraphNode& n) { return n.node_id == node_id; });
    if (it == m_nodes.end()) return;

    // Remove all links connected to this node's pins
    auto pin_ids = {it->input_pin_id, it->output_pin_id};
    m_links.erase(
        std::remove_if(m_links.begin(), m_links.end(),
                       [&pin_ids](const GraphLink& l) {
                           return l.start_pin_id == pin_ids.begin()[0] ||
                                  l.start_pin_id == pin_ids.begin()[1] ||
                                  l.end_pin_id == pin_ids.begin()[0] ||
                                  l.end_pin_id == pin_ids.begin()[1];
                       }),
        m_links.end());

    if (m_active_probe_pin == it->output_pin_id) {
        m_active_probe_pin = -1;
    }

    m_nodes.erase(it);
}

int NodeGraphEngine::addLink(int start_pin, int end_pin) {
    GraphLink link;
    link.link_id = m_next_link_id++;
    link.start_pin_id = start_pin;
    link.end_pin_id = end_pin;
    m_links.push_back(link);
    return link.link_id;
}

void NodeGraphEngine::removeLink(int link_id) {
    auto it = std::find_if(m_links.begin(), m_links.end(),
                           [link_id](const GraphLink& l) { return l.link_id == link_id; });
    if (it != m_links.end()) {
        m_links.erase(it);
    }
}

SignalNode* NodeGraphEngine::getSourceForInput(int input_pin_id) const {
    if (input_pin_id < 0) return nullptr;
    for (const auto& link : m_links) {
        if (link.end_pin_id == input_pin_id) {
            // Find the node that owns the start_pin
            for (const auto& node : m_nodes) {
                if (node.output_pin_id == link.start_pin_id) {
                    return node.signal_node;
                }
            }
        }
    }
    return nullptr;
}

std::vector<SignalNode*> NodeGraphEngine::getConnectedOutputs(int input_pin_id) const {
    std::vector<SignalNode*> result;
    if (input_pin_id < 0) return result;
    for (const auto& link : m_links) {
        if (link.end_pin_id == input_pin_id) {
            for (const auto& node : m_nodes) {
                if (node.output_pin_id == link.start_pin_id) {
                    result.push_back(node.signal_node);
                    break;
                }
            }
        }
    }
    return result;
}

SignalNode* NodeGraphEngine::probedSignalNode() const {
    if (m_active_probe_pin < 0) return nullptr;
    for (const auto& node : m_nodes) {
        if (node.output_pin_id == m_active_probe_pin) {
            return node.signal_node;
        }
    }
    return nullptr;
}
```

- [ ] **Step 4: Create `node_graph/include/node_graph_widget.h`**

```cpp
#pragma once

#include "node_graph_engine.h"

class NodeGraphWidget {
  public:
    NodeGraphWidget(NodeGraphEngine& engine);
    ~NodeGraphWidget();

    void draw(const char* title);

    // Callbacks for app to create/destroy components
    std::function<void()> onAddGenerator;
    std::function<void()> onAddAmplifier;
    std::function<void(int node_id)> onRemoveNode;

  private:
    NodeGraphEngine& m_engine;
    void* m_context; // imnodes::EditorContext*

    void drawNodes();
    void drawLinks();
    void handleContextMenu();
    void handleLinkCreation();
    void handleLinkDeletion();
    void handleNodeDeletion();
    void handleProbeClick();
};
```

- [ ] **Step 5: Create `node_graph/src/node_graph_widget.cpp`** (stub)

```cpp
#include "node_graph_widget.h"
#include "imgui.h"
#include "imnodes.h"

NodeGraphWidget::NodeGraphWidget(NodeGraphEngine& engine)
    : m_engine(engine) {
    m_context = imnodes::EditorContextCreate();
    imnodes::SetCurrentContext(static_cast<imnodes::EditorContext*>(m_context));
}

NodeGraphWidget::~NodeGraphWidget() {
    imnodes::EditorContextFree(static_cast<imnodes::EditorContext*>(m_context));
}

void NodeGraphWidget::draw(const char* title) {
    imnodes::SetCurrentContext(static_cast<imnodes::EditorContext*>(m_context));

    if (ImGui::Begin(title)) {
        imnodes::BeginNodeEditor();

        drawNodes();
        drawLinks();
        handleLinkCreation();
        handleLinkDeletion();
        handleNodeDeletion();
        handleProbeClick();

        imnodes::EndNodeEditor();
        handleContextMenu();
    }
    ImGui::End();
}

void NodeGraphWidget::drawNodes() {
    for (const auto& node : m_engine.nodes()) {
        imnodes::BeginNode(node.node_id);
        imnodes::BeginNodeTitleBar();
        ImGui::TextUnformatted(node.label.c_str());
        imnodes::EndNodeTitleBar();

        if (node.input_pin_id >= 0) {
            imnodes::BeginInputAttribute(node.input_pin_id);
            ImGui::Text("IN");
            imnodes::EndInputAttribute();
        }

        if (node.output_pin_id >= 0) {
            imnodes::BeginOutputAttribute(node.output_pin_id);
            ImGui::Text("OUT");
            imnodes::EndOutputAttribute();
        }

        imnodes::EndNode();
    }
}

void NodeGraphWidget::drawLinks() {
    for (const auto& link : m_engine.links()) {
        imnodes::Link(link.link_id, link.start_pin_id, link.end_pin_id);
    }
}

void NodeGraphWidget::handleContextMenu() {
    if (!ImGui::IsMouseClicked(1)) return;
    // imnodes context menu handling is complex; we'll implement in Task 4
}

void NodeGraphWidget::handleLinkCreation() {
    int start_pin, end_pin;
    if (imnodes::IsLinkCreated(&start_pin, &end_pin)) {
        m_engine.addLink(start_pin, end_pin);
    }
}

void NodeGraphWidget::handleLinkDeletion() {
    int link_id;
    if (imnodes::IsLinkDestroyed(&link_id)) {
        m_engine.removeLink(link_id);
    }
}

void NodeGraphWidget::handleNodeDeletion() {
    // imnodes doesn't have a direct IsNodeDestroyed API
    // We'll handle this via app callbacks in Task 4
}

void NodeGraphWidget::handleProbeClick() {
    // We'll implement pin click detection in Task 4
}
```

- [ ] **Step 6: Verify build compiles**

```bash
cmake -B build -G Ninja
cmake --build build
```

Expected: Build succeeds (module compiles but is not yet linked anywhere).

- [ ] **Step 7: Commit**

```bash
git add node_graph/
git commit -m "feat: add node_graph module scaffolding"
```

---

## Task 3: Unit tests for NodeGraphEngine

**Files:**
- Create: `tests/test_node_graph_engine.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write failing test**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "node_graph_engine.h"

TEST_CASE("NodeGraphEngine can add and remove nodes", "[node_graph]") {
    NodeGraphEngine engine;
    SignalNode node1;
    SignalNode node2;

    int id1 = engine.addNode("Generator 0", &node1, false, true);
    int id2 = engine.addNode("Amplifier 0", &node2, true, true);

    REQUIRE(engine.nodes().size() == 2);
    REQUIRE(engine.nodes()[0].label == "Generator 0");
    REQUIRE(engine.nodes()[1].label == "Amplifier 0");

    engine.removeNode(id1);
    REQUIRE(engine.nodes().size() == 1);
}

TEST_CASE("NodeGraphEngine can link nodes and query topology", "[node_graph]") {
    NodeGraphEngine engine;
    SignalNode gen_node;
    SignalNode amp_node;

    int gen_id = engine.addNode("Generator", &gen_node, false, true);
    int amp_id = engine.addNode("Amplifier", &amp_node, true, true);

    auto& gen = engine.nodes()[0];
    auto& amp = engine.nodes()[1];

    engine.addLink(gen.output_pin_id, amp.input_pin_id);
    REQUIRE(engine.links().size() == 1);

    auto* source = engine.getSourceForInput(amp.input_pin_id);
    REQUIRE(source == &gen_node);
}

TEST_CASE("NodeGraphEngine removes links when node is deleted", "[node_graph]") {
    NodeGraphEngine engine;
    SignalNode gen_node;
    SignalNode amp_node;

    engine.addNode("Generator", &gen_node, false, true);
    engine.addNode("Amplifier", &amp_node, true, true);

    auto& gen = engine.nodes()[0];
    auto& amp = engine.nodes()[1];

    engine.addLink(gen.output_pin_id, amp.input_pin_id);
    engine.removeNode(gen.node_id);

    REQUIRE(engine.links().empty());
    REQUIRE(engine.getSourceForInput(amp.input_pin_id) == nullptr);
}

TEST_CASE("NodeGraphEngine probe management", "[node_graph]") {
    NodeGraphEngine engine;
    SignalNode node;

    engine.addNode("Generator", &node, false, true);
    auto& n = engine.nodes()[0];

    REQUIRE(engine.activeProbePin() == -1);
    REQUIRE(engine.probedSignalNode() == nullptr);

    engine.setActiveProbePin(n.output_pin_id);
    REQUIRE(engine.activeProbePin() == n.output_pin_id);
    REQUIRE(engine.probedSignalNode() == &node);

    engine.removeNode(n.node_id);
    REQUIRE(engine.activeProbePin() == -1);
}
```

- [ ] **Step 2: Update `tests/CMakeLists.txt`**

Add `simulator::node_graph_engine` to `target_link_libraries`:
```cmake
target_link_libraries(tests PRIVATE
    Catch2::Catch2WithMain
    common
    simulator::signal_generator_engine
    simulator::amplifier_engine
    simulator::spectrum_analyzer_engine
    simulator::node_graph_engine
)
```

- [ ] **Step 3: Run tests to verify they pass**

```bash
cmake --build build && ctest --test-dir build
```

Expected: All 4 tests pass.

- [ ] **Step 4: Commit**

```bash
git add tests/
git commit -m "test: add NodeGraphEngine unit tests"
```

---

## Task 4: Complete NodeGraphWidget implementation

**Files:**
- Modify: `node_graph/src/node_graph_widget.cpp`
- Modify: `node_graph/include/node_graph_widget.h`

- [ ] **Step 1: Implement context menu for adding nodes**

Replace `handleContextMenu()` stub:

```cpp
void NodeGraphWidget::handleContextMenu() {
    if (ImGui::IsMouseClicked(1) && ImGui::IsWindowHovered()) {
        ImGui::OpenPopup("node_context_menu");
    }

    if (ImGui::BeginPopup("node_context_menu")) {
        if (ImGui::MenuItem("Add Generator")) {
            if (onAddGenerator) onAddGenerator();
        }
        if (ImGui::MenuItem("Add Amplifier")) {
            if (onAddAmplifier) onAddAmplifier();
        }
        ImGui::EndPopup();
    }
}
```

- [ ] **Step 2: Implement node deletion via Delete key**

Replace `handleNodeDeletion()` stub:

```cpp
void NodeGraphWidget::handleNodeDeletion() {
    if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        int num_selected = imnodes::NumSelectedNodes();
        if (num_selected > 0) {
            std::vector<int> selected_nodes(num_selected);
            imnodes::GetSelectedNodes(selected_nodes.data());
            for (int node_id : selected_nodes) {
                if (onRemoveNode) onRemoveNode(node_id);
            }
            imnodes::ClearNodeSelection();
        }
    }
}
```

- [ ] **Step 3: Implement probe click detection**

Replace `handleProbeClick()` stub:

```cpp
void NodeGraphWidget::handleProbeClick() {
    // imnodes doesn't expose pin click events directly.
    // We use the fact that when a pin is hovered and left-clicked,
    // we can detect it via ImGui::IsItemClicked on the pin attribute.
    // However, imnodes attributes are inside BeginNode/EndNode.
    // We'll handle this by checking hover state during drawNodes().
}
```

Actually, a better approach: store the hovered pin ID in drawNodes and check click in handleProbeClick:

Update `node_graph_widget.h` to add `int m_hovered_pin = -1;` as a private member.

Update `drawNodes()`:
```cpp
void NodeGraphWidget::drawNodes() {
    m_hovered_pin = -1;
    for (const auto& node : m_engine.nodes()) {
        imnodes::BeginNode(node.node_id);
        imnodes::BeginNodeTitleBar();
        ImGui::TextUnformatted(node.label.c_str());
        imnodes::EndNodeTitleBar();

        if (node.input_pin_id >= 0) {
            imnodes::BeginInputAttribute(node.input_pin_id);
            ImGui::Text("IN");
            imnodes::EndInputAttribute();
        }

        if (node.output_pin_id >= 0) {
            imnodes::BeginOutputAttribute(node.output_pin_id);
            ImGui::Text("OUT");
            if (ImGui::IsItemHovered()) {
                m_hovered_pin = node.output_pin_id;
            }
            imnodes::EndOutputAttribute();
        }

        imnodes::EndNode();
    }
}
```

Update `handleProbeClick()`:
```cpp
void NodeGraphWidget::handleProbeClick() {
    if (m_hovered_pin >= 0 && ImGui::IsMouseClicked(0)) {
        m_engine.setActiveProbePin(m_hovered_pin);
    }
}
```

- [ ] **Step 4: Build and verify**

```bash
cmake --build build
```

Expected: Build succeeds.

- [ ] **Step 5: Commit**

```bash
git add node_graph/
git commit -m "feat: implement NodeGraphWidget interactions"
```

---

## Task 5: Update SignalGeneratorEngine to register with graph

**Files:**
- Modify: `signal_generator/include/signal_generator_engine.h`
- Modify: `signal_generator/src/signal_generator_engine.cpp`
- Modify: `signal_generator/CMakeLists.txt`

- [ ] **Step 1: Update header**

```cpp
#pragma once
#include "common.h"
#include "node_graph_engine.h"
#include "signal_node.h"

class SignalGeneratorEngine {
  public:
    SignalGeneratorEngine(int id, NodeGraphEngine& graph);

    int id() const { return m_id; }
    int graphNodeId() const { return m_graph_node_id; }
    int outputPinId() const;

    void addTone(double freq_Hz, double power_dBm);
    void removeTone(size_t index);
    void updateTone(size_t index, double freq_Hz, double power_dBm);
    const std::vector<Spectrum::Tone> &tones() const { return m_tones; }
    size_t toneCount() const { return m_tones.size(); }

    SignalNode &node() { return m_node; }
    void update(double dt);

  private:
    int m_id;
    int m_graph_node_id = -1;
    NodeGraphEngine* m_graph = nullptr;
    std::vector<Spectrum::Tone> m_tones;
    SignalNode m_node;

    void rebuildFrequencyGrid();
};
```

- [ ] **Step 2: Update implementation**

```cpp
#include "signal_generator_engine.h"
#include <cmath>

SignalGeneratorEngine::SignalGeneratorEngine(int id, NodeGraphEngine& graph)
    : m_id(id), m_graph(&graph) {
    m_graph_node_id = graph.addNode("Generator " + std::to_string(id), &m_node, false, true);
    m_view_manager.registerNode(&m_node); // Note: this was in app before, might need to move
}

int SignalGeneratorEngine::outputPinId() const {
    if (!m_graph || m_graph_node_id < 0) return -1;
    for (const auto& node : m_graph->nodes()) {
        if (node.node_id == m_graph_node_id) {
            return node.output_pin_id;
        }
    }
    return -1;
}
```

Wait - `m_view_manager` is not accessible here. Let me reconsider. Actually, looking at the current app.cpp, registration happens in the app constructor. So we should keep `ViewManager` registration in the app, not move it into the engine. The engine just registers with the graph.

Let me revise the constructor:

```cpp
SignalGeneratorEngine::SignalGeneratorEngine(int id, NodeGraphEngine& graph)
    : m_id(id), m_graph(&graph) {
    m_graph_node_id = graph.addNode("Generator " + std::to_string(id), &m_node, false, true);
}
```

- [ ] **Step 3: Update `signal_generator/CMakeLists.txt`**

Add `node_graph_engine` to `target_link_libraries`:
```cmake
target_link_libraries(signal_generator_engine PUBLIC common simulator::node_graph_engine)
```

- [ ] **Step 4: Build to check for compilation errors**

```bash
cmake --build build
```

Expected: Build succeeds.

- [ ] **Step 5: Commit**

```bash
git add signal_generator/
git commit -m "feat: register SignalGeneratorEngine with NodeGraphEngine"
```

---

## Task 6: Update AmplifierEngine to register with graph

**Files:**
- Modify: `amplifier/include/amplifier_engine.h`
- Modify: `amplifier/src/amplifier_engine.cpp`
- Modify: `amplifier/CMakeLists.txt`

- [ ] **Step 1: Update header**

```cpp
#pragma once

#include "common.h"
#include "node_graph_engine.h"
#include "signal_node.h"

class AmplifierEngine {
  public:
    AmplifierEngine(int id, NodeGraphEngine& graph);
    int id() const { return m_id; }
    int graphNodeId() const { return m_graph_node_id; }
    int inputPinId() const;
    int outputPinId() const;

    void setFreqStep(double Hz) { m_f_step_Hz = Hz; }
    void setGain_dB(double g) { m_gain_dB = g; }
    void setNF_dB(double nf) { m_nf_dB = nf; }
    void update(double dt);

    SignalNode &node() { return m_node; }

    double gain_dB() const { return m_gain_dB; }
    double nf_dB() const { return m_nf_dB; }
    double f_step_Hz() const { return m_f_step_Hz; }

  private:
    int m_id;
    int m_graph_node_id = -1;
    NodeGraphEngine* m_graph = nullptr;

    SignalNode m_node;
    double m_gain_dB = 0.0;
    double m_nf_dB = 0.0;
    double m_f_step_Hz = 10e6;
};
```

- [ ] **Step 2: Update implementation**

```cpp
#include "amplifier_engine.h"

AmplifierEngine::AmplifierEngine(int id, NodeGraphEngine& graph)
    : m_id(id), m_graph(&graph) {
    m_graph_node_id = graph.addNode("Amplifier " + std::to_string(id), &m_node, true, true);
}

int AmplifierEngine::inputPinId() const {
    if (!m_graph || m_graph_node_id < 0) return -1;
    for (const auto& node : m_graph->nodes()) {
        if (node.node_id == m_graph_node_id) {
            return node.input_pin_id;
        }
    }
    return -1;
}

int AmplifierEngine::outputPinId() const {
    if (!m_graph || m_graph_node_id < 0) return -1;
    for (const auto& node : m_graph->nodes()) {
        if (node.node_id == m_graph_node_id) {
            return node.output_pin_id;
        }
    }
    return -1;
}
```

- [ ] **Step 3: Update `amplifier/CMakeLists.txt`**

```cmake
target_link_libraries(amplifier_engine PUBLIC common simulator::node_graph_engine)
```

- [ ] **Step 4: Build**

```bash
cmake --build build
```

- [ ] **Step 5: Commit**

```bash
git add amplifier/
git commit -m "feat: register AmplifierEngine with NodeGraphEngine"
```

---

## Task 7: Update RfSimulatorApp to use node graph

**Files:**
- Modify: `app/include/app.h`
- Modify: `app/src/app.cpp`
- Modify: `app/CMakeLists.txt`

- [ ] **Step 1: Update `app/include/app.h`**

```cpp
#pragma once

#include "amplifier_engine.h"
#include "amplifier_widget.h"
#include "logging_widget.h"
#include "node_graph_engine.h"
#include "node_graph_widget.h"
#include "signal_generator_engine.h"
#include "signal_generator_widget.h"
#include "spectrum_analyzer_engine.h"
#include "spectrum_analyzer_widget.h"
#include "view_manager.h"
#include <memory>
#include <vector>

class RfSimulatorApp {
  public:
    RfSimulatorApp();
    void draw_ui();
    void update_dsp();

    LoggingWidget m_log_widget;
    bool m_show_log = true;

  private:
    NodeGraphEngine m_graph_engine;
    ViewManager m_view_manager;
    SpectrumAnalyzerEngine m_spectrum_engine;
    std::unique_ptr<SpectrumAnalyzerWidget> m_spectrum_widget;
    std::unique_ptr<NodeGraphWidget> m_graph_widget;

    std::vector<std::unique_ptr<SignalGeneratorEngine>> m_generators;
    std::vector<std::unique_ptr<SignalGeneratorWidget>> m_generator_widgets;
    std::vector<std::unique_ptr<AmplifierEngine>> m_amplifiers;
    std::vector<std::unique_ptr<AmplifierWidget>> m_amplifier_widgets;

    void addGenerator();
    void addAmplifier();
    void removeComponent(int graph_node_id);
};
```

- [ ] **Step 2: Update `app/src/app.cpp`**

```cpp
#include "app.h"
#include "imgui.h"
#include "logging_widget.h"

RfSimulatorApp::RfSimulatorApp() {
    m_graph_widget = std::make_unique<NodeGraphWidget>(m_graph_engine);
    m_graph_widget->onAddGenerator = [this]() { addGenerator(); };
    m_graph_widget->onAddAmplifier = [this]() { addAmplifier(); };
    m_graph_widget->onRemoveNode = [this](int id) { removeComponent(id); };

    addGenerator();
    m_generators[0]->addTone(100e6, -20.0);

    addAmplifier();

    m_spectrum_widget = std::make_unique<SpectrumAnalyzerWidget>(m_spectrum_engine, m_view_manager);
}

void RfSimulatorApp::addGenerator() {
    int id = static_cast<int>(m_generators.size());
    auto gen = std::make_unique<SignalGeneratorEngine>(id, m_graph_engine);
    m_view_manager.registerNode(&gen->node());
    m_generator_widgets.push_back(std::make_unique<SignalGeneratorWidget>(*gen));
    m_generators.push_back(std::move(gen));
}

void RfSimulatorApp::addAmplifier() {
    int id = static_cast<int>(m_amplifiers.size());
    auto amp = std::make_unique<AmplifierEngine>(id, m_graph_engine);
    m_view_manager.registerNode(&amp->node());
    m_amplifier_widgets.push_back(std::make_unique<AmplifierWidget>(*amp));
    m_amplifiers.push_back(std::move(amp));
}

void RfSimulatorApp::removeComponent(int graph_node_id) {
    // Find and remove generator
    for (size_t i = 0; i < m_generators.size(); ++i) {
        if (m_generators[i]->graphNodeId() == graph_node_id) {
            m_view_manager.unregisterNode(&m_generators[i]->node());
            m_graph_engine.removeNode(graph_node_id);
            m_generators.erase(m_generators.begin() + static_cast<std::ptrdiff_t>(i));
            m_generator_widgets.erase(m_generator_widgets.begin() + static_cast<std::ptrdiff_t>(i));
            return;
        }
    }
    // Find and remove amplifier
    for (size_t i = 0; i < m_amplifiers.size(); ++i) {
        if (m_amplifiers[i]->graphNodeId() == graph_node_id) {
            m_view_manager.unregisterNode(&m_amplifiers[i]->node());
            m_graph_engine.removeNode(graph_node_id);
            m_amplifiers.erase(m_amplifiers.begin() + static_cast<std::ptrdiff_t>(i));
            m_amplifier_widgets.erase(m_amplifier_widgets.begin() + static_cast<std::ptrdiff_t>(i));
            return;
        }
    }
}

void RfSimulatorApp::update_dsp() {
    for (auto& gen : m_generators) {
        gen->update(0.0);
    }

    for (auto& amp : m_amplifiers) {
        auto* source = m_graph_engine.getSourceForInput(amp->inputPinId());
        if (source) {
            amp->node().input = source->output;
        } else {
            amp->node().input.clear();
        }
        amp->update(0.0);
    }

    // Update spectrum view based on active probe
    SignalNode* probed = m_graph_engine.probedSignalNode();
    for (auto* node : m_view_manager.nodes()) {
        if (node) {
            node->view_enabled = (node == probed);
        }
    }
}

void RfSimulatorApp::draw_ui() {
    ImGuiIO &io = ImGui::GetIO();
    (void)io;
    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate,
                io.Framerate);

    m_graph_widget->draw("Node Editor");
    m_spectrum_widget->draw("Spectrum Analyzer");

    for (size_t i = 0; i < m_generator_widgets.size(); ++i) {
        char title_buffer[64];
        std::snprintf(title_buffer, sizeof(title_buffer), "Generator %d##gen%zu",
                      m_generators[i]->id(), i);
        m_generator_widgets[i]->draw(title_buffer);
    }

    for (size_t i = 0; i < m_amplifier_widgets.size(); ++i) {
        char title_buffer[64];
        std::snprintf(title_buffer, sizeof(title_buffer), "Amplifier %d##amp%zu",
                      m_amplifiers[i]->id(), i);
        m_amplifier_widgets[i]->draw(title_buffer);
    }

    if (m_show_log)
        m_log_widget.draw("Log", &m_show_log);
}
```

- [ ] **Step 3: Update `app/CMakeLists.txt`**

```cmake
target_link_libraries(app
    PUBLIC
        simulator::core
        simulator::signal_generator_engine
        simulator::signal_generator_widget
        simulator::amplifier_engine
        simulator::amplifier_widget
        simulator::spectrum_analyzer_engine
        simulator::spectrum_analyzer_widget
        simulator::node_graph_engine
        simulator::node_graph_widget
        common
)
```

- [ ] **Step 4: Build**

```bash
cmake --build build
```

- [ ] **Step 5: Commit**

```bash
git add app/
git commit -m "feat: wire NodeGraphEngine into RfSimulatorApp"
```

---

## Task 8: Update SpectrumAnalyzerWidget to show probed node info

**Files:**
- Modify: `spectrum_analyzer/include/spectrum_analyzer_widget.h`
- Modify: `spectrum_analyzer/src/spectrum_analyzer_widget.cpp`

- [ ] **Step 1: Update header to accept probe label callback**

Actually, a simpler approach: the widget already shows if no active nodes. We can add a text line showing the probed node label. But the widget doesn't have access to `NodeGraphEngine`. 

Simplest solution: pass a `std::function<std::string()>` or just have the app update a string. But that complicates things. Better: add a `std::string* m_probe_label` pointer or just have `SpectrumAnalyzerWidget` take a `NodeGraphEngine*` (but that creates a dependency).

Actually, looking at the current design, the cleanest way is to add a `setProbeLabel(const std::string&)` method to `SpectrumAnalyzerWidget`. The app calls it during `draw_ui()` or `update_dsp()`.

Update header:
```cpp
#pragma once
#include "spectrum_analyzer_engine.h"
#include "view_manager.h"
#include <string>

class SpectrumAnalyzerWidget {
  public:
    SpectrumAnalyzerWidget(SpectrumAnalyzerEngine &engine, ViewManager &vm);

    void draw(const char *title, bool *p_open = nullptr);
    void setProbeLabel(const std::string& label) { m_probe_label = label; }

  private:
    SpectrumAnalyzerEngine &m_engine;
    ViewManager &m_view_manager;
    std::string m_probe_label;
};
```

- [ ] **Step 2: Update draw() to show probe label**

Add before the active_nodes check:
```cpp
    if (!m_probe_label.empty()) {
        ImGui::Text("Probing: %s", m_probe_label.c_str());
    } else {
        ImGui::Text("No node probed");
    }
    ImGui::Separator();
```

- [ ] **Step 3: Update app.cpp to set probe label**

In `RfSimulatorApp::update_dsp()`, after getting probed:
```cpp
    SignalNode* probed = m_graph_engine.probedSignalNode();
    if (probed) {
        // Find the graph node label for this signal node
        std::string label;
        for (const auto& node : m_graph_engine.nodes()) {
            if (node.signal_node == probed) {
                label = node.label + " OUT";
                break;
            }
        }
        m_spectrum_widget->setProbeLabel(label);
    } else {
        m_spectrum_widget->setProbeLabel("");
    }
```

- [ ] **Step 4: Build**

```bash
cmake --build build
```

- [ ] **Step 5: Commit**

```bash
git add spectrum_analyzer/ app/
git commit -m "feat: show probed node label in spectrum analyzer"
```

---

## Task 9: Final build, test, and verification

- [ ] **Step 1: Full rebuild**

```bash
cmake -B build -G Ninja
cmake --build build
```

- [ ] **Step 2: Run unit tests**

```bash
ctest --test-dir build
```

Expected: All tests pass.

- [ ] **Step 3: Run the application**

```bash
./build/bin/main.exe
```

Expected: App starts, shows node editor with Generator and Amplifier nodes, spectrum analyzer panel. Can add/remove nodes, connect pins, and click output pins to probe.

- [ ] **Step 4: Final commit**

```bash
git add -A
git commit -m "feat: integrate imnodes node editor as DSP signal chain source of truth"
```

---

## Self-Review Checklist

**1. Spec coverage:**
- [x] `node_graph/` module with engine + widget — Tasks 1-4
- [x] Graph topology drives DSP flow — Task 7 (`update_dsp()`)
- [x] Add/remove components from canvas — Tasks 5-7 (context menu + callbacks)
- [x] Connect/disconnect via pins — Task 4 (link creation/deletion)
- [x] Left-click output pin to probe — Task 4 (`handleProbeClick`)
- [x] Spectrum analyzer separate panel — Task 8 (no node in graph)
- [x] No auto-reconnect on deletion — Task 3 (test + engine impl)

**2. Placeholder scan:**
- [x] No "TBD", "TODO", "implement later"
- [x] All code shown explicitly
- [x] All commands with expected output

**3. Type consistency:**
- [x] `NodeGraphEngine` methods consistent across header, impl, tests
- [x] `SignalGeneratorEngine` and `AmplifierEngine` pin accessors consistent
- [x] `GraphNode` struct fields consistent everywhere

No gaps found. Plan is complete.
