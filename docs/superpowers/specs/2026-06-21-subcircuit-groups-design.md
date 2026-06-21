# Subcircuit Groups — Design Spec

**Date:** 2026-06-21
**Status:** Approved (pending review of this document)
**Branch:** `docs/subcircuit-groups-design`

---

## 1. Goal

Add **single-level subcircuit groups** to the node editor: the user can rubber-band-select a set of existing components, give the selection a name, and display the bundle as a single block with synthesized input/output pins. When collapsed, the internals are hidden and the block is the only thing the user sees; when expanded, the internals are visible. The DSP layer is **unchanged** — the signal graph stays flat, and probing/inspecting internal components continues to work whether the group is expanded or collapsed.

This unblocks navigating large RF chains (LNA sections, mixer chains, IF strips, PFB banks) without forcing a full hierarchical design.

## 2. Scope

### In scope (v1)

- A `Group` data model and matching `NodeGraphEngine` API additions.
- Rubber-band selection (Shift + left-click drag on empty editor space) with a "Create Subcircuit" popup.
- Expanded and collapsed rendering of a group inside `NodeGraphWidget`, with synthesized boundary pins on collapsed blocks.
- Cross-boundary link rendering: links that exit the group are drawn from the boundary pin (collapsed) or from the internal pin (expanded), without mutating the engine's link table.
- Probing: internal probes keep working; boundary pins show the corresponding probe color; Ctrl-click on a boundary pin adds a probe to the corresponding internal pin.
- Inspector: a new "group panel" branch driven by a new `selectedGroupId` engine state.
- Snapshot lifecycle: ungroup (right-click → Ungroup, or inspector button) leaves the member nodes and their links intact.
- Engine-level auto-cleanup: removing a member of a group auto-removes the group; if a group drops below 2 members, it is auto-removed.
- ~25 engine-level unit tests + ~10 UI tests + 2 benchmarks.

### Out of scope (deferred)

- **Nesting** — a group cannot contain another group. Rubber-band that encloses an already-grouped node is rejected.
- **Save/load of groups** — graph state is not persisted today; group persistence is its own feature.
- **Reusable subcircuit modules** — saving a group as a named module and instantiating it elsewhere.
- **Move-with-internals on the collapsed block** — the block's position is read-only, derived from the centroid of member positions each frame.
- **Custom boundary pin labels** — labels are auto-derived (`<internal node label> <pin label>`).
- **Multi-group selection** — only one group can be selected at a time.
- **Copy/paste of groups** — Ctrl+C / Ctrl+V of a subcircuit.
- **Hierarchical inspector drill-down** — group panel is read-only on members; to edit, the user expands the group.
- **Group color / icon customization** — all groups render with the same neutral style.
- **Auto-grouping / smart suggestions** — pure user-driven creation only.

## 3. Architecture

### Module layout

A new header joins `common/`, and the existing `node_graph/` module grows:

```
common/include/group.h                  # Group, GroupBoundaryPin structs (header-only)
node_graph/include/node_graph_engine.h  # gains group API
node_graph/src/node_graph_engine.cpp    # gains group collection, link-cascade hook
node_graph/include/node_graph_widget.h  # gains subcircuit state
node_graph/src/node_graph_widget.cpp    # gains rubber-band, group rendering, synth id translation
app/src/inspector_panel.cpp             # gains group-panel branch
tests/test_group.cpp                    # new engine-level tests
test_engine/ui_tests.cpp                # gains UI tests (or new ui_group_tests.cpp)
```

The `app` target picks up the new common header via the existing `common` interface library. The `node_graph_engine` and `node_graph_widget` targets are modified in place; no new CMake targets are added. No new third-party dependencies.

### Why this layering

- **`common/`** owns the data types because `Group` is referenced from both the engine and the widget.
- **`node_graph_engine`** is the source of truth for group membership and boundary pin synthesis, but does **not** re-route links. The DSP-facing engine methods (`topologicalOrder`, `getSourceForInput`, `probedSignalNodes`, `addLink`, `addProbePin`) are unchanged in behavior.
- **`node_graph_widget`** owns the rendering, the rubber-band gesture, and the synthesized pin-id space. The widget translates between synthesized IDs (drawn on screen) and real IDs (stored in the engine) at the boundary, so the engine never has to know about the difference.

## 4. Data model

