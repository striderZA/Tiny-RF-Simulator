# Subcircuit Groups Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add single-level subcircuit groups to the node editor — drag-select components, display them as a single block with synthesized input/output pins, expand/collapse to show or hide internals. The DSP layer is unchanged.

**Architecture:** Widget-first with a small engine layer. `common/include/group.h` adds the `Group` and `GroupBoundaryPin` data types. `NodeGraphEngine` gains a group collection and APIs but does **not** re-route links — the DSP graph stays flat, all existing engine methods are unchanged in behavior. `NodeGraphWidget` adds rubber-band selection, group rendering (expanded background rectangle + collapsed imnodes-rendered node), a synthesized pin-id space, a phantom-node workaround (if the imnodes spike says we need it), and a "Create Subcircuit" popup. The inspector panel gains a group-mode branch driven by a new `selectedGroupId` engine state.

**Tech Stack:** C++20, Dear ImGui, ImPlot, imnodes, GLFW, Catch2 v3, imgui_test_engine.

## Global Constraints

- C++20 standard enforced via `CMAKE_CXX_STANDARD 20`
- Engines must not include `<imgui.h>` or `<implot.h>`
- All existing tests must pass without modification
- No new third-party dependencies
- No new CMake targets; the new common header joins the existing `common` interface library
- The DSP graph stays flat — `update_dsp()` in `app/src/app.cpp` is unchanged
- The existing `IComponentEngine` interface is unchanged
- The existing pin-id counter (`m_next_pin_id` starting at 100) is unchanged
- The `Group::id` is in the range `50000..99999` (disjoint from `GraphNode::node_id` which is `1..49999`)
- The `GroupBoundaryPin::id` is in the range `100000+` (disjoint from real pin ids)
- Phantom node ids (if used) are in the range `200000+`
- Snapshot editing: `member_node_ids` is frozen after `addGroup`. The only way to change membership is `removeGroup` + new `addGroup`
- All new `NodeGraphEngine` methods are additive; no existing method's signature changes
- `removeNode` extension to auto-clean groups is the only change to existing engine behavior
- The existing `SignalNode` model is unchanged

## File Structure

### New files

| File | Responsibility |
|---|---|
| `common/include/group.h` | `Group` and `GroupBoundaryPin` structs (header-only) |
| `tests/test_group.cpp` | Engine-level unit tests + integration tests for groups |
| `common/AGENTS.md` | DOX pass: child doc for `common/` ownership and contracts (created in Task 30) |

### Modified files

| File | Change |
|---|---|
| `node_graph/include/node_graph_engine.h` | Add group collection, accessors, group API methods, selection state |
| `node_graph/src/node_graph_engine.cpp` | Implement group API + extend `removeNode` cascade |
| `node_graph/include/node_graph_widget.h` | Add subcircuit state (per-frame maps, selection tracking) |
| `node_graph/src/node_graph_widget.cpp` | Add rubber-band, group rendering, synthesized id translation, probe/link translation |
| `app/src/inspector_panel.cpp` | Add `selectedGroupId` branch + `drawGroupPanel` |
| `app/include/inspector_panel.h` | Declare `drawGroupPanel` private method |
| `tests/CMakeLists.txt` | Add `test_group.cpp` to `TEST_SOURCES` |
| `test_engine/ui_tests.cpp` (or new `test_engine/ui_group_tests.cpp`) | Add UI tests for the user-visible interactions |
| `ARCHITECTURE.md` | Document `Group` data model, `NodeGraphEngine` extensions, `NodeGraphWidget` rendering rules |
| `README.md` | Mention the feature in the "Node Editor" usage table |
| `ROADMAP.md` | Mark the feature as completed |
| `AGENTS.md` (root) | Update Child DOX Index with new child docs |

### Files NOT modified

- `app/src/app.cpp` — `update_dsp()` and the component lifecycle are unchanged
- `common/include/signal_node.h`, `common/include/spectrum.h`, `common/include/component_interface.h`, `common/include/view_manager.h` — DSP-side data model is untouched
- Any DSP engine (`amplifier/`, `signal_generator/`, `mixer/`, `splitter/`, `s_parametric_component/`, `adc/`, `pfb_channelizer/`, `ideal_filter/`, `coax/`) — engines don't know about groups
- `CMakeLists.txt` (root) — no new targets

---

## Phase 0 — imnodes spike

### Task 1: imnodes API spike report

**Files:**
- Create: `docs/superpowers/plans/2026-06-21-imnodes-spike-report.md`

**Goal:** Before any feature code is written, verify that the imnodes API surface required by the design exists in the pinned imnodes revision. If any piece is missing, document the fallback so the spec's coordinate-plumbing details are correct.

**Step 1: Locate the pinned imnodes revision**

```bash
grep -n -A2 "imnodes" CMakeLists.txt
```

Expected: A `FetchContent` block pulling imnodes from a known URL. Note the URL and the GIT_TAG / GIT_SHALLOW / GIT_REPOSITORY fields.

**Step 2: Search the imnodes source for the required API**

```bash
cd build/_deps/imnodes-src  # or wherever FetchContent put it
grep -rn "GetNodeGridSpacePos\|GetNodeScreenSpacePos\|EditorContextGetPanning" .
```

Expected output (any of these is fine, the rest is fallback discussion in the report):

- `ImNodes::GetNodeGridSpacePos(int)` exists in `imnodes.h` and is exported in `imnodes_internal.h`
- `ImNodes::EditorContextResetPanning(ImNodesEditorContext*, ImVec2)` exists
- A screen↔grid transform function exists, or we confirm the editor context exposes `Style()` / `Panning()` for manual transform

**Step 3: Write the spike report**

The spike report has already been written by the controller at `docs/superpowers/plans/2026-06-21-imnodes-spike-report.md` (see commit on the branch). Read it before continuing, and update this plan's Tasks 10-16 with the corrected API names (`GetNodeGridSpacePos`, manual `EditorContextGetPanning` for transforms, and no phantom nodes). with one section per API call:

```markdown
# imnodes API Spike Report

**Date:** 2026-06-21
**Spike goal:** Verify imnodes API surface for subcircuit groups.

## API: `ImNodes::GetNodeGridSpacePos(int node_id)`

- **Status:** [present | absent | present-with-different-signature]
- **Verified at:** [file:line in imnodes source]
- **Fallback if absent:** Track positions ourselves by hooking the imnodes panning. The widget reads each node's pos after `EndNodeEditor()` and stores it in a `std::unordered_map<int, ImVec2>`. Use our own copy for centroid and rubber-band intersection.

## API: Screen↔grid transform

- **Status:** [present | absent | present-with-different-signature]
- **Verified at:** [file:line in imnodes source]
- **Fallback if absent:** Read the editor context's pan/zoom fields manually. imnodes stores `m_panning` (ImVec2) on `ImNodesEditorContext`; combine with the editor's screen-to-grid transform to convert rubber-band coordinates.

## API: Phantom node rendering

- **Status:** [works | doesn't work | not tested]
- **Verified by:** [code snippet or imnodes source ref]
- **Conclusion:** [if imnodes positions pins correctly when the owning node is not drawn this frame, the phantom workaround is not needed. Otherwise we render phantoms as described in section 9 of the spec.]

## Conclusion

[One paragraph: what the design needs to change, if anything, based on the spike results. If the API matches the design, write "No design changes needed; proceeding to phase 1."]
```

**Step 4: Commit**

```bash
git add docs/superpowers/plans/2026-06-21-imnodes-spike-report.md
git commit -m "docs: imnodes API spike report for subcircuit groups"
```

---

## Phase 1 — Data model + engine API

### Task 2: Add Group data model header

**Files:**
- Create: `common/include/group.h`

**Step 1: Write the new header**

```cpp
// common/include/group.h
#pragma once

#include <string>
#include <vector>

struct GroupBoundaryPin {
    int         id;                // synthesized, unique within the group; >= 100000
    int         internal_node_id;  // the in-group node that owns the internal pin
    int         internal_pin_id;   // the engine's real pin id
    bool        is_output;         // true = subcircuit output (link source is in-group)
    std::string label;             // "<internal node label> <pin label>"
};

struct Group {
    int         id;                // allocated by the engine from m_next_group_id; >= 50000
    std::string name;              // user-editable; default "Subcircuit N"
    std::vector<int> member_node_ids;             // frozen after creation (snapshot model)
    std::vector<GroupBoundaryPin> boundary_pins;  // recomputed by rebuildGroupBoundaryPins
    bool        collapsed = false;
};
```

**Step 2: Verify build**

```bash
cmake --build build
```

Expected: Build succeeds. The header is included by the engine header in the next task, so it must compile cleanly as a header-only file.

**Step 3: Commit**

```bash
git add common/include/group.h
git commit -m "feat: add Group and GroupBoundaryPin data types"
```

---

### Task 3: Add group collection to NodeGraphEngine

**Files:**
- Modify: `node_graph/include/node_graph_engine.h`
- Modify: `node_graph/src/node_graph_engine.cpp`

**Step 1: Add private members and basic accessors to the header**

Add to the `private:` section of `NodeGraphEngine`:

```cpp
private:
    int m_next_node_id = 1;
    int m_next_pin_id = 100;
    int m_next_link_id = 1000;
    int m_next_group_id = 50000;
    int m_next_boundary_pin_id = 100000;
    int m_selected_group_id = -1;
    std::vector<int> m_probe_pins;
    std::vector<GraphNode> m_nodes;
    std::vector<GraphLink> m_links;
    std::vector<Group> m_groups;
```

Add to the `public:` section of `NodeGraphEngine`:

```cpp
const std::vector<Group>& groups() const { return m_groups; }
const Group* groupById(int group_id) const;
int numGroups() const { return static_cast<int>(m_groups.size()); }
int selectedGroupId() const { return m_selected_group_id; }
void setSelectedGroupId(int id);
int groupIdForNode(int node_id) const;
const std::vector<int>& groupsContainingNode(int node_id) const;
```

Add the forward declarations and include near the top of the header:

```cpp
#include "group.h"
#include <unordered_map>
```

**Step 2: Add the `m_node_to_group_cache` member**

Add to the `private:` section:

```cpp
std::unordered_map<int, std::vector<int>> m_node_to_group_cache;
```

This is recomputed lazily after every `addGroup` / `removeGroup` / `removeNode` (which calls `removeGroup`).

**Step 3: Implement the basic accessors in the .cpp**

Add at the end of `node_graph/src/node_graph_engine.cpp`:

```cpp
const Group* NodeGraphEngine::groupById(int group_id) const {
    for (const auto& g : m_groups) {
        if (g.id == group_id) return &g;
    }
    return nullptr;
}

void NodeGraphEngine::setSelectedGroupId(int id) {
    m_selected_group_id = id;
}

int NodeGraphEngine::groupIdForNode(int node_id) const {
    auto it = m_node_to_group_cache.find(node_id);
    if (it == m_node_to_group_cache.end()) return -1;
    if (it->second.empty()) return -1;
    return it->second.front();
}

const std::vector<int>& NodeGraphEngine::groupsContainingNode(int node_id) const {
    static const std::vector<int> empty;
    auto it = m_node_to_group_cache.find(node_id);
    if (it != m_node_to_group_cache.end()) return it->second;
    return empty;
}
```

**Step 4: Add a private helper to rebuild the cache**

Add to the `private:` section of the header:

```cpp
void rebuildNodeToGroupCache();
```

Implement in the .cpp:

```cpp
void NodeGraphEngine::rebuildNodeToGroupCache() {
    m_node_to_group_cache.clear();
    for (const auto& g : m_groups) {
        for (int nid : g.member_node_ids) {
            m_node_to_group_cache[nid].push_back(g.id);
        }
    }
}
```

