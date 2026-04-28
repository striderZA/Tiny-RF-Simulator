# Node Editor Design Spec

**Date:** 2026-04-28
**Feature:** ImNodes-based Node Editor for RF Simulator
**Status:** Approved

---

## 1. Overview

Integrate a node editor (using **imnodes**) into the RF Simulator so that users can visually compose the signal chain by adding/removing components and drawing connections between them. The node graph becomes the **source of truth** for DSP signal flow.

### Scope (in this iteration)
- Two component types: **Signal Generator** and **Amplifier**
- Add/remove components from the node graph canvas
- Connect/disconnect components via output→input links
- Left-click any **output pin** to probe it on the **Spectrum Analyzer**
- No Spectrum Analyzer node in the graph — it remains a separate always-visible panel

### Out of scope
- Undo/redo
- Save/load node graph layouts
- Auto-reconnect on deletion
- Multiple probe overlays (single probe only for now)
- Custom node colors per type (can be added later)

---

## 2. Architecture

### 2.1 New Module: `node_graph/`

Following the existing `engine` + `widget` pattern:

```
node_graph/
  CMakeLists.txt
  include/
    node_graph_engine.h
    node_graph_widget.h
  src/
    node_graph_engine.cpp
    node_graph_widget.cpp
```

**CMake targets:**
- `node_graph_engine` — STATIC, pure C++20, testable, no UI dependencies
- `node_graph_widget` — STATIC, depends on `imnodes` + `imgui`

### 2.2 Node Graph Engine (pure data)

```cpp
struct GraphNode {
    int node_id;
    int input_pin_id;   // -1 if source (no input)
    int output_pin_id;  // -1 if sink (no output)
    SignalNode* signal_node;
    std::string label;
};

struct GraphLink {
    int link_id;
    int start_pin_id;   // output pin
    int end_pin_id;     // input pin
};

class NodeGraphEngine {
  public:
    int addNode(const std::string& label, SignalNode* signal_node,
                bool has_input, bool has_output);
    void removeNode(int node_id);
    int addLink(int start_pin, int end_pin);
    void removeLink(int link_id);

    // Topology queries for DSP wiring
    SignalNode* getSourceForInput(int input_pin_id) const;
    std::vector<SignalNode*> getConnectedOutputs(int input_pin_id) const;

    // Probe management
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

**Key behaviors:**
- Node IDs, pin IDs, and link IDs are globally unique integers managed by the engine
- `removeNode(int node_id)` also removes all links connected to that node’s pins
    - `getSourceForInput(input_pin_id)` finds the link ending at the given input pin and returns the source node’s `SignalNode*`
- `probedSignalNode()` returns the `SignalNode*` associated with the currently probed output pin

### 2.3 Node Graph Widget (imnodes UI)

```cpp
class NodeGraphWidget {
  public:
    NodeGraphWidget(NodeGraphEngine& engine);
    void draw(const char* title);

  private:
    NodeGraphEngine& m_engine;
    imnodes::EditorContext* m_context = nullptr;

    void drawNodes();
    void drawLinks();
    void handleContextMenu();   // right-click on empty canvas
    void handleLinkCreation();  // ImNodes::IsLinkCreated
    void handleLinkDeletion();  // ImNodes::IsLinkDestroyed
    void handleNodeDeletion();  // Delete key
    void handleProbeClick();    // left-click on output pin
};
```

**Rendering:**
- Each `GraphNode` is rendered as an imnodes node with a title bar (`label`)
- Input pin (left side) if `has_input`, output pin (right side) if `has_output`
- Links are drawn by iterating `m_engine.links()` and calling `ImNodes::Link(link_id, start_pin, end_pin)`
- The **active probe pin** is rendered with a green halo/border highlight

**Interactions:**
- **Right-click on canvas:** context menu with "Add Generator", "Add Amplifier"
- **Select node + Delete key:** removes node and all connected links
- **Drag pin-to-pin:** creates a link (output→input only)
- **Left-click output pin:** sets it as the active probe for the Spectrum Analyzer
- **Click existing link + Delete:** removes the link

### 2.4 Component Registration

`SignalGeneratorEngine` and `AmplifierEngine` constructors receive a `NodeGraphEngine&` and register themselves:

```cpp
SignalGeneratorEngine::SignalGeneratorEngine(int id, NodeGraphEngine& graph)
    : m_id(id) {
    m_graph_node_id = graph.addNode("Generator " + std::to_string(id),
                                     &m_node, false, true);
}