### `common/include/group.h` (new, header-only)

```cpp
#pragma once

#include <string>
#include <vector>

struct GroupBoundaryPin {
    int    id;                 // synthesized, unique within the group; >= 100000
    int    internal_node_id;   // the in-group node that owns the internal pin
    int    internal_pin_id;    // the engine's real pin id
    bool   is_output;         // true = subcircuit output (link source is in-group)
    std::string label;         // "<internal node label> <pin label>"
};

struct Group {
    int    id;                 // allocated by the engine from its own counter; >= 50000
    std::string name;          // user-editable; default "Subcircuit N"
    std::vector<int> member_node_ids;            // frozen after creation (snapshot model)
    std::vector<GroupBoundaryPin> boundary_pins; // recomputed by rebuildGroupBoundaryPins
    bool   collapsed = false;
};
```

### ID space allocation

To keep three integer ID spaces (real `GraphNode` IDs starting at 1, real pin IDs starting at 100, link IDs starting at 1000) disjoint from group-related IDs, the engine uses:

| Concept | Range | Counter |
|---|---|---|
| `GraphNode::node_id` | `1..49999` | `m_next_node_id` (unchanged) |
| `Group::id` | `50000..99999` | new `m_next_group_id` |
| `GroupBoundaryPin::id` | `100000+` | new `m_next_boundary_pin_id` |

The existing pin-id counter (`m_next_pin_id`, starting at 100) is unchanged.

## 5. Engine API additions (`NodeGraphEngine`)

All new methods. No existing method's signature changes.

```cpp
// Construction-time / snapshot edits
int  addGroup(std::string name, std::vector<int> member_node_ids);
     // returns positive group id, or -1 on validation failure
     // validates: >= 2 members, no node in another group, no duplicate members

void removeGroup(int group_id);
     // removes the group record; member nodes and ALL their links (internal AND
     // cross-boundary) are left in place, unchanged
     // selection is cleared (selectedGroupId reset to -1 if it was this group)

void renameGroup(int group_id, std::string name);

// Collapse state
void setGroupCollapsed(int group_id, bool collapsed);
bool isGroupCollapsed(int group_id) const;

// Cross-boundary recomputation
void rebuildGroupBoundaryPins(int group_id);
     // walks m_links, synthesizes one GroupBoundaryPin per cross-boundary link
     // must be called by the widget after any m_links change that affects the group
     // idempotent if called twice with no link change

const std::vector<int>& groupsContainingNode(int node_id) const;
int  groupIdForNode(int node_id) const;       // -1 if not in a group, else single group id

// Selection
int  selectedGroupId() const;
void setSelectedGroupId(int id);              // -1 to deselect

// Accessors
const std::vector<Group>& groups() const;
const Group* groupById(int group_id) const;
int  numGroups() const;
```

### `removeNode` cascade

`removeNode` (existing) is extended with one extra step after its current pin/probe/link cleanup:

```cpp
void NodeGraphEngine::removeNode(int node_id) {
    // ... existing logic: collect pins, remove links, remove probes, erase node ...
    for (auto& g : m_groups) {
        auto it = std::find(g.member_node_ids.begin(),
                            g.member_node_ids.end(),
                            node_id);
        if (it != g.member_node_ids.end()) {
            // mark for removal; defer the actual erase to the end of the loop
            g.member_node_ids.erase(it);
            groups_to_remove.push_back(g.id);
        }
        if (g.member_node_ids.size() < 2 && !marked_for_remove(g.id)) {
            groups_to_remove.push_back(g.id);
        }
    }
    for (int gid : groups_to_remove) {
        removeGroup(gid);
    }
}
```

This auto-cleans groups whose membership drops to < 2, including the case where the only member is being removed.

### `addGroup` validation

```cpp
if (member_node_ids.size() < 2) return -1;
for (int nid : member_node_ids) {
    if (groupIdForNode(nid) != -1) return -1;       // already in a group
    if (std::find_if(m_nodes.begin(), m_nodes.end(),
                     [nid](const GraphNode& n) { return n.node_id == nid; })
        == m_nodes.end()) return -1;                // unknown node
}
std::sort(member_node_ids.begin(), member_node_ids.end());
if (std::adjacent_find(member_node_ids.begin(), member_node_ids.end())
    != member_node_ids.end()) return -1;            // duplicate
```

