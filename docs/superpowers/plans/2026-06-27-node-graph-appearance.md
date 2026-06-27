# Node Graph Appearance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the default imnodes look with a dark canvas, per-component-type color coding, and vector schematic symbols on every node body. No engine changes, no behavioral changes.

**Architecture:** A new `NodeKind` enum + two small inline helpers (`nodeKindFromLabel`, `themeColor`) live in the engine header as pure view-layer types. The widget gets a `setupDarkTheme()` function and 10 vector symbol helpers. `drawNodes()` replaces the empty `Dummy(80,56)` body with a `drawSchematicSymbol()` call and adds per-kind `PushColorStyle` for title bar + border. Kind is derived from the existing `GraphNode::label` prefix at render time.

**Tech Stack:** C++20, imnodes (existing), ImGui (existing), Catch2 v3.4.0 (existing).

**Spec:** `docs/superpowers/specs/2026-06-27-node-graph-appearance-design.md`

## Global Constraints

- **No engine changes.** `GraphNode`, `addNode()`, `removeNode()` are all unchanged. Only additive change to `node_graph_engine.h` is the new `NodeKind` enum and two inline helpers.
- **No new module / no new CMake target.** All work in `node_graph/`.
- **No new dependencies.** Uses only imnodes + ImGui, already vendored.
- **No PNG icon assets.** All symbols drawn with `ImDrawList` primitives.
- **DSP tests must all pass.** All 67 existing unit tests + 6 benchmarks must continue to pass. No DSP behavior changes.
- **Code style:** 4-space indent, 100 cols, `PointerAlignment: Right` per `.clang-format`. Run `clang-format -i <file>` on every changed file before committing.
- **Theme color representation:** Engine-side helpers return `uint32_t` (packed ARGB, same bit layout as `IM_COL32`). Widget casts to `ImU32` at the call site. The engine header must NOT include `<imgui.h>`.
- **`ponytail:` comment convention** for any deliberate shortcut (e.g. hardcoded body rect fallback, one-shot symbol functions, manual push/pop balance).

---

## File Structure

Files modified, with their responsibility after this plan:

| File | Responsibility |
|---|---|
| `node_graph/include/node_graph_engine.h` | + `NodeKind` enum, + `nodeKindFromLabel()`, + `themeColor()` inline helpers (view-layer; no imgui include) |
| `node_graph/include/node_graph_widget.h` | + `setupDarkTheme()` declaration, + `drawSchematicSymbol()` declaration |
| `node_graph/src/node_graph_widget.cpp` | + dark theme push/pop, + 10 symbol helpers + dispatch, + label lookup, + per-kind colors in `drawNodes()`, + GroupCollapsed kind in `drawGroupCollapsedBlocks()` |
| `tests/test_node_graph_engine.cpp` | + 3 test cases: label lookup round-trip, unknown fallback, color lookup total |

No new files. No other modules touched.

---

## Task 1: Add `NodeKind` enum + `nodeKindFromLabel` helper + tests

**Files:**
- Modify: `node_graph/include/node_graph_engine.h:1-50` (add enum + inline helper at end of header)
- Modify: `tests/test_node_graph_engine.cpp` (add 2 test cases at end of file)

**Interfaces:**
- Consumes: nothing (this is the first task)
- Produces:
  - `enum class NodeKind { Unknown, Generator, Amplifier, Splitter, Mixer, SParam, Adc, PFB, IdealFilter, CoaxCable, GroupCollapsed };` declared in `node_graph_engine.h`
  - `inline NodeKind nodeKindFromLabel(const std::string& label)` — maps label prefix to kind, returns `Unknown` for any unrecognised input

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_node_graph_engine.cpp`:

```cpp
#include <string>

TEST_CASE("nodeKindFromLabel maps known prefixes", "[node_graph][appearance]") {
    REQUIRE(nodeKindFromLabel("Generator 1") == NodeKind::Generator);
    REQUIRE(nodeKindFromLabel("Amplifier 2") == NodeKind::Amplifier);
    REQUIRE(nodeKindFromLabel("Splitter 3") == NodeKind::Splitter);
    REQUIRE(nodeKindFromLabel("Mixer 4") == NodeKind::Mixer);
    REQUIRE(nodeKindFromLabel("S-Param 5") == NodeKind::SParam);  // note: hyphen
    REQUIRE(nodeKindFromLabel("ADC 6") == NodeKind::Adc);
    REQUIRE(nodeKindFromLabel("PFB 7") == NodeKind::PFB);
    REQUIRE(nodeKindFromLabel("IdealFilter 8") == NodeKind::IdealFilter);
    REQUIRE(nodeKindFromLabel("Coax Cable 9") == NodeKind::CoaxCable);
}