**Step 5: Verify build**

```bash
cmake --build build
```

Expected: Build succeeds.

**Step 6: Commit**

```bash
git add node_graph/include/node_graph_engine.h node_graph/src/node_graph_engine.cpp
git commit -m "feat: add group collection and accessors to NodeGraphEngine"
```

---

### Task 4: Implement addGroup with validation (TDD)

**Files:**
- Create: `tests/test_group.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `node_graph/include/node_graph_engine.h`
- Modify: `node_graph/src/node_graph_engine.cpp`

**Step 1: Add `addGroup` declaration to the engine header**

```cpp
int addGroup(std::string name, std::vector<int> member_node_ids);
```

**Step 2: Create the test file with the failing tests**

```cpp
// tests/test_group.cpp
#include <catch2/catch_test_macros.hpp>
#include "node_graph_engine.h"

TEST_CASE("NodeGraphEngine::addGroup rejects 0 members", "[group]") {
    NodeGraphEngine engine;
    SignalNode a, b;
    int id_a = engine.addNode("A", &a, 1, 1);
    (void)id_a;
    int id_b = engine.addNode("B", &b, 1, 1);
    (void)id_b;
    REQUIRE(engine.addGroup("Sub", {}) == -1);
    REQUIRE(engine.numGroups() == 0);
}

TEST_CASE("NodeGraphEngine::addGroup rejects 1 member", "[group]") {
    NodeGraphEngine engine;
    SignalNode a, b;
    int id_a = engine.addNode("A", &a, 1, 1);
    (void)id_a;
    int id_b = engine.addNode("B", &b, 1, 1);
    (void)id_b;
    int gid = engine.addGroup("Sub", {id_a});
    REQUIRE(gid == -1);
    REQUIRE(engine.numGroups() == 0);
}

TEST_CASE("NodeGraphEngine::addGroup with 2 members succeeds", "[group]") {
    NodeGraphEngine engine;
    SignalNode a, b;
    int id_a = engine.addNode("A", &a, 1, 1);
    int id_b = engine.addNode("B", &b, 1, 1);
    int gid = engine.addGroup("Sub", {id_a, id_b});
    REQUIRE(gid >= 50000);
    REQUIRE(gid < 100000);
    REQUIRE(engine.numGroups() == 1);
    const auto& g = engine.groups().front();
    REQUIRE(g.member_node_ids.size() == 2);
    REQUIRE(g.name == "Sub");
    REQUIRE(g.collapsed == false);
}

TEST_CASE("NodeGraphEngine::addGroup rejects overlapping members", "[group]") {
    NodeGraphEngine engine;
    SignalNode a, b, c;
    int id_a = engine.addNode("A", &a, 1, 1);
    int id_b = engine.addNode("B", &b, 1, 1);
    int id_c = engine.addNode("C", &c, 1, 1);
    REQUIRE(engine.addGroup("Sub1", {id_a, id_b}) >= 0);
    REQUIRE(engine.addGroup("Sub2", {id_b, id_c}) == -1);
    REQUIRE(engine.numGroups() == 1);
}

TEST_CASE("NodeGraphEngine::addGroup rejects duplicate members", "[group]") {
    NodeGraphEngine engine;
    SignalNode a, b;
    int id_a = engine.addNode("A", &a, 1, 1);
    int id_b = engine.addNode("B", &b, 1, 1);
    REQUIRE(engine.addGroup("Sub", {id_a, id_a}) == -1);
    REQUIRE(engine.addGroup("Sub", {id_a, id_b, id_a}) == -1);
    REQUIRE(engine.numGroups() == 0);
}