## 6. Widget rendering — expanded state

Internals render normally. The group is visible as a subtle background rectangle drawn *behind* the internals.

- The rectangle is drawn via `ImGui::GetWindowDrawList()->AddRectFilled()` and `AddRect()` in **grid space** (so it pans and zooms with the canvas).
- Bounds = the union of all members' `ImNodes::GetNodeGridPos()` positions, padded by ~16 px.
- Fill: low-alpha background (e.g. `IM_COL32(80, 80, 120, 24)`); stroke: 1 px line (`IM_COL32(120, 120, 180, 96)`). Exact colors tuned during implementation.
- A small title bar (`ImGui::GetWindowDrawList()->AddText()` plus a button) is drawn at the top-left of the rectangle, showing the group name and a `▼ Collapse` button.
- Internals render on top of the rectangle because `drawGroupBackgrounds()` is called *before* the `BeginNode` cycle in the widget's `draw()` method.
- Cross-boundary links and internal links render normally (start pin → end pin, both real IDs). Boundary pins are **not** rendered in the expanded state — they are reserved for the collapsed state.

Hit-testing the `▼` button: `GetWindowDrawList()` doesn't accept ImGui input, so we capture the click via `ImGui::IsMouseClicked(0)` plus a manual bounding-box test against the button's grid-space rect, converted to screen space using imnodes' `ScreenToGrid` / `GridToScreen` (or the manual pan/zoom transform if those are absent).

## 7. Widget rendering — collapsed state

The group is rendered as a single styled **imnodes node**. Internals are entirely skipped.

- The node's `id` is the `Group::id` (in the `50000+` range, disjoint from real `GraphNode` IDs).
- Title bar: the group name, with a `▶ Expand` button.
- Body: a fixed-size styled area (no internals visible); the body size is a compile-time constant (e.g. 120 × 80 px).
- Boundary pins are rendered with `BeginInputAttribute` / `BeginOutputAttribute` using synthesized IDs in the `100000+` range.
- Pin positions are computed at draw time: inputs on the left edge top-to-bottom, outputs on the right edge top-to-bottom. The widget iterates `m_engine.groups()[i].boundary_pins` in order to lay them out.
- Internal nodes: skip the `BeginNode` / `EndNode` cycle entirely. Internal links: skip the `Link()` call entirely. **See section 8 for the cross-boundary link rendering rule.**

Hit-testing the `▶` button: handled by imnodes' standard attribute interaction, since the button is part of the imnodes-rendered node.

### Block position

The block's grid position is read-only and is recomputed every frame as the **centroid** of the members' `GetNodeGridPos()` positions. To "move" a subcircuit, the user expands, drags the internals, then collapses — the block re-centers automatically.

## 8. Boundary pin synthesis

`NodeGraphEngine::rebuildGroupBoundaryPins(group_id)` runs the following algorithm:

1. Look up the group by id; collect `member_set = {member_node_ids}` into a hash set for O(1) lookup.
2. For each `link` in `m_links`:
   - Resolve `start_node_id` = `nodeIdForPin(link.start_pin_id)`.
   - Resolve `end_node_id` = `nodeIdForPin(link.end_pin_id)`.
   - If `start_node_id` is in `member_set` and `end_node_id` is **not**:
     - This is a cross-boundary link on the **output** side. Allocate a `GroupBoundaryPin { id = next_boundary_pin_id++, internal_node_id = start_node_id, internal_pin_id = link.start_pin_id, is_output = true, label = nodeLabel(start_node_id) + " " + pinLabel(start_node_id, output) }`. Append to `group.boundary_pins`.
   - If `end_node_id` is in `member_set` and `start_node_id` is **not**:
     - Cross-boundary link on the **input** side. Same allocation, but `is_output = false`, label uses the input pin label.
   - If both endpoints are in the group, or both are outside, the link is **not** a cross-boundary link; skip.
3. Replace `group.boundary_pins` with the new vector.

`label` derivation:
- If the in-group `GraphNode` has a non-empty `output_labels[i]` / `input_labels[i]` matching `pin_id`, use that.
- Else fall back to `"OUT"` / `"IN"`.

`nodeLabel` reads `GraphNode::label`.

### Idempotency and ID reuse