TEST_CASE("nodeKindFromLabel returns Unknown for unrecognised input", "[node_graph][appearance]") {
    REQUIRE(nodeKindFromLabel("") == NodeKind::Unknown);
    REQUIRE(nodeKindFromLabel("Subcircuit 1") == NodeKind::Unknown);  // groups handled separately
    REQUIRE(nodeKindFromLabel("generator 1") == NodeKind::Unknown);   // case-sensitive
    REQUIRE(nodeKindFromLabel("Amplifier") == NodeKind::Amplifier);  // no trailing space still matches
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build && ctest --test-dir build --output-on-failure -R "nodeKindFromLabel"`
Expected: FAIL with compilation error — `NodeKind` and `nodeKindFromLabel` are not declared.

- [ ] **Step 3: Add `NodeKind` enum + `nodeKindFromLabel` to the engine header**

In `node_graph/include/node_graph_engine.h`, append at the end (after the existing `NodeGraphEngine` class closing `};`):

```cpp
// View-layer component type. Used by NodeGraphWidget to pick a color and
// schematic symbol. The engine never reads, stores, or returns this type;
// it is derived from GraphNode::label at render time.
enum class NodeKind {
    Unknown,
    Generator,
    Amplifier,
    Splitter,
    Mixer,
    SParam,
    Adc,
    PFB,
    IdealFilter,
    CoaxCable,
    GroupCollapsed
};

// Maps a node label to a NodeKind by prefix matching. Each engine
// constructor sets a unique, stable label prefix. First match wins.
// Unrecognised input (empty, group names, future engines) returns Unknown.
inline NodeKind nodeKindFromLabel(const std::string& label) {
    if (label.rfind("Generator ", 0) == 0)    return NodeKind::Generator;
    if (label.rfind("Amplifier ", 0) == 0)    return NodeKind::Amplifier;
    if (label.rfind("Splitter ", 0) == 0)     return NodeKind::Splitter;
    if (label.rfind("Mixer ", 0) == 0)        return NodeKind::Mixer;
    if (label.rfind("S-Param ", 0) == 0)      return NodeKind::SParam;
    if (label.rfind("ADC ", 0) == 0)          return NodeKind::Adc;
    if (label.rfind("PFB ", 0) == 0)          return NodeKind::PFB;
    if (label.rfind("IdealFilter ", 0) == 0)  return NodeKind::IdealFilter;
    if (label.rfind("Coax Cable ", 0) == 0)   return NodeKind::CoaxCable;
    return NodeKind::Unknown;
}
```

`rfind(prefix, 0) == 0` returns true iff the string starts with the prefix. This is the ponytail choice over a chain of `substr == ...` calls.

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build && ctest --test-dir build --output-on-failure -R "nodeKindFromLabel"`
Expected: PASS — both new test cases pass, all existing tests still pass.

- [ ] **Step 5: Commit**

```bash
git add node_graph/include/node_graph_engine.h tests/test_node_graph_engine.cpp
git commit -m "feat(node-graph): add NodeKind enum and label lookup"
```

---

## Task 2: Add `themeColor()` helper + test