TEST_CASE("NodeGraphEngine::addGroup rejects unknown members", "[group]") {
    NodeGraphEngine engine;
    SignalNode a;
    int id_a = engine.addNode("A", &a, 1, 1);
    REQUIRE(engine.addGroup("Sub", {id_a, 9999}) == -1);
    REQUIRE(engine.numGroups() == 0);
}
```

**Step 3: Add to TEST_SOURCES in tests/CMakeLists.txt**

Find the existing `TEST_SOURCES` list (look for `set(TEST_SOURCES` or similar) and add `test_group.cpp` to it. The exact location depends on the existing file layout. Example:

```cmake
set(TEST_SOURCES
    test_main.cpp
    test_adc.cpp
    test_group.cpp              # <-- added
    test_node_graph_engine.cpp
    # ... etc
)
```

**Step 4: Build and run tests to verify they fail**

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "\[group\]"
```

Expected: All 6 tests fail to compile because `addGroup` is not yet defined.

**Step 5: Implement `addGroup` in node_graph_engine.cpp**

```cpp
int NodeGraphEngine::addGroup(std::string name, std::vector<int> member_node_ids) {
    if (member_node_ids.size() < 2) return -1;

    // Validate: no unknown nodes, no node already in a group
    auto find_node = [this](int nid) {
        return std::find_if(m_nodes.begin(), m_nodes.end(),
            [nid](const GraphNode& n) { return n.node_id == nid; });
    };
    for (int nid : member_node_ids) {
        if (find_node(nid) == m_nodes.end()) return -1;
        if (groupIdForNode(nid) != -1) return -1;
    }

    // Reject duplicates
    std::vector<int> sorted = member_node_ids;
    std::sort(sorted.begin(), sorted.end());
    if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end()) return -1;

    Group g;
    g.id = m_next_group_id++;
    g.name = std::move(name);
    g.member_node_ids = std::move(member_node_ids);
    g.collapsed = false;
    m_groups.push_back(std::move(g));
    rebuildNodeToGroupCache();
    return m_groups.back().id;
}
```

**Step 6: Build and run tests to verify they pass**

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "\[group\]"
```

Expected: All 6 tests pass.

**Step 7: Commit**

```bash
git add tests/test_group.cpp tests/CMakeLists.txt node_graph/include/node_graph_engine.h node_graph/src/node_graph_engine.cpp
git commit -m "feat: add addGroup with validation"
```

---

### Task 5: Implement removeGroup (TDD)

**Files:**
- Modify: `tests/test_group.cpp`
- Modify: `node_graph/include/node_graph_engine.h`
- Modify: `node_graph/src/node_graph_engine.cpp`

**Step 1: Add `removeGroup` declaration**

```cpp
void removeGroup(int group_id);
```

**Step 2: Add failing tests**

Append to `tests/test_group.cpp`:

```cpp
TEST_CASE("NodeGraphEngine::removeGroup leaves members and links intact", "[group]") {
    NodeGraphEngine engine;
    SignalNode a, b, c;
    int id_a = engine.addNode("A", &a, 1, 1);
    int id_b = engine.addNode("B", &b, 1, 1);
    int id_c = engine.addNode("C", &c, 1, 1);
    int gid = engine.addGroup("Sub", {id_a, id_b});
    engine.addLink(engine.nodes()[0].output_pin_ids[0], id_c);

    engine.removeGroup(gid);
    REQUIRE(engine.numGroups() == 0);
    REQUIRE(engine.nodes().size() == 3);
    REQUIRE(engine.links().size() == 1);
    REQUIRE(engine.groupIdForNode(id_a) == -1);
    REQUIRE(engine.groupIdForNode(id_b) == -1);
}

TEST_CASE("NodeGraphEngine::removeGroup clears selection if matching", "[group]") {
    NodeGraphEngine engine;
    SignalNode a, b;
    int id_a = engine.addNode("A", &a, 1, 1);
    int id_b = engine.addNode("B", &b, 1, 1);
    int gid = engine.addGroup("Sub", {id_a, id_b});
    engine.setSelectedGroupId(gid);
    engine.removeGroup(gid);
    REQUIRE(engine.selectedGroupId() == -1);
}

TEST_CASE("NodeGraphEngine::removeGroup is no-op for unknown id", "[group]") {
    NodeGraphEngine engine;
    SignalNode a, b;
    int id_a = engine.addNode("A", &a, 1, 1);
    int id_b = engine.addNode("B", &b, 1, 1);
    int gid = engine.addGroup("Sub", {id_a, id_b});
    engine.removeGroup(9999);
    REQUIRE(engine.numGroups() == 1);
    (void)gid;
}
```

**Step 3: Build and run tests to verify they fail**

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "removeGroup"
```

Expected: Tests fail to compile because `removeGroup` is not yet defined.

**Step 4: Implement `removeGroup`**

```cpp
void NodeGraphEngine::removeGroup(int group_id) {
    auto it = std::find_if(m_groups.begin(), m_groups.end(),
        [group_id](const Group& g) { return g.id == group_id; });
    if (it == m_groups.end()) return;

    if (m_selected_group_id == group_id) {
        m_selected_group_id = -1;
    }
    m_groups.erase(it);
    rebuildNodeToGroupCache();
}
```

**Step 5: Build and run tests to verify they pass**

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "removeGroup"
```

Expected: All 3 tests pass.

**Step 6: Commit**

```bash
git add tests/test_group.cpp node_graph/include/node_graph_engine.h node_graph/src/node_graph_engine.cpp
git commit -m "feat: add removeGroup"
```

---

### Task 6: Add renameGroup, setGroupCollapsed, isGroupCollapsed (TDD)

**Files:**
- Modify: `tests/test_group.cpp`
- Modify: `node_graph/include/node_graph_engine.h`
- Modify: `node_graph/src/node_graph_engine.cpp`

**Step 1: Add declarations**

```cpp
void renameGroup(int group_id, std::string name);
void setGroupCollapsed(int group_id, bool collapsed);
bool isGroupCollapsed(int group_id) const;
```

**Step 2: Add failing tests**

```cpp
TEST_CASE("NodeGraphEngine::renameGroup updates name", "[group]") {
    NodeGraphEngine engine;
    SignalNode a, b;
    int id_a = engine.addNode("A", &a, 1, 1);
    int id_b = engine.addNode("B", &b, 1, 1);
    int gid = engine.addGroup("Original", {id_a, id_b});
    engine.renameGroup(gid, "Renamed");
    REQUIRE(engine.groups().front().name == "Renamed");
}

TEST_CASE("NodeGraphEngine::setGroupCollapsed flips flag", "[group]") {
    NodeGraphEngine engine;
    SignalNode a, b;
    int id_a = engine.addNode("A", &a, 1, 1);
    int id_b = engine.addNode("B", &b, 1, 1);
    int gid = engine.addGroup("Sub", {id_a, id_b});
    REQUIRE_FALSE(engine.isGroupCollapsed(gid));
    engine.setGroupCollapsed(gid, true);
    REQUIRE(engine.isGroupCollapsed(gid));
    engine.setGroupCollapsed(gid, false);
    REQUIRE_FALSE(engine.isGroupCollapsed(gid));
}
```

**Step 3: Build and run tests to verify they fail**

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "renameGroup|setGroupCollapsed"
```

Expected: Tests fail to compile.

**Step 4: Implement the methods**

```cpp
void NodeGraphEngine::renameGroup(int group_id, std::string name) {
    for (auto& g : m_groups) {
        if (g.id == group_id) {
            g.name = std::move(name);
            return;
        }
    }
}

void NodeGraphEngine::setGroupCollapsed(int group_id, bool collapsed) {
    for (auto& g : m_groups) {
        if (g.id == group_id) {
            g.collapsed = collapsed;
            return;
        }
    }
}

bool NodeGraphEngine::isGroupCollapsed(int group_id) const {
    for (const auto& g : m_groups) {
        if (g.id == group_id) return g.collapsed;
    }
    return false;
}
```

**Step 5: Build and run tests to verify they pass**

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "renameGroup|setGroupCollapsed"
```

Expected: All tests pass.

**Step 6: Commit**

```bash
git add tests/test_group.cpp node_graph/include/node_graph_engine.h node_graph/src/node_graph_engine.cpp
git commit -m "feat: add renameGroup, setGroupCollapsed, isGroupCollapsed"
```

---

### Task 7: Implement rebuildGroupBoundaryPins (TDD)

**Files:**
- Modify: `tests/test_group.cpp`
- Modify: `node_graph/include/node_graph_engine.h`
- Modify: `node_graph/src/node_graph_engine.cpp`

**Step 1: Add declaration**

```cpp
void rebuildGroupBoundaryPins(int group_id);
```

**Step 2: Add failing tests**

```cpp
TEST_CASE("NodeGraphEngine::rebuildGroupBoundaryPins with no cross-boundary links", "[group]") {
    NodeGraphEngine engine;
    SignalNode a, b;
    int id_a = engine.addNode("A", &a, 1, 1);
    int id_b = engine.addNode("B", &b, 1, 1);
    int gid = engine.addGroup("Sub", {id_a, id_b});
    engine.rebuildGroupBoundaryPins(gid);
    REQUIRE(engine.groups().front().boundary_pins.empty());
}

TEST_CASE("NodeGraphEngine::rebuildGroupBoundaryPins synthesizes one pin per cross-boundary link", "[group]") {
    NodeGraphEngine engine;
    SignalNode a, b, c;
    int id_a = engine.addNode("A", &a, 1, 1);
    int id_b = engine.addNode("B", &b, 1, 1);
    int id_c = engine.addNode("C", &c, 1, 1);
    int gid = engine.addGroup("Sub", {id_a, id_b});

    // Internal link: A.out -> B.in (both inside the group)
    engine.addLink(engine.nodes()[0].output_pin_ids[0], engine.nodes()[1].input_pin_ids[0]);
    // Cross-boundary link: B.out -> C.in (B is inside, C is outside)
    engine.addLink(engine.nodes()[1].output_pin_ids[0], engine.nodes()[2].input_pin_ids[0]);

    engine.rebuildGroupBoundaryPins(gid);
    const auto& bp = engine.groups().front().boundary_pins;
    REQUIRE(bp.size() == 1);
    REQUIRE(bp[0].is_output == true);
    REQUIRE(bp[0].internal_node_id == id_b);
    REQUIRE(bp[0].internal_pin_id == engine.nodes()[1].output_pin_ids[0]);
    REQUIRE(bp[0].id >= 100000);
    REQUIRE(bp[0].label == "B OUT");
}

TEST_CASE("NodeGraphEngine::rebuildGroupBoundaryPins with input cross-boundary", "[group]") {
    NodeGraphEngine engine;
    SignalNode a, b, c;
    int id_a = engine.addNode("A", &a, 1, 1);
    int id_b = engine.addNode("B", &b, 1, 1);
    int id_c = engine.addNode("C", &c, 1, 1);
    int gid = engine.addGroup("Sub", {id_a, id_b});

    // Cross-boundary: C.out -> A.in (C is outside, A is inside)
    engine.addLink(engine.nodes()[2].output_pin_ids[0], engine.nodes()[0].input_pin_ids[0]);

    engine.rebuildGroupBoundaryPins(gid);
    const auto& bp = engine.groups().front().boundary_pins;
    REQUIRE(bp.size() == 1);
    REQUIRE(bp[0].is_output == false);
    REQUIRE(bp[0].internal_node_id == id_a);
    REQUIRE(bp[0].internal_pin_id == engine.nodes()[0].input_pin_ids[0]);
    REQUIRE(bp[0].label == "A IN");
}

TEST_CASE("NodeGraphEngine::rebuildGroupBoundaryPins ignores internal links", "[group]") {
    NodeGraphEngine engine;
    SignalNode a, b;
    int id_a = engine.addNode("A", &a, 1, 1);
    int id_b = engine.addNode("B", &b, 1, 1);
    int gid = engine.addGroup("Sub", {id_a, id_b});
    engine.addLink(engine.nodes()[0].output_pin_ids[0], engine.nodes()[1].input_pin_ids[0]);

    engine.rebuildGroupBoundaryPins(gid);
    REQUIRE(engine.groups().front().boundary_pins.empty());
}

TEST_CASE("NodeGraphEngine::rebuildGroupBoundaryPins unique IDs", "[group]") {
    NodeGraphEngine engine;
    SignalNode a, b, c, e1, e2, e3;
    int id_a = engine.addNode("A", &a, 1, 1);
    int id_b = engine.addNode("B", &b, 1, 1);
    int id_c = engine.addNode("C", &c, 1, 1);
    int id_e1 = engine.addNode("E1", &e1, 1, 1);
    int id_e2 = engine.addNode("E2", &e2, 1, 1);
    int id_e3 = engine.addNode("E3", &e3, 1, 1);
    int gid = engine.addGroup("Sub", {id_a, id_b, id_c});

    // Three cross-boundary input links: E1 -> A, E2 -> B, E3 -> C
    engine.addLink(engine.nodes()[3].output_pin_ids[0], engine.nodes()[0].input_pin_ids[0]);
    engine.addLink(engine.nodes()[4].output_pin_ids[0], engine.nodes()[1].input_pin_ids[0]);
    engine.addLink(engine.nodes()[5].output_pin_ids[0], engine.nodes()[2].input_pin_ids[0]);

    engine.rebuildGroupBoundaryPins(gid);
    const auto& bp = engine.groups().front().boundary_pins;
    REQUIRE(bp.size() == 3);
    REQUIRE(bp[0].id != bp[1].id);
    REQUIRE(bp[1].id != bp[2].id);
    REQUIRE(bp[0].id != bp[2].id);
    REQUIRE(bp[0].id >= 100000);
    REQUIRE(bp[1].id >= 100000);
    REQUIRE(bp[2].id >= 100000);
}

TEST_CASE("NodeGraphEngine::rebuildGroupBoundaryPins uses per-pin label", "[group]") {
    NodeGraphEngine engine;
    SignalNode a, b, c;
    int id_a = engine.addNode("Amp", &a, 1, 1);
    int id_b = engine.addNode("Splitter", &b, 1, 1);
    int id_c = engine.addNode("C", &c, 1, 1);
    int gid = engine.addGroup("Sub", {id_a, id_b});

    // Set per-pin label on the splitter's input
    engine.setNodePinLabels(id_b, {"RF IN"}, {"OUT 1", "OUT 2"});

    // Cross-boundary: C.out -> Splitter.in
    engine.addLink(engine.nodes()[2].output_pin_ids[0], engine.nodes()[1].input_pin_ids[0]);

    engine.rebuildGroupBoundaryPins(gid);
    const auto& bp = engine.groups().front().boundary_pins;
    REQUIRE(bp.size() == 1);
    REQUIRE(bp[0].label == "Splitter RF IN");
}
```

**Step 3: Build and run tests to verify they fail**

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "rebuildGroupBoundaryPins"
```

Expected: Tests fail to compile.

**Step 4: Implement `rebuildGroupBoundaryPins`**

```cpp
void NodeGraphEngine::rebuildGroupBoundaryPins(int group_id) {
    auto* g = groupById(group_id);
    if (!g) return;

    // Build a set of member node ids for O(1) lookup
    std::unordered_set<int> members(g->member_node_ids.begin(), g->member_node_ids.end());

    // Helper: find the node id that owns a pin
    auto node_for_pin = [this](int pin_id) -> int {
        for (const auto& n : m_nodes) {
            for (int p : n.input_pin_ids) if (p == pin_id) return n.node_id;
            for (int p : n.output_pin_ids) if (p == pin_id) return n.node_id;
        }
        return -1;
    };

    // Helper: get label for a node
    auto node_label = [this](int nid) -> std::string {
        for (const auto& n : m_nodes) {
            if (n.node_id == nid) return n.label;
        }
        return "";
    };

    // Helper: get pin label for a node's input or output
    auto pin_label = [](const GraphNode& n, int pin_id, bool is_output) -> std::string {
        const auto& labels = is_output ? n.output_labels : n.input_labels;
        const auto& pins = is_output ? n.output_pin_ids : n.input_pin_ids;
        for (size_t i = 0; i < pins.size(); ++i) {
            if (pins[i] == pin_id) {
                if (i < labels.size() && !labels[i].empty()) return labels[i];
                return is_output ? "OUT" : "IN";
            }
        }
        return is_output ? "OUT" : "IN";
    };

    std::vector<GroupBoundaryPin> new_pins;
    for (const auto& link : m_links) {
        int start_node = node_for_pin(link.start_pin_id);
        int end_node = node_for_pin(link.end_pin_id);
        if (start_node < 0 || end_node < 0) continue;

        bool start_in = members.count(start_node) > 0;
        bool end_in = members.count(end_node) > 0;
        if (start_in && end_in) continue;            // internal link
        if (!start_in && !end_in) continue;          // external link

        GroupBoundaryPin bp;
        bp.id = m_next_boundary_pin_id++;
        if (start_in) {
            // source is in-group; boundary pin is an output
            bp.is_output = true;
            bp.internal_node_id = start_node;
            bp.internal_pin_id = link.start_pin_id;
            for (const auto& n : m_nodes) {
                if (n.node_id == start_node) {
                    bp.label = node_label(start_node) + " " + pin_label(n, link.start_pin_id, true);
                    break;
                }
            }
        } else {
            // target is in-group; boundary pin is an input
            bp.is_output = false;
            bp.internal_node_id = end_node;
            bp.internal_pin_id = link.end_pin_id;
            for (const auto& n : m_nodes) {
                if (n.node_id == end_node) {
                    bp.label = node_label(end_node) + " " + pin_label(n, link.end_pin_id, false);
                    break;
                }
            }
        }
        new_pins.push_back(std::move(bp));
    }
    g->boundary_pins = std::move(new_pins);
}
```

**Step 5: Build and run tests to verify they pass**

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "rebuildGroupBoundaryPins"
```

Expected: All tests pass.

**Step 6: Commit**

```bash
git add tests/test_group.cpp node_graph/include/node_graph_engine.h node_graph/src/node_graph_engine.cpp
git commit -m "feat: add rebuildGroupBoundaryPins"
```

---

### Task 8: Extend removeNode with group auto-cleanup (TDD)

**Files:**
- Modify: `tests/test_group.cpp`
- Modify: `node_graph/src/node_graph_engine.cpp`

**Step 1: Add failing tests**

```cpp
TEST_CASE("NodeGraphEngine::removeNode auto-removes group when last member removed", "[group]") {
    NodeGraphEngine engine;
    SignalNode a, b;
    int id_a = engine.addNode("A", &a, 1, 1);
    int id_b = engine.addNode("B", &b, 1, 1);
    int gid = engine.addGroup("Sub", {id_a, id_b});
    (void)gid;

    engine.removeNode(id_a);
    REQUIRE(engine.numGroups() == 0);
}

TEST_CASE("NodeGraphEngine::removeNode auto-removes group when count drops below 2", "[group]") {
    NodeGraphEngine engine;
    SignalNode a, b, c;
    int id_a = engine.addNode("A", &a, 1, 1);
    int id_b = engine.addNode("B", &b, 1, 1);
    int id_c = engine.addNode("C", &c, 1, 1);
    int gid = engine.addGroup("Sub", {id_a, id_b, id_c});
    (void)gid;

    engine.removeNode(id_a);
    engine.removeNode(id_b);
    REQUIRE(engine.numGroups() == 0);
}

TEST_CASE("NodeGraphEngine::removeNode leaves group intact when >= 2 members remain", "[group]") {
    NodeGraphEngine engine;
    SignalNode a, b, c;
    int id_a = engine.addNode("A", &a, 1, 1);
    int id_b = engine.addNode("B", &b, 1, 1);
    int id_c = engine.addNode("C", &c, 1, 1);
    int gid = engine.addGroup("Sub", {id_a, id_b, id_c});

    engine.removeNode(id_a);
    REQUIRE(engine.numGroups() == 1);
    REQUIRE(engine.groupIdForNode(id_b) == gid);
    REQUIRE(engine.groupIdForNode(id_c) == gid);
}
```

**Step 2: Build and run tests to verify they fail**

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "removeNode auto-removes|removeNode leaves group"
```

Expected: Two tests pass (the one that removes the last member) but the others fail because the existing `removeNode` doesn't know about groups.

**Step 3: Modify `removeNode` to cascade group cleanup**

In `node_graph/src/node_graph_engine.cpp`, find the existing `removeNode` function and add the group cleanup at the end (after the node is erased from `m_nodes`):

```cpp
void NodeGraphEngine::removeNode(int node_id) {
    // ... existing logic: collect node_pins, remove links, remove probes, erase from m_nodes ...

    // NEW: cascade group cleanup
    std::vector<int> groups_to_remove;
    for (const auto& g : m_groups) {
        if (std::find(g.member_node_ids.begin(), g.member_node_ids.end(), node_id)
            != g.member_node_ids.end()) {
            groups_to_remove.push_back(g.id);
        }
    }
    // Also drop groups that have dropped below 2 members (e.g., from prior cascading)
    for (const auto& g : m_groups) {
        if (g.member_node_ids.size() < 2 &&
            std::find(groups_to_remove.begin(), groups_to_remove.end(), g.id) == groups_to_remove.end()) {
            // Check none of its members are in groups_to_remove (then it's already going)
            bool already = false;
            for (int gid : groups_to_remove) {
                if (gid == g.id) { already = true; break; }
            }
            if (!already) groups_to_remove.push_back(g.id);
        }
    }
    for (int gid : groups_to_remove) {
        removeGroup(gid);
    }
}
```

**Step 4: Build and run tests to verify they pass**

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "removeNode auto-removes|removeNode leaves group"
```

Expected: All 3 tests pass.

**Step 5: Run the full test suite to verify no regressions**

```bash
ctest --test-dir build --output-on-failure
```

Expected: All existing tests still pass.

**Step 6: Commit**

```bash
git add tests/test_group.cpp node_graph/src/node_graph_engine.cpp
git commit -m "feat: cascade group cleanup in removeNode"
```

---

### Task 9: Integration test — signal flow through a group

**Files:**
- Modify: `tests/test_group.cpp`

**Step 1: Add the integration test**

```cpp
#include "signal_generator_engine.h"
#include "amplifier_engine.h"
#include "splitter_engine.h"

TEST_CASE("Group preserves signal flow when collapsed", "[group][integration]") {
    NodeGraphEngine engine;
    SignalGeneratorEngine gen(1, engine);
    AmplifierEngine amp(2, engine);
    SplitterEngine splitter(3, engine);

    // Wire: gen -> amp -> splitter
    int gen_out = gen.outputPinId();
    int amp_in = amp.inputPinId();
    int amp_out = amp.outputPinId();
    int split_in = splitter.inputPinId(0);
    int split_out_0 = splitter.outputPinId(0);
    int split_out_1 = splitter.outputPinId(1);

    engine.addLink(gen_out, amp_in);
    engine.addLink(amp_out, split_in);

    // Group the amp + splitter
    int gid = engine.addGroup("IF Stage", {amp.graphNodeId(), splitter.graphNodeId()});
    REQUIRE(gid >= 0);

    // Set the generator's tone
    gen.addTone(100e6, -20.0);

    // Run the update loop
    gen.update(0.0);
    amp.update(0.0);
    splitter.update(0.0);

    // The splitter's outputs should match what the amplifier sent in
    REQUIRE(splitter.node().outputs[0].tones.size() == 1);
    REQUIRE(splitter.node().outputs[1].tones.size() == 1);
    REQUIRE(splitter.node().outputs[0].tones[0].freq_Hz == Approx(100e6));
    REQUIRE(splitter.node().outputs[1].tones[0].freq_Hz == Approx(100e6));
}

TEST_CASE("Group preserves topological order", "[group][integration]") {
    NodeGraphEngine engine;
    SignalGeneratorEngine gen(1, engine);
    AmplifierEngine amp(2, engine);
    SplitterEngine splitter(3, engine);

    engine.addLink(gen.outputPinId(), amp.inputPinId());
    engine.addLink(amp.outputPinId(), splitter.inputPinId(0));

    auto order_before = engine.topologicalOrder();
    int gid = engine.addGroup("IF", {amp.graphNodeId(), splitter.graphNodeId()});
    (void)gid;
    auto order_after = engine.topologicalOrder();

    REQUIRE(order_before == order_after);
}
```

**Step 2: Build and run tests**

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "Group preserves"
```

Expected: Both tests pass.

**Step 3: Commit**

```bash
git add tests/test_group.cpp
git commit -m "test: integration tests for groups preserving signal flow and topology"
```

**End of Phase 1.** The engine is feature-complete. Phase 2 begins the widget.

---

## Phase 2 — Widget rendering: expanded state

### Task 10: Add widget state for groups

**Files:**
- Modify: `node_graph/include/node_graph_widget.h`
- Modify: `node_graph/src/node_graph_widget.cpp`

**Step 1: Add includes and state to the widget header**

Add to `node_graph_widget.h`:

```cpp
#include "group.h"
#include <unordered_map>

class NodeGraphWidget {
  public:
    // ... existing public API ...
  private:
    NodeGraphEngine &m_engine;
    ImNodesEditorContext *m_context;
    IconRegistry m_icons;

    // ... existing interaction state ...

    // Subcircuit state
    std::unordered_map<int, int> m_synth_pin_to_real_pin;  // rebuilt every frame
    std::unordered_map<int, int> m_phantom_id_for_node;    // rebuilt every frame if needed
    bool m_use_phantom_nodes = false;  // spike report confirmed phantom workaround is NOT needed; stays false
    int m_context_menu_group_id = -1;  // the group id when group_context_menu is open

    // Rubber-band state
    bool m_rubber_band_active = false;
    ImVec2 m_rubber_band_start = ImVec2(0, 0);
    ImVec2 m_rubber_band_end = ImVec2(0, 0);
    std::vector<int> m_rubber_band_members;  // populated on release
    bool m_show_create_popup = false;

    // Internal rendering helpers
    void rebuildSynthMaps();           // call at the start of draw()
    void drawGroupBackgrounds();
    void drawPhantomNodes();           // empty body until Task 14 if phantoms are needed
    void drawGroupCollapsedBlocks();
    void drawGroupTitleBar(Group& g, const ImVec2& top_left);

    // Interaction handlers
    void handleRubberBand();
    void handleGroupSelection();
    size_t findNodeIndex(int node_id) const;
};
```

**Step 2: Add the empty implementations in the .cpp**

Add to `node_graph_widget.cpp`:

```cpp
void NodeGraphWidget::rebuildSynthMaps() {
    m_synth_pin_to_real_pin.clear();
    m_phantom_id_for_node.clear();
    int phantom_counter = 200000;
    for (const auto& g : m_engine.groups()) {
        for (const auto& bp : g.boundary_pins) {
            m_synth_pin_to_real_pin[bp.id] = bp.internal_pin_id;
        }
        if (m_use_phantom_nodes) {
            for (int nid : g.member_node_ids) {
                m_phantom_id_for_node[nid] = phantom_counter++;
            }
        }
    }
}

void NodeGraphWidget::drawGroupBackgrounds() {
    // Real implementation is added in Task 11.
}

void NodeGraphWidget::drawPhantomNodes() {
    // Real implementation is added in Task 14 only if the spike report
    // says the phantom workaround is needed.
}

void NodeGraphWidget::drawGroupCollapsedBlocks() {
    // Real implementation is added in Task 12.
}

void NodeGraphWidget::drawGroupTitleBar(Group& g, const ImVec2& top_left) {
    (void)g; (void)top_left;
    // Real implementation is added in Task 11.
}
```

**Step 3: Wire `rebuildSynthMaps` into the `draw()` method**

In `NodeGraphWidget::draw`, add `rebuildSynthMaps();` at the start of the `if (ImGui::Begin(title, p_open))` block, and call `ImNodes::ClearNodeSelection();` to reset phantom selection state.

```cpp
void NodeGraphWidget::draw(const char *title, bool *p_open) {
    ImNodes::EditorContextSet(m_context);

    if (ImGui::Begin(title, p_open)) {
        ImNodes::ClearNodeSelection();
        rebuildSynthMaps();

        drawGroupBackgrounds();
        drawPhantomNodes();

        ImNodes::BeginNodeEditor();

        drawNodes();
        drawGroupCollapsedBlocks();
        drawLinks();

        // ... rest of existing draw code ...
```

**Step 4: Verify build**

```bash
cmake --build build
```

Expected: Build succeeds. The empty bodies compile cleanly; they are filled in by Tasks 11, 12, and 14.

**Step 5: Commit**

```bash
git add node_graph/include/node_graph_widget.h node_graph/src/node_graph_widget.cpp
git commit -m "feat: add widget state for subcircuit groups"
```

---

### Task 11: Render expanded group backgrounds and title bars

**Files:**
- Modify: `node_graph/src/node_graph_widget.cpp`

**Step 1: Implement `drawGroupBackgrounds`**

Replace the empty body with:

```cpp
void NodeGraphWidget::drawGroupBackgrounds() {
    // Phantoms are not used in v1 (spike confirmed). Group background is drawn here;
    // internals (when expanded) are drawn by drawNodes() afterward, on top of this background.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (const auto& g : m_engine.groups()) {
        if (g.collapsed) continue;
        if (g.member_node_ids.empty()) continue;

        // Compute bounding box in grid space
        ImVec2 top_left(std::numeric_limits<float>::max(),
                         std::numeric_limits<float>::max());
        ImVec2 bottom_right(std::numeric_limits<float>::lowest(),
                            std::numeric_limits<float>::lowest());
        const float NODE_W = 120.0f, NODE_H = 80.0f;  // approximate; refined in spike
        for (int nid : g.member_node_ids) {
            ImVec2 pos;
            ImVec2 pos = ImNodes::GetNodeGridSpacePos(nid);
            top_left.x = std::min(top_left.x, pos.x);
            top_left.y = std::min(top_left.y, pos.y);
            bottom_right.x = std::max(bottom_right.x, pos.x + NODE_W);
            bottom_right.y = std::max(bottom_right.y, pos.y + NODE_H);
        }
        if (top_left.x > bottom_right.x) continue;  // no valid members

        // Convert grid space to screen space
        ImVec2 pad(16, 16);
        ImVec2 tl_screen = top_left - pad + ImNodes::EditorContextGetPanning();
        ImVec2 br_screen = bottom_right + pad + ImNodes::EditorContextGetPanning();

        dl->AddRectFilled(tl_screen, br_screen, IM_COL32(80, 80, 120, 24));
        dl->AddRect(tl_screen, br_screen, IM_COL32(120, 120, 180, 96));

        // Title bar at the top of the rectangle
        drawGroupTitleBar(const_cast<Group&>(g), top_left);
    }
}
```

**Step 2: Implement `drawGroupTitleBar`**

```cpp
void NodeGraphWidget::drawGroupTitleBar(Group& g, const ImVec2& top_left_grid) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 tl_screen = top_left_grid - ImVec2(16, 16) + ImNodes::EditorContextGetPanning();
    ImVec2 br_screen = top_left_grid + ImVec2(180, -16) + ImNodes::EditorContextGetPanning();

    // Background of the title bar
    dl->AddRectFilled(tl_screen, br_screen, IM_COL32(60, 60, 100, 200));

    // Group name
    dl->AddText(tl_screen + ImVec2(8, 4), IM_COL32(255, 255, 255, 255), g.name.c_str());

    // Collapse button (▼)
    ImVec2 btn_min = br_screen - ImVec2(24, 0);
    ImVec2 btn_max = br_screen;
    dl->AddRectFilled(btn_min, btn_max, IM_COL32(100, 100, 140, 255));
    dl->AddText(btn_min + ImVec2(6, 2), IM_COL32(255, 255, 255, 255), "\xE2\x96\xBC");  // ▼

    // Hit-test the button
    if (ImGui::IsMouseClicked(0)) {
        ImVec2 mouse = ImGui::GetMousePos();
        if (mouse.x >= btn_min.x && mouse.x <= btn_max.x &&
            mouse.y >= btn_min.y && mouse.y <= btn_max.y) {
            m_engine.setGroupCollapsed(g.id, true);
        }
    }
}
```

**Step 3: Verify build**

```bash
cmake --build build
```

Expected: Build succeeds. The expanded-state backgrounds and title bars will render in the running app.

**Step 4: Manual smoke test**

```bash
cmake --build build && ./build/bin/main.exe   # or main on Linux
```

Add a few components via right-click. Currently there's no UI to create a group; the smoke test is to verify no rendering glitches occur. (Group creation comes in Phase 4.)

**Step 5: Commit**

```bash
git add node_graph/src/node_graph_widget.cpp
git commit -m "feat: render expanded group backgrounds and title bars"
```

**End of Phase 2.** Phase 3 implements the collapsed state.

---

## Phase 3 — Widget rendering: collapsed state

### Task 12: Render collapsed group as a single imnodes node

**Files:**
- Modify: `node_graph/src/node_graph_widget.cpp`

**Step 1: Implement `drawGroupCollapsedBlocks`**

```cpp
void NodeGraphWidget::drawGroupCollapsedBlocks() {
    for (const auto& g : m_engine.groups()) {
        if (!g.collapsed) continue;

        // Compute centroid in grid space
        if (g.member_node_ids.empty()) continue;
        ImVec2 sum(0, 0);
        int count = 0;
        for (int nid : g.member_node_ids) {
            ImVec2 pos;
            ImVec2 pos = ImNodes::GetNodeGridSpacePos(nid);
            {
                sum.x += pos.x;
                sum.y += pos.y;
                ++count;
            }
        }
        if (count == 0) continue;
        ImVec2 centroid(sum.x / count, sum.y / count);
        ImVec2 node_pos = centroid - ImVec2(60, 40);  // center the 120x80 block

        // Render the block as an imnodes node
        ImNodes::BeginNode(g.id);
        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted(g.name.c_str());

        // Expand button (▶) in the title bar
        ImGui::SameLine();
        if (ImGui::SmallButton("\xE2\x96\xB6")) {  // ▶
            m_engine.setGroupCollapsed(g.id, false);
        }
        ImNodes::EndNodeTitleBar();

        // Body
        ImGui::Dummy(ImVec2(120, 60));

        // Boundary pins
        int input_idx = 0, output_idx = 0;
        for (const auto& bp : g.boundary_pins) {
            int slot = m_engine.probeSlotForPin(bp.internal_pin_id);
            if (slot >= 0) {
                static const ImU32 probe_colors[4] = {
                    IM_COL32(22, 199, 154, 255),
                    IM_COL32(230, 150, 40, 255),
                    IM_COL32(120, 50, 170, 255),
                    IM_COL32(60, 140, 220, 255),
                };
                ImNodes::PushColorStyle(ImNodesCol_Pin, probe_colors[slot]);
                ImNodes::PushColorStyle(ImNodesCol_PinHovered, probe_colors[slot]);
            }
            if (bp.is_output) {
                ImNodes::BeginOutputAttribute(bp.id);
                ImGui::TextUnformatted(bp.label.c_str());
                ImNodes::EndOutputAttribute();
                (void)output_idx++;
            } else {
                ImNodes::BeginInputAttribute(bp.id);
                ImGui::TextUnformatted(bp.label.c_str());
                ImNodes::EndInputAttribute();
                (void)input_idx++;
            }
            if (slot >= 0) {
                ImNodes::PopColorStyle();
                ImNodes::PopColorStyle();
            }
        }

        ImNodes::EndNode();
        (void)node_pos;  // imnodes uses GetNodeGridSpacePos for layout; explicit pos is read-only
    }
}
```

Note: We rely on imnodes to position the node based on its previously known position (centroid) — no explicit `SetNodeGridPos` call. The block tracks its own position internally; on first render the centroid is set, on subsequent renders imnodes keeps it.

**Step 2: Verify build**

```bash
cmake --build build
```

Expected: Build succeeds.

**Step 3: Commit**

```bash
git add node_graph/src/node_graph_widget.cpp
git commit -m "feat: render collapsed group as a single imnodes node"
```

---

### Task 13: Skip rendering of internal nodes and internal links when their group is collapsed

**Files:**
- Modify: `node_graph/src/node_graph_widget.cpp`

**Step 1: Modify `drawNodes` to skip internal nodes when their group is collapsed**

Find the `drawNodes` method. Build a set of "hidden node ids" (members of collapsed groups) at the start, then skip them in the loop:

```cpp
void NodeGraphWidget::drawNodes() {
    std::unordered_set<int> hidden_nodes;
    for (const auto& g : m_engine.groups()) {
        if (g.collapsed) {
            for (int nid : g.member_node_ids) hidden_nodes.insert(nid);
        }
    }

    for (const auto &node : m_engine.nodes()) {
        if (hidden_nodes.count(node.node_id)) continue;

        ImNodes::BeginNode(node.node_id);
        // ... existing body unchanged ...
        ImNodes::EndNode();
    }
}
```

**Step 2: Modify `drawLinks` to skip internal links when their group is collapsed**

```cpp
void NodeGraphWidget::drawLinks() {
    std::unordered_set<int> collapsed_groups;
    std::unordered_set<int> hidden_nodes;
    for (const auto& g : m_engine.groups()) {
        if (g.collapsed) {
            collapsed_groups.insert(g.id);
            for (int nid : g.member_node_ids) hidden_nodes.insert(nid);
        }
    }

    auto pin_owner_node = [this](int pin_id) -> int {
        for (const auto& n : m_engine.nodes()) {
            for (int p : n.input_pin_ids) if (p == pin_id) return n.node_id;
            for (int p : n.output_pin_ids) if (p == pin_id) return n.node_id;
        }
        return -1;
    };

    for (const auto &link : m_engine.links()) {
        int start_node = pin_owner_node(link.start_pin_id);
        int end_node = pin_owner_node(link.end_pin_id);
        if (start_node < 0 || end_node < 0) continue;

        bool start_hidden = hidden_nodes.count(start_node) > 0;
        bool end_hidden = hidden_nodes.count(end_node) > 0;
        if (start_hidden && end_hidden) continue;  // internal link in collapsed group

        // Cross-boundary link: draw it as-is (imnodes positions the in-group endpoint
        // from the internal node, which we've skipped — see Phantom Node note in spec section 9)
        ImNodes::Link(link.link_id, link.start_pin_id, link.end_pin_id);
    }
}
```

**Step 3: Verify build and manual smoke test**

```bash
cmake --build build && ./build/bin/main.exe
```

When a group is collapsed, the internal nodes should disappear and the group block should appear in their place. Cross-boundary links should be visible (or invisible, depending on whether the spike's phantom workaround is needed — see Task 14).

**Step 4: Commit**

```bash
git add node_graph/src/node_graph_widget.cpp
git commit -m "feat: skip rendering internal nodes and internal links when group is collapsed"
```

---

### Task 14: Phantom node workaround (conditional on spike result)

**Files:**
- Modify: `node_graph/src/node_graph_widget.cpp`

**Step 1: If the spike report says phantoms are needed, set the flag and implement `drawPhantomNodes`**

In the widget's constructor (or initialization):

```cpp
// node_graph_widget.cpp
NodeGraphWidget::NodeGraphWidget(NodeGraphEngine &engine) : m_engine(engine), m_context(nullptr) {
    m_context = ImNodes::EditorContextCreate();
    ImNodes::EditorContextSet(m_context);
    // Set this based on the spike report's conclusion:
    m_use_phantom_nodes = false;  // spike report confirms phantom workaround is NOT needed
}
```

Replace the empty `drawPhantomNodes`:

```cpp
void NodeGraphWidget::drawPhantomNodes() {
    // The spike report confirmed the phantom workaround is not needed for the
    // pinned imnodes revision: imnodes' position pool is stable, so the in-group
    // pin position used by cross-boundary link rendering remains accurate even
    // when the owning internal node is not drawn this frame (its position is
    // preserved from the previous frame's EndNode() call). m_use_phantom_nodes
    // stays false; this function is intentionally a no-op.
    (void)0;

    for (const auto& g : m_engine.groups()) {
        if (!g.collapsed) continue;
        for (int nid : g.member_node_ids) {
            auto it = m_phantom_id_for_node.find(nid);
            if (it == m_phantom_id_for_node.end()) continue;
            int phantom_id = it->second;

            ImNodes::BeginNode(phantom_id);
            ImNodes::BeginNodeTitleBar();
            ImGui::Dummy(ImVec2(0, 0));
            ImNodes::EndNodeTitleBar();

            size_t idx = findNodeIndex(nid);
            if (idx >= m_engine.nodes().size()) continue;
            const auto& node = m_engine.nodes()[idx];
            for (int pin : node.input_pin_ids) {
                ImNodes::BeginInputAttribute(pin);
                ImGui::Dummy(ImVec2(0, 0));
                ImNodes::EndInputAttribute();
            }
            for (int pin : node.output_pin_ids) {
                ImNodes::BeginOutputAttribute(pin);
                ImGui::Dummy(ImVec2(0, 0));
                ImNodes::EndOutputAttribute();
            }
            ImNodes::EndNode();
        }
    }
}
```

The `findNodeIndex` helper is declared in Task 10. Implement it:

```cpp
size_t NodeGraphWidget::findNodeIndex(int node_id) const {
    const auto& nodes = m_engine.nodes();
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].node_id == node_id) return i;
    }
    return SIZE_MAX;  // not found; caller must check
}
```

**Step 2: Verify no further change is needed**

`drawPhantomNodes` is a no-op; the spike confirmed imnodes handles the cross-boundary link rendering case correctly. Task 14 is complete at this point. No calls to `drawPhantomNodes` need to be made from `draw()` either; the function is dead code, kept only as a hook for future imnodes revisions that might require the workaround.

**Step 3: Verify build**

```bash
cmake --build build
```

Expected: Build succeeds. `drawPhantomNodes` is empty; nothing to smoke-test in this task.

**Step 4: No commit needed for this task**

The previous tasks in Phase 3 (Tasks 10-13) already added the `m_use_phantom_nodes` flag, `m_phantom_id_for_node` map, and the `drawPhantomNodes` declaration. The spike report confirmed none of these need to be set or used; the flag stays `false` (declared in Task 10's initializer), the map stays empty, and the function stays empty. There is no new code or new commit in Task 14 — it is a verification step.

**End of Phase 3.** Groups are visually collapsible, but cannot yet be created through the UI. Phase 4 implements rubber-band selection.

---

## Phase 4 — Rubber-band + group creation

### Task 15: Detect Shift+drag on empty editor space

**Files:**
- Modify: `node_graph/src/node_graph_widget.cpp`

(The rubber-band state members — `m_rubber_band_active`, `m_rubber_band_start`, `m_rubber_band_end`, `m_rubber_band_members`, `m_show_create_popup`, plus the `handleRubberBand()` declaration — were added in Task 10.)

**Step 1: Wire `handleRubberBand()` into the draw loop**

In `NodeGraphWidget::draw`, after `ImNodes::EndNodeEditor()` and before `showPinTooltips()`, add:

```cpp
handleRubberBand();
```

**Step 2: Implement `handleRubberBand()` in the .cpp**

```cpp
void NodeGraphWidget::handleRubberBand() {
    bool editor_hovered = ImNodes::IsEditorHovered();
    bool shift = ImGui::IsKeyDown(ImGuiKey_LeftShift);
    bool left_down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    bool left_released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);

    // Start rubber-band on Shift + left-click down on empty space
    if (left_down && shift && editor_hovered && !m_rubber_band_active) {
        m_rubber_band_active = true;
        m_rubber_band_start = ImGui::GetMousePos();
        m_rubber_band_end = m_rubber_band_start;
    }

    // Update end position while dragging
    if (m_rubber_band_active && left_down) {
        m_rubber_band_end = ImGui::GetMousePos();
        // Draw the rectangle
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 a(std::min(m_rubber_band_start.x, m_rubber_band_end.x),
                 std::min(m_rubber_band_start.y, m_rubber_band_end.y));
        ImVec2 b(std::max(m_rubber_band_start.x, m_rubber_band_end.x),
                 std::max(m_rubber_band_start.y, m_rubber_band_end.y));
        dl->AddRectFilled(a, b, IM_COL32(100, 200, 255, 32));
        dl->AddRect(a, b, IM_COL32(100, 200, 255, 200));
    }

    // On release, collect enclosed nodes
    if (m_rubber_band_active && left_released) {
        m_rubber_band_active = false;
        m_rubber_band_members.clear();

        ImVec2 screen_a(
            std::min(m_rubber_band_start.x, m_rubber_band_end.x),
            std::min(m_rubber_band_start.y, m_rubber_band_start.y));
        ImVec2 screen_b(
            std::max(m_rubber_band_start.x, m_rubber_band_end.x),
            std::max(m_rubber_band_start.y, m_rubber_band_end.y));

        for (const auto& node : m_engine.nodes()) {
            if (m_engine.groupIdForNode(node.node_id) != -1) continue;  // skip grouped
            ImVec2 pos;
            ImVec2 pos = ImNodes::GetNodeScreenSpacePos(node.node_id);
            ImVec2 center = pos + ImVec2(60, 40);  // approximate node center
            if (center.x >= screen_a.x && center.x <= screen_b.x &&
                center.y >= screen_a.y && center.y <= screen_b.y) {
                m_rubber_band_members.push_back(node.node_id);
            }
        }

        if (m_rubber_band_members.size() >= 2) {
            m_show_create_popup = true;
            ImGui::OpenPopup("CreateSubcircuit");
        }
    }
}
```

**Step 3: Verify build**

```bash
cmake --build build
```

Expected: Build succeeds.

**Step 4: Commit**

```bash
git add node_graph/include/node_graph_widget.h node_graph/src/node_graph_widget.cpp
git commit -m "feat: detect Shift+drag rubber-band on empty editor space"
```

---

### Task 16: "Create Subcircuit" popup

**Files:**
- Modify: `node_graph/src/node_graph_widget.cpp`

**Step 1: Add the popup after the rubber-band handler**

```cpp
if (m_show_create_popup) {
    if (ImGui::BeginPopupModal("CreateSubcircuit", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        static char name_buf[128];
        static int last_member_count = -1;
        if (last_member_count != static_cast<int>(m_rubber_band_members.size())) {
            std::snprintf(name_buf, sizeof(name_buf), "Subcircuit %d",
                          m_engine.numGroups() + 1);
            last_member_count = static_cast<int>(m_rubber_band_members.size());
        }

        ImGui::Text("Members (%zu):", m_rubber_band_members.size());
        ImGui::Indent();
        for (int nid : m_rubber_band_members) {
            for (const auto& n : m_engine.nodes()) {
                if (n.node_id == nid) {
                    ImGui::TextUnformatted(n.label.c_str());
                    break;
                }
            }
        }
        ImGui::Unindent();

        ImGui::InputText("Name", name_buf, sizeof(name_buf));

        if (ImGui::Button("Create")) {
            m_engine.addGroup(name_buf, m_rubber_band_members);
            m_show_create_popup = false;
            last_member_count = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            m_show_create_popup = false;
            last_member_count = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
```

**Step 2: Verify build and manual smoke test**

```bash
cmake --build build && ./build/bin/main.exe
```

Add 3+ components. Shift+drag a rectangle around 2-3 of them. The "Create Subcircuit" popup should open with the member list and a default name. Click "Create" — the group is created in expanded state. The background rectangle and title bar from Task 11 should appear.

**Step 3: Commit**

```bash
git add node_graph/src/node_graph_widget.cpp
git commit -m "feat: Create Subcircuit popup with member list and name field"
```

**End of Phase 4.** Users can now create groups. Phase 5 adds the inspector and right-click context menu.

---

## Phase 5 — Inspector + right-click + selection

### Task 17: Group click selects group

**Files:**
- Modify: `node_graph/src/node_graph_widget.cpp`

**Step 1: Add group selection handling in `handleProbeClick` (or a new handler)**

After the existing `handleProbeClick()` call, add:

```cpp
handleGroupSelection();
```

Implement:

```cpp
void NodeGraphWidget::handleGroupSelection() {
    int hovered_node = -1;
    if (ImGui::IsMouseClicked(0) && ImNodes::IsNodeHovered(&hovered_node)) {
        if (hovered_node >= 50000 && hovered_node < 100000) {
            // It's a group
            m_engine.setSelectedGroupId(hovered_node);
            ImNodes::ClearNodeSelection();
        } else {
            // It's a regular node; deselect any group
            m_engine.setSelectedGroupId(-1);
        }
    }
    if (ImGui::IsMouseClicked(0)) {
        bool editor_hovered = ImNodes::IsEditorHovered();
        int hovered_node = -1;
        bool node_hovered = ImNodes::IsNodeHovered(&hovered_node);
        bool link_hovered = ImNodes::IsLinkHovered();
        if (editor_hovered && !node_hovered && !link_hovered) {
            m_engine.setSelectedGroupId(-1);
        }
    }
}
```

**Step 2: Verify build**

```bash
cmake --build build
```

Expected: Build succeeds.

**Step 4: Commit**

```bash
git add node_graph/include/node_graph_widget.h node_graph/src/node_graph_widget.cpp
git commit -m "feat: select group on group block click"
```

---

### Task 18: Inspector group panel

**Files:**
- Modify: `app/include/inspector_panel.h`
- Modify: `app/src/inspector_panel.cpp`

**Step 1: Add `drawGroupPanel` declaration to the header**

In `app/include/inspector_panel.h`, find the class declaration and add to `private:`:

```cpp
void drawGroupPanel(int group_id);
```

**Step 2: Add the `selectedGroupId` branch in `InspectorPanel::draw`**

In `app/src/inspector_panel.cpp`, find `InspectorPanel::draw` and add the branch at the top of the body (after the existing early-outs):

```cpp
void InspectorPanel::draw(const char *title, bool *p_open) {
    // ... existing setup code ...

    int gid = m_graph_engine.selectedGroupId();
    if (gid >= 0) {
        drawGroupPanel(gid);
        return;
    }

    // ... existing per-component property drawer ...
}
```

**Step 3: Implement `drawGroupPanel`**

Add at the end of `inspector_panel.cpp`:

```cpp
void InspectorPanel::drawGroupPanel(int group_id) {
    const Group* g = m_graph_engine.groupById(group_id);
    if (!g) {
        m_graph_engine.setSelectedGroupId(-1);
        return;
    }

    static char name_buf[128];
    static int last_gid = -1;
    if (last_gid != group_id) {
        std::strncpy(name_buf, g->name.c_str(), sizeof(name_buf) - 1);
        name_buf[sizeof(name_buf) - 1] = '\0';
        last_gid = group_id;
    }

    ImGui::InputText("Name", name_buf, sizeof(name_buf));
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        m_graph_engine.renameGroup(group_id, name_buf);
    }

    ImGui::Separator();
    ImGui::Text("Members (%zu):", g->member_node_ids.size());
    ImGui::Indent();
    for (int nid : g->member_node_ids) {
        for (const auto& n : m_graph_engine.nodes()) {
            if (n.node_id == nid) {
                ImGui::TextUnformatted(n.label.c_str());
                break;
            }
        }
    }
    ImGui::Unindent();

    if (!g->boundary_pins.empty()) {
        ImGui::Separator();
        ImGui::Text("Boundary pins:");
        ImGui::Indent();
        for (const auto& bp : g->boundary_pins) {
            const char* dir = bp.is_output ? "OUT" : "IN";
            ImGui::Text("%s -> \"%s\"", dir, bp.label.c_str());
        }
        ImGui::Unindent();
    }

    ImGui::Separator();
    if (ImGui::Button("Ungroup")) {
        m_graph_engine.removeGroup(group_id);
        last_gid = -1;
    }
}
```

**Step 4: Verify build and manual smoke test**

```bash
cmake --build build && ./build/bin/main.exe
```

Create a group, click on the collapsed block, open the Properties window. The group panel should show the name field, member list, boundary pins, and Ungroup button. Editing the name and pressing Enter should rename the group. Clicking Ungroup should remove the group.

**Step 5: Commit**

```bash
git add app/include/inspector_panel.h app/src/inspector_panel.cpp
git commit -m "feat: inspector group panel"
```

---

### Task 19: Right-click context menu on groups

**Files:**
- Modify: `node_graph/src/node_graph_widget.cpp`

**Step 1: Add a group context menu in `handleContextMenu`**

After the existing `node_context_menu` and `canvas_context_menu`, add:

```cpp
if (ImGui::BeginPopup("group_context_menu")) {
    const Group* g = m_engine.groupById(m_context_menu_group_id);
    if (g) {
        if (ImGui::MenuItem(g->collapsed ? "Expand" : "Collapse")) {
            m_engine.setGroupCollapsed(m_context_menu_group_id, !g->collapsed);
        }
        if (ImGui::MenuItem("Rename")) {
            m_pending_rename_group_id = m_context_menu_group_id;
            std::strncpy(m_rename_buffer, g->name.c_str(), sizeof(m_rename_buffer) - 1);
        }
        if (ImGui::MenuItem("Ungroup")) {
            m_engine.removeGroup(m_context_menu_group_id);
        }
    }
    ImGui::EndPopup();
}
```

**Step 2: Open the group context menu on right-click**

In `handleContextMenu`, before the existing popup logic, add:

```cpp
if (right_click && editor_hovered) {
    int hovered_node = -1;
    bool node_hovered = ImNodes::IsNodeHovered(&hovered_node);
    if (node_hovered && hovered_node >= 50000 && hovered_node < 100000) {
        ImGui::OpenPopup("group_context_menu");
        m_context_menu_group_id = hovered_node;
        return;  // don't open the node_context_menu for groups
    }
    // ... existing node_hovered / canvas logic ...
}
```

**Step 3: Verify build and manual smoke test**

```bash
cmake --build build && ./build/bin/main.exe
```

Right-click on a collapsed group block. The context menu should show "Expand/Collapse", "Rename", "Ungroup". Right-click on an expanded group's title bar (drawn via DrawList) — this requires the same bounding-box hit-test as the `▼` button; add it to `handleGroupInteractions` if not already covered.

**Step 4: Commit**

```bash
git add node_graph/src/node_graph_widget.cpp
git commit -m "feat: right-click context menu on groups"
```

**End of Phase 5.** Users can create, select, inspect, rename, expand/collapse, and ungroup subcircuits. Phase 6 integrates probing.

---

## Phase 6 — Probe translation

### Task 20: Probe translation for boundary pins and group-block clicks

**Files:**
- Modify: `node_graph/src/node_graph_widget.cpp`

**Step 1: Modify `handleProbeClick` to translate synthesized pin ids**

Find the existing `handleProbeClick` method. After detecting a click and determining `target_pin`, add the translation step:

```cpp
// In handleProbeClick, where target_pin is determined:
if (target_pin >= 100000) {
    // Synthesized boundary pin id; translate to real internal pin id
    auto it = m_synth_pin_to_real_pin.find(target_pin);
    if (it != m_synth_pin_to_real_pin.end()) {
        target_pin = it->second;
    } else {
        target_pin = -1;  // stale pin id
    }
}
if (target_pin >= 0) {
    if (ctrl)
        m_engine.addProbePin(target_pin);
    else if (shift)
        m_engine.removeProbePin(target_pin);
}
```

**Step 2: Extend `handleProbeClick` to handle group-block clicks**

Modify the existing logic that probes a node's first output pin. Add a check: if the hovered node is a group id (50000+), probe the first boundary output pin's internal pin id.

```cpp
// In handleProbeClick, where target_pin is determined from m_clicked_node:
if (target_pin < 0 && m_clicked_node >= 50000 && m_clicked_node < 100000) {
    // Group-block click; probe the first output boundary pin
    const Group* g = m_engine.groupById(m_clicked_node);
    if (g) {
        for (const auto& bp : g->boundary_pins) {
            if (bp.is_output) {
                target_pin = bp.internal_pin_id;
                break;
            }
        }
    }
}
```

**Step 3: Verify build and manual smoke test**

```bash
cmake --build build && ./build/bin/main.exe
```

Create a group, collapse it, Ctrl+click the block — a probe should be added (visible in the spectrum analyzer). Ctrl+click a boundary pin on the block — same effect.

**Step 4: Commit**

```bash
git add node_graph/src/node_graph_widget.cpp
git commit -m "feat: probe translation for boundary pins and group-block clicks"
```

---

### Task 21: Boundary pin probe color (already implemented in Task 12)

Task 12 already includes the boundary pin probe color logic in `drawGroupCollapsedBlocks`. Verify the test:

- Create a chain: Generator → Amplifier
- Group the Amplifier (single-member groups are forbidden — instead, add another component and group {Amplifier, Splitter})
- Probe the Amplifier's output pin while the group is expanded
- Collapse the group
- The boundary pin (representing the Amplifier's output) should now show the probe color

If this works, Task 21 is a verification step, not new code. Mark it complete and move to Phase 7.

---

## Phase 7 — Link creation / destruction translation

### Task 22: Link creation through boundary pin

**Files:**
- Modify: `node_graph/src/node_graph_widget.cpp`

**Step 1: Modify `handleLinkCreation` to translate synthesized pin ids**

```cpp
void NodeGraphWidget::handleLinkCreation() {
    int start_pin, end_pin;
    m_link_created = ImNodes::IsLinkCreated(&start_pin, &end_pin);
    if (m_link_created) {
        // Translate synthesized pin ids to real internal pin ids
        if (start_pin >= 100000) {
            auto it = m_synth_pin_to_real_pin.find(start_pin);
            if (it != m_synth_pin_to_real_pin.end()) start_pin = it->second;
        }
        if (end_pin >= 100000) {
            auto it = m_synth_pin_to_real_pin.find(end_pin);
            if (it != m_synth_pin_to_real_pin.end()) end_pin = it->second;
        }
        if (start_pin < 100000 && end_pin < 100000) {
            m_engine.addLink(start_pin, end_pin);

            // Resolve pin -> owning node id
            auto pin_owner_node = [this](int pin_id) {
                for (const auto& n : m_engine.nodes()) {
                    for (int p : n.input_pin_ids) if (p == pin_id) return n.node_id;
                    for (int p : n.output_pin_ids) if (p == pin_id) return n.node_id;
                }
                return -1;
            };
            int start_node = pin_owner_node(start_pin);
            int end_node = pin_owner_node(end_pin);

            // Rebuild boundary pins for any affected group
            for (const auto& g : m_engine.groups()) {
                for (int nid : g.member_node_ids) {
                    if (nid == start_node || nid == end_node) {
                        m_engine.rebuildGroupBoundaryPins(g.id);
                        break;
                    }
                }
            }
        }
    }
}
```

**Step 2: Verify build and manual smoke test**

```bash
cmake --build build && ./build/bin/main.exe
```

Create a group, collapse it, drag a new link from a boundary pin to an external node. The link should be created in the engine with the real internal pin id. Expanding the group should show the link originating from the internal pin.

**Step 3: Commit**

```bash
git add node_graph/src/node_graph_widget.cpp
git commit -m "feat: link creation through boundary pins with translation"
```

---

### Task 23: Link destruction through boundary pin

**Files:**
- Modify: `node_graph/src/node_graph_widget.cpp`

**Step 1: Modify `handleLinkDeletion` to translate synthesized pin ids**

```cpp
void NodeGraphWidget::handleLinkDeletion() {
    int link_id;
    if (ImNodes::IsLinkDestroyed(&link_id)) {
        // imnodes reports the link id; we need to find the corresponding real link in the engine
        // and remove it
        // For now, since link ids are globally unique and the engine stores them, just remove by id
        m_engine.removeLink(link_id);
        // Rebuild boundary pins for any affected group
        // (rebuild for all groups is O(N) per deletion; acceptable for small circuit sizes)
        for (const auto& g : m_engine.groups()) {
            m_engine.rebuildGroupBoundaryPins(g.id);
        }
    }
}
```

Note: The engine's `removeLink` already takes a link id and works correctly; the synthesized-to-real translation is automatic because the link id is the same in both spaces (the engine never sees synthesized ids).

**Step 2: Verify build and manual smoke test**

```bash
cmake --build build && ./build/bin/main.exe
```

Create a cross-boundary link, then delete it (imnodes' default link deletion gesture). The link should be removed from the engine, and the boundary pin synthesis should update accordingly.

**Step 3: Commit**

```bash
git add node_graph/src/node_graph_widget.cpp
git commit -m "feat: link destruction with boundary pin rebuild"
```

**End of Phase 7.** All features are wired up. Phase 8 is polish and docs.

---

## Phase 8 — Polish, benchmarks, docs

### Task 24: Add benchmarks

**Files:**
- Modify: `tests/test_bench_dsp.cpp` (or create `tests/test_bench_groups.cpp`)

**Step 1: Add a benchmark for `rebuildGroupBoundaryPins`**

```cpp
#include <catch2/benchmark/benchmark_all.hpp>
#include "node_graph_engine.h"

TEST_CASE("Benchmark: rebuildGroupBoundaryPins with 100 cross-boundary links", "[benchmark][group]") {
    NodeGraphEngine engine;
    std::vector<int> nodes;
    for (int i = 0; i < 50; ++i) {
        SignalNode n;
        nodes.push_back(engine.addNode("N" + std::to_string(i), &n, 1, 1));
    }
    int gid = engine.addGroup("Big", nodes);
    for (int i = 1; i < 50; ++i) {
        SignalNode ext;
        int ext_id = engine.addNode("E" + std::to_string(i), &ext, 1, 1);
        engine.addLink(engine.nodes()[i].output_pin_ids[0], ext_id);
    }
    BENCHMARK("rebuildGroupBoundaryPins") {
        engine.rebuildGroupBoundaryPins(gid);
    };
}
```

**Step 2: Add a benchmark for group add/remove cycle**

```cpp
TEST_CASE("Benchmark: group add/remove cycle", "[benchmark][group]") {
    NodeGraphEngine engine;
    std::vector<int> nodes;
    for (int i = 0; i < 10; ++i) {
        SignalNode n;
        nodes.push_back(engine.addNode("N" + std::to_string(i), &n, 1, 1));
    }
    BENCHMARK("addGroup+removeGroup") {
        int gid = engine.addGroup("Cycle", nodes);
        engine.removeGroup(gid);
    };
}
```

**Step 3: Add to TEST_SOURCES if creating a new file**

If you created a new `tests/test_bench_groups.cpp`, add it to `tests/CMakeLists.txt` next to the existing benchmark file.

**Step 4: Run benchmarks**

```bash
cmake --build build
./build/bin/tests "[benchmark][group]"
```

Expected: Benchmarks run without crashing. Note the times; if `rebuildGroupBoundaryPins` is much over 1 µs per call for the 100-link case, the algorithm needs a closer look.

**Step 5: Commit**

```bash
git add tests/test_bench_dsp.cpp  # or new file
git commit -m "bench: add benchmarks for group operations"
```

---

### Task 25: Update ARCHITECTURE.md

**Files:**
- Modify: `ARCHITECTURE.md`

**Step 1: Add a "Subcircuit Groups" section after the existing "Node Graph" section**

Find the section that starts with `## 6. Node Graph` and add a new `## 6.1. Subcircuit Groups` subsection:

```markdown
### 6.1. Subcircuit Groups

The node editor supports user-defined **subcircuit groups**: a named set of `GraphNode`s that can be expanded to show internals or collapsed to display as a single block with synthesized input/output pins. The DSP graph is unchanged — groups are a visual layer only.

**Data model** lives in `common/include/group.h`:

- `Group` — `{id, name, member_node_ids, boundary_pins, collapsed}`. `member_node_ids` is frozen after creation (snapshot editing model).
- `GroupBoundaryPin` — synthesized pin on a collapsed group block, representing one cross-boundary link. `{id (>= 100000), internal_node_id, internal_pin_id, is_output, label}`.

**Engine API** in `NodeGraphEngine`:

- `addGroup(name, member_node_ids)` / `removeGroup(group_id)` / `renameGroup(...)` — snapshot lifecycle.
- `setGroupCollapsed(...)` / `isGroupCollapsed(...)` — view state.
- `rebuildGroupBoundaryPins(group_id)` — recomputes the `boundary_pins` vector based on cross-boundary links in `m_links`.
- `selectedGroupId()` / `setSelectedGroupId(id)` — group-level selection (separate from imnodes' node selection).
- `groupIdForNode(node_id)` / `groupsContainingNode(node_id)` — membership lookups.

`removeNode` cascades: if the removed node is a member of a group, or if removing it drops the group below 2 members, the group is auto-removed.

**Widget rendering** in `NodeGraphWidget`:

- Expanded: a subtle background rectangle drawn via `GetWindowDrawList()` behind the internals, with a `▼ Collapse` button in the title bar.
- Collapsed: a single imnodes-rendered node (id = `group.id` in the `50000+` range) with synthesized boundary pins (`100000+` range) drawn via `BeginInputAttribute` / `BeginOutputAttribute`. Internals are skipped from the `BeginNode/EndNode` cycle. Internal links are skipped. Cross-boundary links are drawn from the real internal pin id (imnodes positions the endpoint from the owning node — if a "phantom node" workaround is needed for the imnodes version pinned in the project, it is documented in the imnodes spike report).
- Rubber-band selection: Shift + left-click drag on empty editor space, with a "Create Subcircuit" popup for naming.
- Probing: synthesized boundary pin ids are translated to real internal pin ids before calling `m_engine.addProbePin` / `removeProbePin`. Probes on internal pins continue to drive the spectrum analyzer when the group is collapsed; the boundary pin shows the corresponding probe color.

**ID space allocation** to keep the four integer ID spaces disjoint:

| Concept | Range |
|---|---|
| `GraphNode::node_id` | `1..49999` |
| `Group::id` | `50000..99999` |
| `GroupBoundaryPin::id` | `100000+` |
| Phantom node ids (if used) | `200000+` |
```

**Step 2: Commit**

```bash
git add ARCHITECTURE.md
git commit -m "docs: document subcircuit groups in ARCHITECTURE.md"
```

---

### Task 26: Update README.md

**Files:**
- Modify: `README.md`

**Step 1: Add the feature to the "Node Editor" usage table**

Find the table that starts with `| Action | Effect |` in README.md. Add rows:

```markdown
| **Shift+left-click + drag** empty canvas | Rubber-band select components |
| **Shift+left-click + drag** ≥ 2 components | Open "Create Subcircuit" popup |
| **Click ▶ on group block** | Expand subcircuit (show internals) |
| **Click ▼ on group title bar** | Collapse subcircuit (show as block) |
| **Right-click** group block | Ungroup / Rename / Collapse / Expand |
| **Click** group block | Select subcircuit (group panel in Inspector) |
```

**Step 2: Commit**

```bash
git add README.md
git commit -m "docs: document subcircuit groups in README.md"
```

---

### Task 27: Update ROADMAP.md

**Files:**
- Modify: `ROADMAP.md`

**Step 1: Add a row to the features table**

Find the existing features table and add:

```markdown
| 11 | **Subcircuit groups** — expandable/collapsible node groups with synthesized input/output pins for navigating large circuits | ✅ Completed | Snapshot editing model; visual layer only, DSP graph stays flat; engine + widget + inspector integration |
```

**Step 2: Commit**

```bash
git add ROADMAP.md
git commit -m "docs: mark subcircuit groups as completed in ROADMAP.md"
```

---

### Task 28: DOX pass — create common/AGENTS.md and update root Child DOX Index

**Files:**
- Create: `common/AGENTS.md`
- Modify: `AGENTS.md` (root)

**Step 1: Read the existing root AGENTS.md**

```bash
cat AGENTS.md
```

Look at the "Child DOX Index" section at the end.

**Step 2: Create `common/AGENTS.md`**

```markdown
# common — AGENTS.md

## Purpose

Own the header-only data model shared by all RF Simulator modules: `SignalNode`, `Spectrum`, `IComponentEngine`, `ViewManager`, `Group`, `GroupBoundaryPin`, and the math utilities in `common.h`.

## Ownership

- `common/include/common.h` — `MIN_FREQ`, `MAX_FREQ`, `NUM_BINS`, `dbToLinear`, `addedNoiseDensity_W_per_Hz`
- `common/include/signal_node.h` — `SignalNode` (input + output + view_enabled)
- `common/include/spectrum.h` — `Spectrum` (frequencies, tones, noise vectors, phase, generation counter)
- `common/include/component_interface.h` — `IComponentEngine` (DSP engine contract)
- `common/include/view_manager.h` — `ViewManager` (registry of `SignalNode*`)
- `common/include/group.h` — `Group` and `GroupBoundaryPin` (subcircuit grouping data)
- `common/include/iq_stream.h` — `IQStream` (used by the digital chain)
- `common/include/nonlinear_model.h` — amplifier nonlinear model helpers
- `common/include/session_state.h` — Windows app.ini read/write
- `common/CMakeLists.txt` — `INTERFACE` library exposing all of the above

## Local Contracts

- All headers are `pragma once`; the directory forms a single `simulator::common` INTERFACE CMake target.
- Engines (in other modules) include `signal_node.h` and `component_interface.h`. Widgets additionally include nothing from `common/` directly; they receive `SignalNode&` references via `IComponentEngine`.
- `Group` is consumed by `NodeGraphEngine` and `NodeGraphWidget`. It is *not* consumed by any DSP engine — groups are a visual layer.

## Work Guidance

- Changes to `SignalNode` or `Spectrum` affect every engine. Update all engines' `update()` and tests.
- New fields on `IComponentEngine` must keep a default implementation that preserves backward compat for all existing engines.
- New files in `common/include/` are automatically picked up by `common/CMakeLists.txt`'s glob.

## Verification

- `cmake --build build && ctest --test-dir build` must pass with zero failures.
- All existing engines must still compile against modified `IComponentEngine`.

## Child DOX Index

No child docs. `common/` is a flat directory.
```

**Step 3: Update the root `AGENTS.md` Child DOX Index**

The root `AGENTS.md` currently contains the message `"This project is not yet indexed. Before continuing you must scan the project, build the DOX tree and replace this message with the actual index."` (or similar). Replace that block with:

```markdown
## Child DOX Index

- [common/AGENTS.md](common/AGENTS.md) — Header-only data model shared by all modules (`SignalNode`, `Spectrum`, `IComponentEngine`, `Group`, etc.)
- [app/AGENTS.md](app/AGENTS.md) — Application orchestrator (`RfSimulatorApp`)
- [node_graph/AGENTS.md](node_graph/AGENTS.md) — Node editor engine and widget, plus subcircuit groups
- [tests/AGENTS.md](tests/AGENTS.md) — Catch2 unit + benchmark tests
- [amplifier/AGENTS.md](amplifier/AGENTS.md) — Amplifier engine + widget
- [signal_generator/AGENTS.md](signal_generator/AGENTS.md) — Signal generator engine + widget
- [spectrum_analyzer/AGENTS.md](spectrum_analyzer/AGENTS.md) — Spectrum analyzer engine + widget
- (other module-specific docs created as needed)
```

(Adjust this list to match the project's actual existing child docs; only add `common/AGENTS.md` if it doesn't already exist.)

**Step 4: Commit**

```bash
git add common/AGENTS.md AGENTS.md
git commit -m "docs: DOX pass for subcircuit groups"
```

---

### Task 29: Add UI tests (imgui_test_engine)

**Files:**
- Modify: `test_engine/ui_tests.cpp` (or create `test_engine/ui_group_tests.cpp`)

**Step 1: Add a UI test for the rubber-band creation flow**

Read the existing `test_engine/ui_tests.cpp` to understand the test framework's API (e.g., how a `Test` is defined, how to simulate input, how to assert engine state).

Add a new test:

```cpp
TEST_F(RfSimulatorFixture, "Subcircuit: Shift+drag creates a group from selected nodes") {
    // Add 3 components via the right-click context menu
    ImGuiTestEngine* eng = ImGuiTestEngine::CreateContext();
    // ... (test framework boilerplate) ...

    // Set up: add Generator, Amplifier, Splitter
    // ... use ImGuiTestEngine's input simulation to click "Add Generator" etc. ...

    // Simulate Shift+left-click drag enclosing the 3 nodes
    ImGuiTestRef editor = ...;
    ImGuiTest* t = ...;
    t->MouseDownOn(editor);
    t->KeyDown(ImGuiKey_LeftShift);
    t->MouseMoveTo(editor, /* x, y of drag end */);
    t->MouseUp();

    // Assert: the CreateSubcircuit popup is open
    // Assert: confirm creates a group with 3 members
}
```

(Exact API calls depend on the project's imgui_test_engine version. Adapt to the existing test patterns in `ui_tests.cpp`.)

**Step 2: Add a UI test for inspector switching**

```cpp
TEST_F(RfSimulatorFixture, "Subcircuit: inspector switches to group panel when group is selected") {
    // ... set up a group ...
    // Click on the collapsed group block
    // Assert: inspector shows the group panel
    // Click "Ungroup" in the inspector
    // Assert: group is removed
}
```

**Step 3: Add a UI test for probe translation**

```cpp
TEST_F(RfSimulatorFixture, "Subcircuit: Ctrl+click on boundary pin probes internal pin") {
    // ... set up a group with a cross-boundary link ...
    // Collapse the group
    // Ctrl+click the boundary pin
    // Assert: engine.probePins() contains the internal pin id
}
```

**Step 4: Run UI tests**

```bash
cmake --build build
./build/bin/test_engine
```

Expected: All UI tests pass.

**Step 5: Commit**

```bash
git add test_engine/ui_tests.cpp  # or new file
git commit -m "test: UI tests for subcircuit groups"
```

---

### Task 30: Self-review against the spec

**Step 1: Re-read the spec and verify coverage**

Open `docs/superpowers/specs/2026-06-21-subcircuit-groups-design.md` and walk through each section. For each requirement, verify there is a task that implements it:

- Section 1 (Goal) — covered by Tasks 4-9, 10-13, 15-23
- Section 2 (Scope: in) — covered by Tasks 2-30
- Section 2 (Scope: out) — explicitly not implemented; verify no task adds these
- Section 3 (Architecture) — Tasks 2-3, 10 establish the layering
- Section 4 (Data model) — Task 2 (header), Task 3 (engine collection)
- Section 5 (Engine API) — Tasks 4-7
- Section 6 (Widget expanded state) — Task 11
- Section 7 (Widget collapsed state) — Tasks 12-13
- Section 8 (Boundary pin synthesis) — Task 7
- Section 9 (Cross-boundary link rendering) — Tasks 13-14
- Section 10 (User interactions) — Tasks 15-16 (rubber-band + create), 19 (right-click)
- Section 11 (Inspector integration) — Task 18
- Section 12 (Probing integration) — Task 20
- Section 13 (CMake) — no new targets; verified
- Section 14 (Testing) — Tasks 4-9 (engine tests), 9 (integration), 24 (benchmarks), 29 (UI tests)
- Section 15 (Implementation phasing) — followed
- Section 16 (Risks) — risk #1 (imnodes API) handled by Task 1 (spike); risks #3-7 handled by tasks throughout

**Step 2: Run the full test suite to confirm everything passes**

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: All tests pass (existing + new).

**Step 3: Run benchmarks to confirm performance**

```bash
./build/bin/tests "[benchmark][group]"
```

Expected: `rebuildGroupBoundaryPins` for 100 cross-boundary links completes in well under 1 ms per call.

**Step 4: Final review and commit any cleanup**

```bash
git status
```

If there are any uncommitted changes, commit them with an appropriate message.

**Step 5: Done**

The feature is complete. Ready for review and merge.

---

## Summary

| Phase | Tasks | Days |
|---|---|---|
| 0 — imnodes spike | 1 | 1 |
| 1 — Engine API | 2-9 | ~3-4 |
| 2 — Widget expanded | 10-11 | ~2-3 |
| 3 — Widget collapsed | 12-14 | ~2-3 |
| 4 — Rubber-band | 15-16 | ~1-2 |
| 5 — Inspector + right-click | 17-19 | ~2-3 |
| 6 — Probes | 20-21 | ~1-2 |
| 7 — Links | 22-23 | ~1-2 |
| 8 — Polish + docs | 24-30 | ~1-2 |
| **Total** | **30 tasks** | **~14-22 days** |

**PR shape:** PR 1 = Tasks 1-13 (engine + widget rendering); PR 2 = Tasks 15-30 (creation, integration, polish, docs). Each PR is independently reviewable and shippable.