AmplifierEngine::AmplifierEngine(int id, NodeGraphEngine& graph)
    : m_id(id) {
    m_graph_node_id = graph.addNode("Amplifier " + std::to_string(id),
                                     &m_node, true, true);
}
```

Each engine stores its `graph_node_id` and exposes `inputPinId()` / `outputPinId()` by looking them up from the graph engine.

### 2.5 DSP Wiring (`RfSimulatorApp::update_dsp()`)

```cpp
void RfSimulatorApp::update_dsp() {
    // Update all generators (sources)
    for (auto& gen : m_generators) {
        gen->update(0.0);
    }

    // Propagate through amplifiers based on graph topology
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
    for (auto* node : m_view_manager.registeredNodes()) {
        node->view_enabled = (node == probed);
    }
}
```

**Update order:** Generators first (no inputs), then amplifiers in graph traversal order. For now, a single pass is sufficient since we only have one amplifier stage. If we add cascaded amplifiers later, we may need topological sort.

### 2.6 Spectrum Analyzer Integration

- No node in the graph
- Remains a separate always-visible ImGui window
- Displays text indicating which output pin is currently probed (e.g., "Probing: Amplifier 0 OUT")
- `SpectrumAnalyzerWidget` queries `NodeGraphEngine::probedSignalNode()` to get the spectrum to render

---

## 3. UI/UX Details

### 3.1 Node Appearance

| Component | Title Color | Pins |
|-----------|-------------|------|
| Generator | Red-ish (`#e94560`) | Output only (right) |
| Amplifier | Purple-ish (`#533483`) | Input (left), Output (right) |

Node body shows a summary of key parameters (e.g., frequency/power for generator, gain/NF for amplifier).

### 3.2 Probe Highlight

- The active probe pin is drawn with a **green halo** (using ImNodes pin color override or a custom draw list rect)
- Spectrum Analyzer panel shows: "Probing: [Node Label] OUT"

### 3.3 Context Menu

Right-click on empty canvas shows:
- `Add Generator`
- `Add Amplifier`
- `---`
- `Fit to Content` (optional, imnodes built-in)

### 3.4 Deletion Behavior

- Select node + **Delete** key → node removed, all connected links removed, downstream nodes become disconnected (no auto-reconnect)
- Select link + **Delete** key → link removed
- No confirmation dialogs

---

## 4. Data Flow

```
[User interacts with NodeGraphWidget]
           |
           v
[ImNodes events: create link, delete node, probe click]
           |
           v
[NodeGraphEngine updates topology / active probe]
           |
           v
[RfSimulatorApp::update_dsp() reads topology from NodeGraphEngine]
           |
           v
[SignalNode::input / SignalNode::output updated]
           |
           v
[SpectrumAnalyzerWidget reads probed SignalNode->output]
```

---

## 5. Error Handling

- **Invalid link attempt** (input→input, output→output, or self-loop): silently ignored by `NodeGraphEngine::addLink`
- **Disconnected amplifier:** `node().input.clear()`, amplifier outputs zero/empty spectrum
- **No active probe:** Spectrum Analyzer shows "No node selected" placeholder

---

## 6. Testing Strategy

- **Unit tests for `NodeGraphEngine`:**
  - Add/remove nodes and links
  - `getSourceForInput()` correctness for simple and chained topologies
  - Probe pin get/set
  - Disconnected node handling
- **UI tests (imgui_test_engine):**
  - Add node via context menu
  - Create link between two nodes
  - Delete node and verify downstream disconnect
  - Click output pin and verify probe updates

---

## 7. Dependencies

- **imnodes** — fetched via `FetchContent_Declare` in root `CMakeLists.txt` (reintroduced from git history)
- Existing dependencies: `imgui` (docking branch), `implot`, `glfw`

---

## 8. Migration Plan

1. Reintroduce `imnodes` FetchContent in root `CMakeLists.txt`
2. Create `node_graph/` module with engine + widget
3. Update `SignalGeneratorEngine` and `AmplifierEngine` constructors to accept `NodeGraphEngine&`
4. Update `RfSimulatorApp` to own `NodeGraphEngine` and `NodeGraphWidget`
5. Replace `draw_signal_chain()` with `NodeGraphWidget::draw()`
6. Update `update_dsp()` to use graph topology
7. Update `SpectrumAnalyzerWidget` to show probed node info
8. Remove old `draw_signal_chain()` implementation
9. Add tests
10. Verify build and run

---

## 9. Open Questions / Future Work

- **Node body content:** Should we embed editable parameters directly in the node body, or keep the separate config widgets? For now, keep separate widgets (double-click node to open).
- **Multiple probes:** Shift+click to add overlay probes could be a nice future enhancement.
- **Node colors:** Hardcoded per type for now; could be user-configurable later.
- **Graph persistence:** Save/load node positions and topology to JSON (future feature).

---

## 10. Sign-off

| Role | Name | Date | Status |
|------|------|------|--------|
| Designer | AI Assistant | 2026-04-28 | Approved |
| User | Jaco | 2026-04-28 | Approved |