`rebuildGroupBoundaryPins` always clears `group.boundary_pins` and synthesizes from scratch. Each new boundary pin gets a fresh id from `m_next_boundary_pin_id++`, monotonically increasing. The previous IDs are not recycled.

The widget's per-frame `synth_to_real_pin` map is rebuilt every frame from the engine state, so stale boundary-pin IDs are harmless — by the next frame, the new synthesis has produced the canonical mapping. The counter grows by at most one per cross-boundary link per rebuild, so a 50-group / 100-cross-boundary-link circuit never approaches the 2³¹ ID ceiling in a single session.

### When `rebuildGroupBoundaryPins` is called

The widget calls it in exactly two situations:

1. After the widget processes a link creation, link deletion, or `addLink` / `removeLink` that involves a member of the group.
2. After `removeNode` cascades and removes a member of the group (the engine's `removeNode` extension in section 5 handles this internally — the widget just calls `removeNode` and the engine does the rest).

The widget does **not** call it on every frame; the synthesis is cached and only recomputed when link state changes.

## 9. Cross-boundary link rendering

When the group is **collapsed**, every cross-boundary link is drawn as `ImNodes::Link(link.link_id, link.start_pin_id, link.end_pin_id)`. Both endpoints are real engine pin IDs (not synthesized). imnodes positions the in-group endpoint by looking up the pin's owning node's screen-space bounding box.

The internal node is **not** drawn (we skipped its `BeginNode`/`EndNode` cycle), so imnodes may not have a current bounding box for it. The phase-0 spike verifies whether this is a problem; the workaround, if needed, is the "phantom node" technique described below.

### Phantom node workaround

If the spike finds that imnodes does not know the position of a pin whose owning node was not drawn this frame, the widget renders the in-group node as a **phantom** with a synthesized node id in the `200000+` range (well above real `GraphNode` IDs and group IDs):

- `ImNodes::BeginNode(phantom_id);` — opens the cycle with the synthesized id
- `ImNodes::BeginNodeTitleBar(); ImGui::Dummy(ImVec2(0, 0)); ImNodes::EndNodeTitleBar();` — zero-height title bar
- For each pin: `ImNodes::BeginInputAttribute(pin_id); ImGui::Dummy(ImVec2(0, 0)); ImNodes::EndInputAttribute();` — zero-content attribute
- `ImNodes::EndNode();` — closes the cycle

The phantoms are drawn in addition to the real group block, after `drawGroupBackgrounds()` and before `drawLinks()`. The widget maintains a per-frame `phantom_id_for_node` map (built alongside `synth_to_real_pin`) so it can also resolve phantom ids if a hit-test ever reports one. To prevent stale phantom selection state from imnodes, the widget calls `ImNodes::ClearNodeSelection()` at the start of every frame.

If the phantom workaround is not needed (imnodes positions pins correctly even when the owning node isn't drawn this frame), the phantom code is simply not present and `200000+` is unused.

## 10. User interactions

### Rubber-band → create

- Activation: **Shift + left-click drag** on empty editor space (`ImNodes::IsEditorHovered() && !IsNodeHovered() && !IsLinkHovered() && ImGui::IsKeyDown(ImGuiKey_LeftShift)`).
- During drag: draw a semi-transparent rectangle via `GetWindowDrawList()->AddRectFilled()` in screen space, converted to grid space for the hit test.
- On release: convert the rectangle to grid space; for each `m_engine.nodes()` entry, get the node's `GetNodeGridPos`, take the node's center (position + half the node's known size), and check if the center is inside the grid-space rectangle. If `0` or `1` nodes are enclosed, the rubber-band is silently cancelled. If `>= 2` valid members are enclosed, the "Create Subcircuit" popup opens.

### "Create Subcircuit" popup

- Read-only list of member labels.
- `ImGui::InputText` for the name (default: `Subcircuit N` where N is `engine.numGroups() + 1`).
- Confirm button: `m_engine.addGroup(name, member_node_ids)`. Cancel: no-op.

### Collapse / expand

- Click `▼` on expanded title bar → `m_engine.setGroupCollapsed(group_id, true)`.
- Click `▶` on collapsed block → `m_engine.setGroupCollapsed(group_id, false)`.

### Rename

- Double-click on the group name (either title bar or block title bar) → inline `ImGui::InputText` → `m_engine.renameGroup(group_id, new_name)` on commit (Enter or focus loss).

### Ungroup

- Right-click on group block / title bar → context menu → "Ungroup" → `m_engine.removeGroup(group_id)`. Member nodes are left in place with all their links intact.
- Inspector → "Ungroup" button does the same.

### Auto-cleanup

- `removeNode` (existing, called by Delete key or right-click → Remove) auto-removes any group whose member count drops below 2, per section 5's extension.

## 11. Inspector integration

`InspectorPanel::draw()` gains one branch at the top:

```cpp
int gid = m_graph_engine.selectedGroupId();
if (gid >= 0) {
    drawGroupPanel(gid);
    return;
}
// ... existing node-property drawer ...
```

`drawGroupPanel(gid)` renders:

- Editable name field (`ImGui::InputText` with a backing buffer; on `ImGui::IsItemDeactivatedAfterEdit()`, call `m_graph_engine.renameGroup`).
- `Members (N)` header followed by a read-only list: one `ImGui::TextUnformatted(member.label)` per entry in `group.member_node_ids`.
- `Boundary pins` header followed by a read-only list: one line per entry in `group.boundary_pins`, formatted as `OUT 1 → "<internal node label> <pin label>"` (or `IN` for inputs).
- `Ungroup` button at the bottom → `m_graph_engine.removeGroup(gid)`; selection is cleared by `removeGroup` itself.

The existing node-property drawer is unchanged. When `selectedGroupId() == -1`, the inspector behaves exactly as before.

## 12. Probing integration

The existing `handleProbeClick()` (in `NodeGraphWidget`) is extended with a synthesized-pin translation step:

```cpp
// pseudocode
if (ImGui::IsMouseReleased(0) && (ctrl || shift)) {
    int target_pin = m_clicked_pin;
    if (target_pin >= 100000) {
        // synthesized boundary pin id; translate to real internal pin id
        auto it = m_synth_pin_to_real_pin.find(target_pin);
        if (it != m_synth_pin_to_real_pin.end()) {
            target_pin = it->second;
        } else {
            target_pin = -1;  // stale pin id, ignore
        }
    }
    if (target_pin >= 0) {
        if (ctrl) m_engine.addProbePin(target_pin);
        else if (shift) m_engine.removeProbePin(target_pin);
    }
}
```

`m_synth_pin_to_real_pin` is a `std::unordered_map<int, int>` on the widget, rebuilt every frame from the engine's `m_groups` so it always reflects the current boundary-pin synthesis.

### Group-block click handling

The existing `handleProbeClick()` also handles a click on a node (not a pin) by probing the node's first output pin. We extend this for groups: if `m_clicked_node >= 50000` (a group id), the widget looks up the group's first *output* boundary pin via `engine.groupById(group_id)->boundary_pins` (filtered by `is_output == true`) and probes its `internal_pin_id`. The behavior is therefore consistent with the regular pin-click path: Ctrl-click on a group block probes the first boundary output.

### Boundary pin probe color

The existing probe-color rendering in `drawNodes()` already pushes one of four probe-slot colors based on `m_engine.probeSlotForPin(pin_id)`. For boundary pins, we add a translation step:

```cpp
for (const auto& group : m_engine.groups()) {
    if (!group.collapsed) continue;
    for (const auto& bp : group.boundary_pins) {
        int slot = m_engine.probeSlotForPin(bp.internal_pin_id);
        if (slot >= 0) {
            ImNodes::PushColorStyle(ImNodesCol_Pin, probe_colors[slot]);
            ImNodes::PushColorStyle(ImNodesCol_PinHovered, probe_colors[slot]);
        }
        // ... draw boundary pin with BeginInputAttribute(bp.id) / BeginOutputAttribute(bp.id) ...
        if (slot >= 0) {
            ImNodes::PopColorStyle();
            ImNodes::PopColorStyle();
        }
    }
}
```

This way, a probe on an internal pin shows up on the corresponding boundary pin when the group is collapsed. When the group is expanded, the probe color shows on the internal pin (existing behavior).

## 13. CMake / module layout

No new CMake targets. The new header `common/include/group.h` joins the existing `common` interface library (the `CMakeLists.txt` in `common/` already globs `include/*.h`). The engine and widget source files are modified in place.

`tests/CMakeLists.txt` gains one new entry: `test_group.cpp` in `TEST_SOURCES`.

`test_engine/CMakeLists.txt` (if separate) gains the UI test cases. If the test file is too large, a new `test_engine/ui_group_tests.cpp` is created.

## 14. Testing

### Engine-level unit tests in `tests/test_group.cpp`

Mirrors the style of `tests/test_node_graph_engine.cpp`. Catch2 v3, no ImGui.

**Group creation validation**

- `addGroup` with 0 members returns `-1`; `engine.groups().size() == 0`
- `addGroup` with 1 member returns `-1` (≥ 2 required)
- `addGroup` with 2 valid members returns positive id; new group has the right `member_node_ids`
- `addGroup` with a member already in an existing group returns `-1`
- `addGroup` with the same node listed twice returns `-1`
- `addGroup` with a non-existent member id returns `-1`

**Group state changes**

- `setGroupCollapsed(id, true)` flips the flag
- `renameGroup(id, "X")` updates the name
- `selectedGroupId()` defaults to `-1`; `setSelectedGroupId(id)` round-trips

**Ungrouping**

- `removeGroup` leaves every member in `m_nodes`; ALL of their links (internal AND cross-boundary) remain in `m_links` unchanged
- After `removeGroup`, `groupsContainingNode(any_member_id)` returns empty
- `removeGroup` clears `selectedGroupId` if it was the removed group
- `removeGroup` of a non-existent id is a no-op

**Auto-cleanup on member removal**

- `removeNode` of the only member of a group auto-removes the group
- `removeNode` of one of two members auto-removes the group (drops to < 2)
- `removeNode` of one of N ≥ 3 members leaves the group intact
- Probes on a removed member's pins are cleared (existing `removeNode` behavior)

**Boundary pin synthesis**

- Group with no cross-boundary links: empty `boundary_pins` (but `GroupBoundaryPin` count is zero, not "pins with no link")
- One cross-boundary output link: one boundary pin, `is_output = true`, correct fields
- One cross-boundary input link: one boundary pin, `is_output = false`
- N cross-boundary links of mixed directions: N pins with right flags
- All links inside the group: no synthesis
- All links outside the group: no synthesis
- Boundary pin IDs are unique within a group
- Boundary pin IDs are ≥ `100000`
- `rebuildGroupBoundaryPins` clears and re-synthesizes; second call with no link change produces the same number of pins with the same `(internal_node_id, internal_pin_id, is_output, label)` fields, but pin ids may differ
- Label uses the engine's per-pin label if set, else falls back to `"IN"` / `"OUT"`

### Integration tests in `tests/test_group.cpp`

- Generator → Amplifier → Splitter chain, all grouped: signal still propagates from input through the group to the splitter's two outputs when the group is collapsed
- Probing the amplifier's output pin (internal) drives the spectrum analyzer when the group is collapsed
- `topologicalOrder` returns the same order with or without the group

### UI tests in `test_engine/ui_tests.cpp` (or new `ui_group_tests.cpp`)

imgui_test_engine. One `Test` per user-visible interaction:

- Rubber-band encloses 3 nodes → "Create Subcircuit" popup opens with the 3 names; confirm → group is created
- Rubber-band encloses 1 node → no popup, no group
- Rubber-band encloses a node already in a group → no popup, no group (overlap rejection)
- Click on a collapsed block → `engine.selectedGroupId() == group_id`
- Click on `▼` button → `engine.isGroupCollapsed(id) == true`
- Click on `▶` button → `engine.isGroupCollapsed(id) == false`
- Right-click on a group block → context menu has "Ungroup", "Rename", "Collapse / Expand"
- Click "Ungroup" → group is removed; member nodes and their links are intact
- Inspector with a group selected shows the group panel
- Inspector "Ungroup" button works
- Ctrl-click on a boundary pin (collapsed) → `engine.probePins()` contains the internal pin id
- Boundary pin shows probe color when the internal pin is probed
- Cross-boundary link renders from boundary pin (collapsed) or from internal pin (expanded)

### Benchmarks in `tests/test_bench_dsp.cpp` (or new `test_bench_groups.cpp`)

- `rebuildGroupBoundaryPins` for a group of 50 members with 100 cross-boundary links: mean time per call
- Group add/remove cycle for 100 groups of 10 members each: mean time per cycle

## 15. Implementation phasing

### Phase 0 — imnodes spike (1 day, blocking)

Build the project, verify the imnodes API surface (sections 6, 7, 8, 9), document the result, and update the spec's coordinate-plumbing details if the API doesn't match expectations. Output: a one-page spike report in the PR.

### Phase 1 — Data model + engine API (1–2 days)

`common/include/group.h`, `NodeGraphEngine` additions, `removeNode` cascade, `tests/test_group.cpp` (engine-level tests only).

### Phase 2 — Widget rendering: expanded state (2–3 days)

Background rectangle, title bar, `▼` button, expanded-state UI tests.

### Phase 3 — Widget rendering: collapsed state (2–3 days)

ImNodes-rendered group block, boundary pins, collapsed-state UI tests, phantom-node workaround if the spike says we need it.

### Phase 4 — Rubber-band + group creation (1–2 days)

Shift+drag detection, rectangle drawing, node hit testing, "Create Subcircuit" popup, `addGroup` wiring.

### Phase 5 — Inspector + right-click + selection (2–3 days)

Inspector group panel, right-click context menu on groups, group selection state, `ClearNodeSelection` coordination.

### Phase 6 — Probe translation (1–2 days)

Synthesized-pin translation in `handleProbeClick`, boundary pin probe color in `drawNodes`.

### Phase 7 — Link creation / destruction translation (1–2 days)

Synthesized-pin translation in `handleLinkCreation` and `handleLinkDeletion`, `rebuildGroupBoundaryPins` invocation after link changes.

### Phase 8 — Polish, benchmarks, docs (1–2 days)

Run benchmarks, update `ARCHITECTURE.md` / `README.md` / `ROADMAP.md`, DOX pass per `AGENTS.md`, self-review against this spec.

### PR shape

**Two PRs.**

- **PR 1 — Core (phases 0–3).** Data model, engine API, expanded and collapsed rendering, engine tests, imnodes spike report. At the end of this PR, groups can be created via a test fixture or a temporary debug command, and the rendering layer is reviewable in isolation.
- **PR 2 — Creation + integration (phases 4–8).** Rubber-band selection, inspector panel, right-click context menu, probe and link translation, benchmarks, docs.

## 16. Risks & follow-ups

### Risks

1. **imnodes API surface (highest).** The phase-0 spike verifies `ImNodes::GetNodeGridPos` and the screen↔grid transform in the pinned revision. Fallbacks (track positions ourselves, manual pan/zoom transform) are noted in sections 6 and 8 and are equivalent in complexity.

2. **Cross-boundary link rendering via phantom nodes.** If imnodes doesn't position pins correctly when the owning node isn't drawn, the phantom-node workaround in section 9 is used. If phantoms also fail, we compute pin positions from the node's grid pos + a constant offset per pin and translate the link rendering to use those positions.

3. **Selection state confusion.** The widget calls `ImNodes::ClearNodeSelection()` when a group block is clicked, so `selectedGroupId` and imnodes' node selection never disagree.

4. **Multi-port components inside a group.** A 4-port component inside a group may have 4 cross-boundary links; the synthesis enumerates each independently. No special case needed. Verified by the "N cross-boundary links of mixed directions" test.

5. **Performance with many groups.** For 50 groups / 500 nodes, this is comfortably linear. The `rebuildGroupBoundaryPins` benchmark catches O(N²) regressions early.

6. **Group ID collision.** Group IDs are allocated from `m_next_group_id` starting at `50000`, disjoint from `m_next_node_id` (which starts at `1`).

7. **Probe-on-internal-pin visibility when collapsed.** Probes still drive the spectrum analyzer. The pin itself is hidden; the boundary pin shows the probe color as a proxy. Straightforward because each boundary pin is uniquely associated with one cross-boundary link, which has exactly one in-group pin id.

### Follow-ups (post-v1)

- Nesting (group contains group), if and when needed.
- Save/load of graph state, including groups.
- Reusable subcircuit modules (save group as a named module, instantiate in another circuit).
- Move-with-internals on the collapsed block (drag the block, internals follow).
- Custom boundary pin labels (user-editable).
- Multi-group selection + group-level batch operations (move, delete, recolor).
- Copy/paste of groups (with all internals and cross-boundary links).
- Hierarchical inspector drill-down (group panel → member properties).
- Group colors / icons / thumbnails.

---

**End of spec.** Please review and let me know if you want to make any changes before we start writing out the implementation plan.