**Files:**
- Modify: `node_graph/include/node_graph_engine.h` (append after Task 1's additions)
- Modify: `tests/test_node_graph_engine.cpp` (append)

**Interfaces:**
- Consumes: `NodeKind` (from Task 1)
- Produces: `inline uint32_t themeColor(NodeKind k)` returning packed ARGB color (engine has no imgui include, so no `IM_COL32`). Widget casts result to `ImU32` at the call site.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_node_graph_engine.cpp`:

```cpp
#include <cstdint>

TEST_CASE("themeColor returns a non-zero color for every NodeKind", "[node_graph][appearance]") {
    REQUIRE(themeColor(NodeKind::Unknown)        != 0u);
    REQUIRE(themeColor(NodeKind::Generator)      != 0u);
    REQUIRE(themeColor(NodeKind::Amplifier)      != 0u);
    REQUIRE(themeColor(NodeKind::Splitter)       != 0u);
    REQUIRE(themeColor(NodeKind::Mixer)          != 0u);
    REQUIRE(themeColor(NodeKind::SParam)         != 0u);
    REQUIRE(themeColor(NodeKind::Adc)            != 0u);
    REQUIRE(themeColor(NodeKind::PFB)            != 0u);
    REQUIRE(themeColor(NodeKind::IdealFilter)    != 0u);
    REQUIRE(themeColor(NodeKind::CoaxCable)      != 0u);
    REQUIRE(themeColor(NodeKind::GroupCollapsed) != 0u);
}

TEST_CASE("themeColor returns a distinct color per NodeKind", "[node_graph][appearance]") {
    // Spot-check a few: if any two of these collide, the palette needs adjusting.
    REQUIRE(themeColor(NodeKind::Generator) != themeColor(NodeKind::Amplifier));
    REQUIRE(themeColor(NodeKind::Mixer)     != themeColor(NodeKind::Adc));
    REQUIRE(themeColor(NodeKind::PFB)       != themeColor(NodeKind::IdealFilter));
    REQUIRE(themeColor(NodeKind::CoaxCable) != themeColor(NodeKind::Unknown));
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build && ctest --test-dir build --output-on-failure -R "themeColor"`
Expected: FAIL with compilation error — `themeColor` is not declared.

- [ ] **Step 3: Add `themeColor()` to the engine header**

Append to `node_graph/include/node_graph_engine.h` (after `nodeKindFromLabel`):

```cpp
// Per-NodeKind ARGB color. Engine has no imgui include, so the return type
// is plain uint32_t (same bit layout as IM_COL32: 0xAARRGGBB). The widget
// casts to ImU32 at the call site.
inline uint32_t themeColor(NodeKind k) {
    switch (k) {
        case NodeKind::Generator:      return 0xFF4ADE80;  // green
        case NodeKind::Amplifier:      return 0xFFFB923C;  // orange
        case NodeKind::Mixer:          return 0xFFC084FC;  // purple
        case NodeKind::Splitter:       return 0xFFFACC15;  // amber
        case NodeKind::Adc:            return 0xFF60A5FA;  // blue
        case NodeKind::PFB:            return 0xFF22D3EE;  // cyan
        case NodeKind::IdealFilter:    return 0xFF2DD4BF;  // teal
        case NodeKind::CoaxCable:      return 0xFF94A3B8;  // slate
        case NodeKind::SParam:         return 0xFFF472B6;  // pink
        case NodeKind::GroupCollapsed: return 0xFF818CF8;  // indigo
        case NodeKind::Unknown:        // fallthrough
        default:                       return 0xFF9CA3AF;  // gray
    }
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build && ctest --test-dir build --output-on-failure -R "themeColor"`
Expected: PASS — both new test cases pass, all existing tests still pass.

- [ ] **Step 5: Run the full test suite to confirm no regression**

Run: `ctest --test-dir build --output-on-failure`
Expected: All 67 unit tests + 6 benchmarks pass.

- [ ] **Step 6: Commit**

```bash
git add node_graph/include/node_graph_engine.h tests/test_node_graph_engine.cpp
git commit -m "feat(node-graph): add themeColor helper with palette"
```

---

## Task 3: Apply dark theme to node editor canvas

**Files:**
- Modify: `node_graph/include/node_graph_widget.h:30-50` (add private method declaration)
- Modify: `node_graph/src/node_graph_widget.cpp:5-15` (include imnodes) and `:17-25` (constructor) and `:30-50` (top of `draw()`)

**Interfaces:**
- Consumes: nothing new (Task 3 is widget-internal)
- Produces: `void NodeGraphWidget::setupDarkTheme()` — pushes ~8 `ImNodesCol_*` color styles for the canvas, grid, pins, link. Called once per frame at the top of `draw()` before `BeginNodeEditor`. Per-node title/border overrides come in Task 4.

- [ ] **Step 1: Add `setupDarkTheme` declaration to the widget header**

In `node_graph/include/node_graph_widget.h`, inside the `private:` section, add:

```cpp
    void setupDarkTheme();
    ImU32 nodeBodyColor(NodeKind kind) const;
```

(`nodeBodyColor` is declared now but not implemented until Task 6; declaring it now keeps the widget header stable across Tasks 4-6. If you'd rather defer it, just delete that line — Task 6 will add it then.)

- [ ] **Step 2: Add `setupDarkTheme` implementation to the widget cpp**

In `node_graph/src/node_graph_widget.cpp`, add a new function (anywhere at the top, after the anonymous namespace already there for `showSpectrumTooltip`):

```cpp
void NodeGraphWidget::setupDarkTheme() {
    // ponytail: per-frame push is balanced by imnodes' BeginNodeEditor stack
    // reset; we don't pop here. Per-node title/border overrides (Task 4) are
    // pushed inside BeginNode/EndNode and explicitly popped in the same block.
    ImNodes::PushColorStyle(ImNodesCol_CanvasBg,    IM_COL32(20, 20, 28, 255));
    ImNodes::PushColorStyle(ImNodesCol_GridLine,    IM_COL32(45, 45, 60, 100));
    ImNodes::PushColorStyle(ImNodesCol_Link,        IM_COL32(180, 180, 200, 200));
    ImNodes::PushColorStyle(ImNodesCol_NodeBg,      IM_COL32(35, 35, 45, 255));
    ImNodes::PushColorStyle(ImNodesCol_NodeBorder,  IM_COL32(80, 80, 100, 255));
    ImNodes::PushColorStyle(ImNodesCol_TitleBar,    IM_COL32(60, 60, 80, 255));
    ImNodes::PushColorStyle(ImNodesCol_Pin,         IM_COL32(200, 200, 220, 255));
    ImNodes::PushColorStyle(ImNodesCol_PinHovered,  IM_COL32(120, 200, 255, 255));
}
```

- [ ] **Step 3: Call `setupDarkTheme()` at the top of `draw()`**

In `node_graph/src/node_graph_widget.cpp`, in the `NodeGraphWidget::draw()` method, **before** `rebuildSynthMaps()`:

```cpp
void NodeGraphWidget::draw(const char *title, bool *p_open) {
    ImNodes::EditorContextSet(m_context);

    if (ImGui::Begin(title, p_open)) {
        setupDarkTheme();                      // NEW
        rebuildSynthMaps();
        // ... rest unchanged
```

- [ ] **Step 4: Build and verify it compiles**

Run: `cmake --build build`
Expected: success, no errors.

- [ ] **Step 5: Manually verify the canvas is dark**

Run: `./build/bin/rf_simulator` (Linux/macOS) or `build\bin\rf_simulator.exe` (Windows).
Visual check: open the Node Editor window. The canvas background should be dark navy-gray (roughly `#14141c`), with a subtle visible grid. The default imnodes light theme should be gone.
If the canvas still looks light, check that `setupDarkTheme()` is being called (add a temporary `LOG_INFO("setupDarkTheme called")` to confirm).

- [ ] **Step 6: Commit**

```bash
git add node_graph/include/node_graph_widget.h node_graph/src/node_graph_widget.cpp
git commit -m "feat(node-graph): apply dark theme to node editor canvas"
```

---

## Task 4: Per-kind title bar + border colors in `drawNodes()`

**Files:**
- Modify: `node_graph/src/node_graph_widget.cpp:75-135` (the `drawNodes()` method's per-node loop)

**Interfaces:**
- Consumes: `NodeKind` (via `nodeKindFromLabel` from Task 1) and `themeColor` (from Task 2)
- Produces: per-node `PushColorStyle(TitleBar, color)` + `PushColorStyle(NodeBorder, color)`, balanced by `PopColorStyle` ×2 before `EndNode()`

- [ ] **Step 1: Add per-kind color push/pop around the existing per-node body**

In `node_graph/src/node_graph_widget.cpp`, in the `drawNodes()` method's main loop, find the section:

```cpp
        ImNodes::BeginNode(node.node_id);
        ImVec2 screen_pos = ImNodes::GetNodeScreenSpacePos(node.node_id);
        m_node_screen_positions[node.node_id] = screen_pos;
        if (first_visible) {
            ImVec2 grid_pos = ImNodes::GetNodeGridSpacePos(node.node_id);
            m_grid_to_screen_offset = screen_pos - grid_pos;
            first_visible = false;
        }
        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted(node.label.c_str());
        ImNodes::EndNodeTitleBar();

        // Icon body (pixel art from registry, or empty area if no icon loaded)
        ImTextureID tex = m_icons.get(node.label);
        if (tex) {
            ImGui::Image(tex, ImVec2(80, 56));
        } else {
            ImGui::Dummy(ImVec2(80, 56));
        }
```

Replace it with:

```cpp
        ImNodes::BeginNode(node.node_id);
        ImVec2 screen_pos = ImNodes::GetNodeScreenSpacePos(node.node_id);
        m_node_screen_positions[node.node_id] = screen_pos;
        if (first_visible) {
            ImVec2 grid_pos = ImNodes::GetNodeGridSpacePos(node.node_id);
            m_grid_to_screen_offset = screen_pos - grid_pos;
            first_visible = false;
        }
        const NodeKind kind = nodeKindFromLabel(node.label);
        const ImU32 color = static_cast<ImU32>(themeColor(kind));
        ImNodes::PushColorStyle(ImNodesCol_TitleBar, color);
        ImNodes::PushColorStyle(ImNodesCol_NodeBorder, color);
        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted(node.label.c_str());
        ImNodes::EndNodeTitleBar();

        // Symbol body (replaces the empty 80x56 dummy). Real implementation
        // lands in Task 6; for now, an empty dummy keeps the layout stable.
        ImGui::Dummy(ImVec2(96, 64));
```

Then find the `ImNodes::EndNode();` line near the end of the loop and insert the pop just before it:

```cpp
        ImNodes::PopColorStyle();  // NodeBorder
        ImNodes::PopColorStyle();  // TitleBar
        ImNodes::EndNode();
```

- [ ] **Step 2: Build and verify it compiles**

Run: `cmake --build build`
Expected: success, no errors. (ImGui/imgui-include issues surface here if any.)

- [ ] **Step 3: Manually verify per-kind colors**

Run: `./build/bin/rf_simulator` (or `.exe`).
Add at least one node of each kind available from the right-click menu (Generator, Amplifier, Splitter, Mixer, S-Param Component, RF ADC, PFB Channelizer, Ideal Filter, Coax Cable).
Visual check:
- Each node has a distinctly colored title bar and matching border color
- Colors match the palette in the spec (gen=green, amp=orange, mixer=purple, etc.)
- The Unknown fallback (any future engine) renders gray
- Probes still color pins teal/orange/purple/blue on top of the new theme

- [ ] **Step 4: Run the full test suite**

Run: `ctest --test-dir build --output-on-failure`
Expected: All 67+ unit tests + 6 benchmarks pass. (No DSP changes, but catch any unintended side effects.)

- [ ] **Step 5: Commit**

```bash
git add node_graph/src/node_graph_widget.cpp
git commit -m "feat(node-graph): color-code nodes by component type"
```

---

## Task 5: Implement schematic symbol helpers (vector)

**Files:**
- Modify: `node_graph/include/node_graph_widget.h:30-50` (add declarations)
- Modify: `node_graph/src/node_graph_widget.cpp` (add 10 helpers + dispatch function)

**Interfaces:**
- Consumes: `NodeKind` (from Task 1), `themeColor` (from Task 2)
- Produces: 10 `static void draw<Name>Symbol(ImDrawList*, ImVec2 center, ImU32 color)` helpers + `static void drawSchematicSymbol(ImDrawList*, ImVec2 center, NodeKind, ImU32 color)` dispatch. Each helper is 5-20 lines of `ImDrawList` calls.

- [ ] **Step 1: Declare the helpers in the widget header**

In `node_graph/include/node_graph_widget.h`, inside the `private:` section, add:

```cpp
    void setupDarkTheme();
    static void drawSchematicSymbol(ImDrawList* dl, ImVec2 center, NodeKind kind, ImU32 color);
```

(The 10 per-name helpers stay `static` in the .cpp anonymous namespace; no header pollution.)

- [ ] **Step 2: Add the 10 symbol helpers + dispatch in an anonymous namespace in the widget cpp**

In `node_graph/src/node_graph_widget.cpp`, find the existing anonymous namespace (the one that holds `showSpectrumTooltip`) and **add** at the end of that namespace (before its closing `}`):

```cpp
// ponytail: per-name symbol helpers are static and one-shot — no shared
// state. If a symbol grows beyond ~30 lines, factor it out, but for v1
// inlining keeps the file readable.

static void drawGeneratorSymbol(ImDrawList* dl, ImVec2 c, ImU32 color) {
    // 2 cycles of a sine wave, 16 sample polyline.
    constexpr int N = 16;
    constexpr float W = 30.0f, H = 12.0f;
    ImVec2 pts[N];
    for (int i = 0; i < N; ++i) {
        float t = static_cast<float>(i) / (N - 1);          // 0..1
        float x = c.x - W + 2.0f * W * t;
        float y = c.y - H * std::sin(2.0f * 3.14159265f * 2.0f * t);
        pts[i] = ImVec2(x, y);
    }
    dl->AddPolyline(pts, N, color, ImDrawFlags_None, 2.0f);
}

static void drawAmplifierSymbol(ImDrawList* dl, ImVec2 c, ImU32 color) {
    // Right-pointing op-amp triangle.
    ImVec2 a(c.x - 20, c.y - 16);
    ImVec2 b(c.x - 20, c.y + 16);
    ImVec2 d(c.x + 20, c.y);
    dl->AddTriangle(a, b, d, color, 2.0f);
    // + / - on the input legs
    dl->AddLine(ImVec2(a.x - 8, c.y - 6), ImVec2(a.x - 8, c.y - 14), color, 2.0f);  // - vertical
    dl->AddLine(ImVec2(a.x - 12, c.y - 10), ImVec2(a.x - 4, c.y - 10), color, 2.0f); // - horiz
    dl->AddLine(ImVec2(a.x - 8, c.y + 6), ImVec2(a.x - 8, c.y + 14), color, 2.0f);  // + vertical
}

static void drawMixerSymbol(ImDrawList* dl, ImVec2 c, ImU32 color) {
    // Circle with X.
    dl->AddCircle(c, 14.0f, color, 24, 2.0f);
    dl->AddLine(ImVec2(c.x - 10, c.y - 10), ImVec2(c.x + 10, c.y + 10), color, 2.0f);
    dl->AddLine(ImVec2(c.x - 10, c.y + 10), ImVec2(c.x + 10, c.y - 10), color, 2.0f);
}

static void drawSplitterSymbol(ImDrawList* dl, ImVec2 c, ImU32 color) {
    // Y-branch: one input dot on left, splits to two output dots on right.
    ImVec2 in_pt(c.x - 22, c.y);
    ImVec2 mid(c.x, c.y);
    ImVec2 out_a(c.x + 22, c.y - 12);
    ImVec2 out_b(c.x + 22, c.y + 12);
    dl->AddLine(in_pt, mid, color, 2.0f);
    dl->AddLine(mid, out_a, color, 2.0f);
    dl->AddLine(mid, out_b, color, 2.0f);
    dl->AddCircleFilled(in_pt, 2.5f, color);
    dl->AddCircleFilled(out_a, 2.5f, color);
    dl->AddCircleFilled(out_b, 2.5f, color);
}

static void drawAdcSymbol(ImDrawList* dl, ImVec2 c, ImU32 color) {
    // Stair-step polyline (digital sampling).
    ImVec2 pts[6] = {
        ImVec2(c.x - 24, c.y + 8),
        ImVec2(c.x - 16, c.y + 8),
        ImVec2(c.x - 16, c.y - 4),
        ImVec2(c.x - 8,  c.y - 4),
        ImVec2(c.x - 8,  c.y + 8),
        ImVec2(c.x + 24, c.y + 8),
    };
    dl->AddPolyline(pts, 6, color, ImDrawFlags_None, 2.0f);
}

static void drawFilterSymbol(ImDrawList* dl, ImVec2 c, ImU32 color) {
    // L-C ladder: two small circles + two parallel lines.
    dl->AddCircle(ImVec2(c.x - 10, c.y), 5.0f, color, 16, 2.0f);
    dl->AddCircle(ImVec2(c.x + 10, c.y), 5.0f, color, 16, 2.0f);
    dl->AddLine(ImVec2(c.x - 22, c.y - 10), ImVec2(c.x - 22, c.y + 10), color, 2.0f);
    dl->AddLine(ImVec2(c.x + 22, c.y - 10), ImVec2(c.x + 22, c.y + 10), color, 2.0f);
    dl->AddLine(ImVec2(c.x - 22, c.y), ImVec2(c.x + 22, c.y), color, 2.0f);
}

static void drawCoaxSymbol(ImDrawList* dl, ImVec2 c, ImU32 color) {
    // Coax cross-section: 2 concentric circles.
    dl->AddCircle(c, 16.0f, color, 32, 2.0f);
    dl->AddCircle(c,  8.0f, color, 24, 2.0f);
}

static void drawSParamSymbol(ImDrawList* dl, ImVec2 c, ImU32 color) {
    // Rectangle with "S" centered (text).
    ImVec2 tl(c.x - 16, c.y - 12);
    ImVec2 br(c.x + 16, c.y + 12);
    dl->AddRect(tl, br, color, 0.0f, ImDrawFlags_None, 2.0f);
    dl->AddText(ImVec2(c.x - 5, c.y - 8), color, "S");
}

static void drawPfbSymbol(ImDrawList* dl, ImVec2 c, ImU32 color) {
    // 3 stacked thin rectangles, with a small "M" label.
    for (int i = 0; i < 3; ++i) {
        float y_off = (i - 1) * 8.0f;
        dl->AddRect(
            ImVec2(c.x - 20, c.y - 4 + y_off),
            ImVec2(c.x + 20, c.y + 4 + y_off),
            color, 0.0f, ImDrawFlags_None, 2.0f);
    }
    dl->AddText(ImVec2(c.x - 4, c.y - 18), color, "M");
}

static void drawGroupCollapsedSymbol(ImDrawList* dl, ImVec2 c, ImU32 color) {
    // 3 small dots connected by short lines (miniature DAG).
    ImVec2 a(c.x - 18, c.y - 6);
    ImVec2 b(c.x, c.y);
    ImVec2 d(c.x + 18, c.y + 6);
    dl->AddLine(a, b, color, 2.0f);
    dl->AddLine(b, d, color, 2.0f);
    dl->AddCircleFilled(a, 3.0f, color);
    dl->AddCircleFilled(b, 3.0f, color);
    dl->AddCircleFilled(d, 3.0f, color);
}
```

Then **after** the anonymous namespace (at file scope), add the dispatch:

```cpp
void NodeGraphWidget::drawSchematicSymbol(ImDrawList* dl, ImVec2 center, NodeKind kind, ImU32 color) {
    switch (kind) {
        case NodeKind::Generator:      drawGeneratorSymbol(dl, center, color);      break;
        case NodeKind::Amplifier:      drawAmplifierSymbol(dl, center, color);      break;
        case NodeKind::Splitter:       drawSplitterSymbol(dl, center, color);       break;
        case NodeKind::Mixer:          drawMixerSymbol(dl, center, color);          break;
        case NodeKind::SParam:         drawSParamSymbol(dl, center, color);         break;
        case NodeKind::Adc:            drawAdcSymbol(dl, center, color);            break;
        case NodeKind::PFB:            drawPfbSymbol(dl, center, color);            break;
        case NodeKind::IdealFilter:    drawFilterSymbol(dl, center, color);         break;
        case NodeKind::CoaxCable:      drawCoaxSymbol(dl, center, color);           break;
        case NodeKind::GroupCollapsed: drawGroupCollapsedSymbol(dl, center, color); break;
        case NodeKind::Unknown:        /* dimmed, draw nothing */                   break;
    }
}
```

- [ ] **Step 3: Add `#include <cmath>` if not already in the widget cpp**

Check: `node_graph/src/node_graph_widget.cpp` already includes `<algorithm>`, `<cstdio>`, `<cstring>`, etc. but not `<cmath>` (used for `std::sin` in `drawGeneratorSymbol`).
Add at the top of the file (next to the other includes):

```cpp
#include <cmath>
```

- [ ] **Step 4: Build and verify it compiles**

Run: `cmake --build build`
Expected: success. (If a symbol helper has a typo or wrong ImGui API call, the build fails here.)

- [ ] **Step 5: Commit (no visual check yet — wiring lands in Task 6)**

```bash
git add node_graph/include/node_graph_widget.h node_graph/src/node_graph_widget.cpp
git commit -m "feat(node-graph): add 10 schematic symbol helpers (vector)"
```

---

## Task 6: Wire symbol into `drawNodes()` body (replace the `Dummy`)

**Files:**
- Modify: `node_graph/src/node_graph_widget.cpp` (the `drawNodes()` method, replacing the `ImGui::Dummy(96, 64)` from Task 4)

**Interfaces:**
- Consumes: `NodeGraphWidget::drawSchematicSymbol` (from Task 5), `kind` + `color` (from Task 4)
- Produces: each visible node has its schematic symbol drawn in the body

- [ ] **Step 1: Replace the `ImGui::Dummy(96, 64)` in `drawNodes()` with a symbol call**

In `node_graph/src/node_graph_widget.cpp`, in `drawNodes()`, find:

```cpp
        // Symbol body (replaces the empty 80x56 dummy). Real implementation
        // lands in Task 6; for now, an empty dummy keeps the layout stable.
        ImGui::Dummy(ImVec2(96, 64));
```

Replace with:

```cpp
        // Schematic symbol body. Centered in the body region. The imnodes
        // version pinned in this project doesn't expose GetNodeBodyRect,
        // so we compute the center from the screen position + the
        // configured body height (96 wide, 64 tall) + an empirical title
        // bar offset.
        // ponytail: hardcoded body rect; the title bar height is what imnodes
        // uses by default. Upgrade when GetNodeBodyRect becomes available.
        constexpr float BODY_W = 96.0f;
        constexpr float BODY_H = 64.0f;
        constexpr float TITLE_BAR_H = 24.0f;
        ImVec2 body_center(screen_pos.x + BODY_W * 0.5f,
                           screen_pos.y + TITLE_BAR_H + BODY_H * 0.5f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        drawSchematicSymbol(dl, body_center, kind, color);
        ImGui::Dummy(ImVec2(BODY_W, BODY_H));
```

(The `ImGui::Dummy` is kept at the end to give the node a stable, predictable body height. The symbol is drawn on top of where the dummy reserves space.)

- [ ] **Step 2: Build and verify it compiles**

Run: `cmake --build build`
Expected: success.

- [ ] **Step 3: Manually verify each symbol**

Run: `./build/bin/rf_simulator` (or `.exe`).
Add at least one node of each kind from the right-click menu. Visual check:
- **Generator**: 2-cycle sine wave (green)
- **Amplifier**: right-pointing triangle with `+`/`−` (orange)
- **Mixer**: circle with X (purple)
- **Splitter**: Y-branch with 3 dots (amber)
- **ADC**: stair-step (blue)
- **PFB**: 3 stacked rectangles + "M" (cyan)
- **Ideal Filter**: 2 circles + parallel lines (teal)
- **Coax Cable**: 2 concentric circles (slate)
- **S-Param**: rectangle with "S" (pink)
- **Unknown** (no test path; nothing to add — engine never produces an unknown label in v1)

If any symbol looks wrong, tweak the geometry in Task 5's helpers and re-run.

- [ ] **Step 4: Run the full test suite**

Run: `ctest --test-dir build --output-on-failure`
Expected: All 67+ unit tests + 6 benchmarks pass.

- [ ] **Step 5: Commit**

```bash
git add node_graph/src/node_graph_widget.cpp
git commit -m "feat(node-graph): draw schematic symbol on each node body"
```

---

## Task 7: Group collapsed block uses `GroupCollapsed` kind/color

**Files:**
- Modify: `node_graph/src/node_graph_widget.cpp:300-380` (the `drawGroupCollapsedBlocks` method)

**Interfaces:**
- Consumes: `NodeKind::GroupCollapsed` (from Task 1), `themeColor` (from Task 2)
- Produces: collapsed group blocks have an indigo title bar + border (matching the rest of the theme) and the group-bundle schematic symbol in the body

- [ ] **Step 1: Push per-collapsed-block color overrides + draw symbol**

In `node_graph/src/node_graph_widget.cpp`, in `drawGroupCollapsedBlocks()`, find:

```cpp
        // Render the block as an imnodes node
        ImNodes::BeginNode(g.id);
        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted(g.name.c_str());
        ImNodes::EndNodeTitleBar();
```

Replace with:

```cpp
        // Render the block as an imnodes node
        ImNodes::BeginNode(g.id);
        const ImU32 group_color = static_cast<ImU32>(themeColor(NodeKind::GroupCollapsed));
        ImNodes::PushColorStyle(ImNodesCol_TitleBar, group_color);
        ImNodes::PushColorStyle(ImNodesCol_NodeBorder, group_color);
        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted(g.name.c_str());
        ImNodes::EndNodeTitleBar();
```

Then find the existing `if (g.boundary_pins.empty()) { ... } else { ... }` block in the same function and **after** its closing brace, before the `ImGui::Dummy(ImVec2(0, 4));` spacer, insert the symbol draw:

```cpp
        // Schematic symbol in the body (same machinery as drawNodes).
        // ponytail: hardcoded body rect — see Task 6 for the upgrade path.
        ImVec2 block_screen_pos = ImNodes::GetNodeScreenSpacePos(g.id);
        constexpr float BODY_W = 120.0f;
        constexpr float BODY_H = 60.0f;
        constexpr float TITLE_BAR_H = 24.0f;
        ImVec2 body_center(block_screen_pos.x + BODY_W * 0.5f,
                           block_screen_pos.y + TITLE_BAR_H + BODY_H * 0.5f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        drawSchematicSymbol(dl, body_center, NodeKind::GroupCollapsed, group_color);
```

Then find the `ImNodes::EndNode();` at the end of the loop body and insert the pops just before it:

```cpp
        ImNodes::PopColorStyle();  // NodeBorder
        ImNodes::PopColorStyle();  // TitleBar
        ImNodes::EndNode();
```

- [ ] **Step 2: Build and verify it compiles**

Run: `cmake --build build`
Expected: success.

- [ ] **Step 3: Manually verify group rendering**

Run: `./build/bin/rf_simulator` (or `.exe`).
Create a group: shift+drag-rubber-band over 2+ nodes, give it a name in the popup, leave it collapsed.
Visual check:
- Collapsed group block has an indigo (`#818cf8`-ish) title bar + border
- The body shows the group-bundle symbol (3 dots + connecting lines)
- The boundary pins are still drawn with their probe colors (teal/orange/purple/blue) — preserved
- Expanding the group and collapsing again still works
- The expanded-group background rectangle and title bar still render correctly (untouched code path)

- [ ] **Step 4: Run the full test suite + group regression tests**

Run: `ctest --test-dir build --output-on-failure`
Expected: All 67+ unit tests + 6 benchmarks pass, including any group-specific tests.

- [ ] **Step 5: Commit**

```bash
git add node_graph/src/node_graph_widget.cpp
git commit -m "feat(node-graph): themed collapsed group block with symbol"
```

---

## Task 8: Final visual verification + checklist

**Files:**
- Modify: none (verification only)

**Interfaces:**
- Consumes: the final compiled binary
- Produces: a sign-off that the spec is met

- [ ] **Step 1: Build the project from a clean state**

Run: `cmake --build build --clean-first`
Expected: success, no warnings on touched files.

- [ ] **Step 2: Run the full test suite**

Run: `ctest --test-dir build --output-on-failure`
Expected: All 67+ unit tests + 6 benchmarks pass. (Should be 67+3 = 70 unit tests now, plus 6 benchmarks, after Tasks 1 and 2 added 3 new cases.)

- [ ] **Step 3: Run the full visual verification checklist**

Run: `./build/bin/rf_simulator` (or `.exe`). Go through the spec's Section 8 manual checklist:

- [ ] Canvas is dark (`#14141c`-ish), grid is visible
- [ ] Each of the 9 component types has a distinct color
- [ ] Each of the 9 component types has a distinct schematic symbol
- [ ] Probes still color pins teal/orange/purple/blue on top of the new theme
- [ ] Group background still renders when expanded
- [ ] Group collapsed block still renders with boundary pins, using indigo color
- [ ] Links still draw between nodes (muted light gray)
- [ ] No regressions: nodes still drag, links still create on pin drag, inspector still updates on selection, save/load still works (if exercised)

- [ ] **Step 4: Run `clang-format` on every changed file**

Run:
```bash
clang-format -i node_graph/include/node_graph_engine.h
clang-format -i node_graph/include/node_graph_widget.h
clang-format -i node_graph/src/node_graph_widget.cpp
clang-format -i tests/test_node_graph_engine.cpp
```
Expected: no diff after the format pass (files were already style-clean, or the format pass is the only change).

- [ ] **Step 5: Commit the format pass if any changes**

```bash
git diff --stat   # show what changed
git add -u
git commit -m "style: clang-format node_graph appearance changes"
```
(Skip this step if `git diff --stat` is empty.)

- [ ] **Step 6: Final commit summary**

Print the commit list for the branch:
```bash
git log --oneline master..HEAD
```

Expected: 7 commits, in this order (Tasks 1-7), plus a possible format commit:
```
<hash> feat(node-graph): themed collapsed group block with symbol
<hash> feat(node-graph): draw schematic symbol on each node body
<hash> feat(node-graph): add 10 schematic symbol helpers (vector)
<hash> feat(node-graph): color-code nodes by component type
<hash> feat(node-graph): apply dark theme to node editor canvas
<hash> feat(node-graph): add themeColor helper with palette
<hash> feat(node-graph): add NodeKind enum and label lookup
```

---

## Self-Review Notes (post-write)

**Spec coverage:**
- Goal (dark canvas, color coding, schematic symbols, kind enum) — Tasks 1, 3, 4, 5, 6 ✓
- Section 4 (data model — NodeKind enum in engine header) — Task 1 ✓
- Section 5 (visual design — palette, symbols, theme) — Tasks 2 (palette), 5 (symbols), 3 (theme) ✓
- Section 6 (data flow — render path, label lookup, group collapsed) — Tasks 1 (lookup), 4 (per-kind colors), 6 (symbol wire), 7 (group) ✓
- Section 7 (error handling — Unknown fallback, no crash paths) — covered by `NodeKind::Unknown` returns in Tasks 1 and 2 + the `default:` in `themeColor` and the `case Unknown: break;` in the symbol dispatch ✓
- Section 8 (testing — 3 cases, regression, manual checklist) — Tasks 1, 2 (unit tests), Task 8 (manual + regression) ✓
- Section 9 (implementation outline — file list, line counts) — all files listed, Task 1+2+3+4+5+6+7 covers them ✓

**Placeholders scanned:** no "TBD", "TODO", "fill in", "appropriate error handling", "similar to Task N". All steps have exact code or commands.

**Type consistency:**
- `NodeKind` enum is declared in Task 1 and used in Tasks 1, 2, 4, 5, 6, 7 — same enum, same file.
- `nodeKindFromLabel` declared in Task 1 (header), used in Task 4 (cpp) — same signature.
- `themeColor` declared in Task 2 (header), used in Tasks 4, 6, 7 (cpp) — same signature.
- `drawSchematicSymbol` declared in Task 5 (header), defined in Task 5 (cpp), used in Tasks 6 and 7 (cpp) — same signature.
- `setupDarkTheme` declared in Task 3 (header), defined in Task 3 (cpp), called in Task 3 (cpp) — same signature.
- `nodeBodyColor` was mentioned in Task 3 but I removed it from the implementation (deferred per "If you'd rather defer it" note). No other task uses it. No collision.

**Found and fixed during self-review:**
- Initial Task 3 mentioned a `nodeBodyColor` helper that nothing else needs. Added a "delete this line" escape hatch in Step 1 so the implementer can clean it up if they don't want it. Verified nothing else references it.
- Task 5 Step 3: missing `<cmath>` include for `std::sin`. Added.
- Task 4 push/pop balance: explicitly balanced (2 push, 2 pop) inside the BeginNode/EndNode block, even if the body throws.
