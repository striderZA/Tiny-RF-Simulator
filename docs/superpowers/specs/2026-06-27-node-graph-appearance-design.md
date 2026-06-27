# Node Graph Appearance — Design Spec

**Date:** 2026-06-27
**Status:** Approved (pending review of this document)
**Branch:** `feat/node-graph-appearance`
**Goal:** Replace the current "empty gray box" look of node graph components with a modern dark canvas and per-component-type schematic symbols. Make the visual identity match the project's purpose as an RF signal-chain simulator.

---

## 1. Goal

The node graph is the central UI of the simulator — every interaction (adding components, wiring, probing, grouping) happens here. Today, every node renders as a generic gray imnodes box with no icon, no component-type differentiation, and the default light imnodes theme. All ten component types look identical. This makes the graph harder to scan, hides the "schematic" character of an RF tool, and makes adding new components feel weightless.

This spec delivers:

1. A **dark canvas** with a subtle visible grid.
2. **Per-component-type color coding** via the title bar and node border.
3. **Schematic vector symbols** drawn directly on each node body using `ImDrawList` primitives (no PNGs, no asset files).
4. A `NodeKind` enum used by the widget to look up the right color + symbol. Kind is derived from the existing `label` prefix at render time — **no engine changes, no GraphNode field changes**.

The DSP layer is **unchanged**. All 67 existing unit tests and 6 benchmarks must still pass. Group rendering is preserved.

## 2. Scope

### In scope (v1)

- A `NodeKind` enum declared in `node_graph_engine.h` (engine module, no DSP meaning).
- A `nodeKindFromLabel(const std::string& label)` helper in the widget that maps label prefixes to `NodeKind`.
- A `themeColor(NodeKind)` lookup function and a `setupDarkTheme()` function in the widget.
- Schematic symbol helpers (one per component type) drawn with `ImDrawList`.
- The empty `ImGui::Dummy(80, 56)` body in `drawNodes()` is replaced by a `drawSchematicSymbol()` call.
- Group collapsed block reuses the same machinery (kind = `NodeKind::GroupCollapsed`, hard-coded in `drawGroupCollapsedBlocks`).
- New unit tests for the kind prefix lookup, color lookup, and unknown-fallback path.
- A manual verification checklist for the visual side.

### Out of scope (deferred)

- Animated / glowing probed links.
- Light vs dark theme switching.
- Per-component user-customizable colors or symbols.
- HiDPI-specific stroke widths.
- Icon assets (PNGs) — the v1 look is entirely vector.
- Runtime theme API (one theme: dark).

### Out of scope (deferred)

- Animated / glowing probed links.
- Light vs dark theme switching.
- Per-component user-customizable colors or symbols.
- HiDPI-specific stroke widths.
- Icon assets (PNGs) — the v1 look is entirely vector.
- Runtime theme API (one theme: dark).

## 3. Architecture

### Module layout

No new module. All changes are in-place edits to the node_graph module only:

```
node_graph/include/node_graph_engine.h      # + NodeKind enum (no other change)
node_graph/include/node_graph_widget.h      # + setupDarkTheme, drawSchematicSymbol declarations
node_graph/src/node_graph_widget.cpp        # + dark theme, + 10 symbol helpers, + kind-from-label lookup, per-kind colors in drawNodes()
tests/test_node_graph_engine.cpp            # + 3 new test cases (kind lookup, color lookup, default Unknown)
```

**No engine.cpp changes. No GraphNode field changes. No `addNode()` parameter changes. No changes to any other module (app, component_registry, any engine).** The widget derives the kind from the existing `GraphNode::label` at render time, which is set in each engine constructor and never changes during a node's lifetime.

### Why this layering

- **Zero engine touch** — the engine knows nothing about the new visual system. `GraphNode` is unchanged; `addNode()` is unchanged.
- **The widget owns the theme** — colors, the label→kind map, and symbol draw functions all live in `NodeGraphWidget`, not in `IconRegistry`. The existing `IconRegistry` is untouched (still loaded with zero icons; remains a no-op until PNGs are added later).
- **Label-prefix matching is safe** — each engine constructor sets a unique, stable label prefix (e.g. `"Generator "`, `"Amplifier "`, `"S-Param "`). The widget matches these prefixes; no engine code is involved. The `Unknown` fallback covers any future engine that doesn't follow the convention, rendering as a gray node with no symbol — visible bug, not a crash.
- **Groups are handled in their own draw path** — `drawGroupCollapsedBlocks()` uses `NodeKind::GroupCollapsed` directly, no label matching needed.

## 4. Data model

### `node_graph/include/node_graph_engine.h`

A new enum is added. **No other changes to this file** — `GraphNode` keeps its existing fields, `addNode()` keeps its existing signature.

```cpp
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
```

The `NodeKind` enum lives in the node_graph engine header because the widget already depends on that header. It is **purely a view-layer type** — the engine never reads it, stores it, or returns it.

## 5. Visual design

### Color palette

| `NodeKind` | Display name | Title bar / border color |
|---|---|---|
| `Generator`     | "Generator"        | `#4ade80` (green)   |
| `Amplifier`     | "Amplifier"        | `#fb923c` (orange)  |
| `Mixer`         | "Mixer"            | `#c084fc` (purple)  |
| `Splitter`      | "Splitter"         | `#facc15` (amber)   |
| `Adc`           | "ADC"              | `#60a5fa` (blue)    |
| `PFB`           | "PFB Channelizer"  | `#22d3ee` (cyan)    |
| `IdealFilter`   | "Ideal Filter"     | `#2dd4bf` (teal)    |
| `CoaxCable`     | "Coax Cable"       | `#94a3b8` (slate)   |
| `SParam`        | "S-Param"          | `#f472b6` (pink)    |
| `GroupCollapsed`| (subcircuit)       | `#818cf8` (indigo)  |
| `Unknown`       | (fallback)         | `#9ca3af` (gray)    |

The `themeColor(NodeKind)` helper returns the color as an `IM_COL32` literal. Each color is used for both `ImNodesCol_TitleBar` and `ImNodesCol_NodeBorder` on that node. Title bars are filled with the kind color at full opacity; borders use the same color.

### Schematic symbols (vector, all `ImDrawList`)

Drawn in a 60×40 area centered in the 96×64 body region. Stroke width 2px, color = the kind color. Each symbol is a `static void draw<Name>Symbol(ImDrawList*, ImVec2 center, ImU32 color)` helper. A `drawSchematicSymbol(ImDrawList*, ImVec2 center, NodeKind, ImU32 color)` switch dispatches.

| Kind | Symbol | Construction |
|---|---|---|
| `Generator`     | sine wave              | 2 full cycles, polyline through 16 points sampled from `sin(2π x · 2)` over x ∈ [-1, 1] |
| `Amplifier`     | op-amp triangle        | 3 lines forming a right-pointing triangle; small `+`/`−` on the two input legs |
| `Mixer`         | circle with X          | outer circle (radius 14) + two crossing diagonal lines |
| `Splitter`      | Y-branch               | one input dot on left, vertical line to center, two lines to upper-right and lower-right dots |
| `Adc`           | stair-step             | step-shaped polyline (digital sampling look) |
| `IdealFilter`   | L-C ladder             | 2 small circles + 2 parallel horizontal lines (LC tank) |
| `CoaxCable`     | coax cross-section     | 2 concentric circles (outer radius 16, inner radius 8) |
| `SParam`        | S-matrix box           | rectangle with bold "S" centered |
| `PFB`           | channel stack          | 3 stacked thin rectangles with a small "M" label |
| `GroupCollapsed`| subcircuit bundle      | 3 small filled circles connected by short lines (miniature DAG) |
| `Unknown`       | "?"                    | dimmed question mark; if not drawable, leave the body empty (gray fallback) |

The symbol center is obtained from `ImNodes::GetNodeBodyRect()` if the imnodes version exposes it; otherwise it is computed as `GetNodeScreenSpacePos() + ImVec2(48, 24 + titleBarHeight)`. The fallback path is documented in a `ponytail:` comment in the helper.

### Canvas theme

`setupDarkTheme()` is called once per frame, before `ImNodes::BeginNodeEditor`. It pushes the following color styles (and pops them at frame end, balanced by imnodes' per-frame stack reset):

| `ImNodesCol_*`   | Value                    | Notes |
|---|---|---|
| `CanvasBg`       | `IM_COL32(20, 20, 28, 255)`     | dark navy-gray |
| `GridLine`       | `IM_COL32(45, 45, 60, 100)`     | subtle but visible |
| `Link`           | `IM_COL32(180, 180, 200, 200)`  | muted light gray |
| `NodeBg`         | `IM_COL32(35, 35, 45, 255)`     | dark node body |
| `NodeBorder`     | `IM_COL32(80, 80, 100, 255)`    | default, overridden per-node below |
| `TitleBar`       | `IM_COL32(60, 60, 80, 255)`     | default, overridden per-node below |
| `Pin`            | `IM_COL32(200, 200, 220, 255)`  | clean light pin |
| `PinHovered`     | `IM_COL32(120, 200, 255, 255)`  | cyan highlight |

Per-node overrides are pushed inside the `BeginNode/EndNode` block and popped in the same block (always balanced, even if the body drawing throws):

```cpp
ImNodes::BeginNode(node.node_id);
ImNodes::PushColorStyle(ImNodesCol_TitleBar, themeColor(node.kind));
ImNodes::PushColorStyle(ImNodesCol_NodeBorder, themeColor(node.kind));
ImNodes::BeginNodeTitleBar();
ImGui::TextUnformatted(node.label.c_str());
ImNodes::EndNodeTitleBar();
drawSchematicSymbol(dl, bodyCenter, node.kind, themeColor(node.kind));
// pins unchanged
ImNodes::PopColorStyle();  // NodeBorder
ImNodes::PopColorStyle();  // TitleBar
ImNodes::EndNode();
```

### Pins and links

- **Pins**: default imnodes pin rendering, using the clean light color from the theme. The existing four-color probe system (teal/orange/purple/blue) is **preserved** and is pushed on top of the new pin color for probed pins only.
- **Links**: muted light gray from the theme. No per-link color in v1. Probed links do not glow yet (deferred).

### Node dimensions

The current `ImGui::Dummy(80, 56)` body grows to a 96×64 target by adjusting the title bar text padding (default imnodes behavior). No explicit width setting is needed — the title bar text and pin labels drive the natural width; the 96×64 body sits inside the rendered rect.

## 6. Data flow

### Component creation (one-time per node)

The kind is **not** part of the create path. Each engine's constructor continues to call `m_graph.addNode("EngineName " + id, &m_node, n_in, n_out)` exactly as today. The widget reads the existing `label` and matches the prefix at render time. No creation-path changes anywhere.

### Per-frame render

```
draw_ui()
  → m_graph_widget->draw("Node Editor", ...)
    → setupDarkTheme()                                          [NEW, ~8 PushColorStyle]
    → rebuildSynthMaps()                                        [unchanged]
    → drawGroupBackgrounds()                                    [unchanged]
    → ImNodes::BeginNodeEditor()
      → drawNodes()
          for each visible node:
            kind = nodeKindFromLabel(node.label)                [NEW, label prefix match]
            BeginNode
            PushColorStyle(TitleBar, themeColor(kind))          [NEW]
            PushColorStyle(NodeBorder, themeColor(kind))        [NEW]
            BeginNodeTitleBar / Text(label)
            EndNodeTitleBar
            drawSchematicSymbol(dl, center, kind, color)        [NEW, replaces Dummy()]
            // pins unchanged (incl. probe color push/pop)
            PopColorStyle x2
            EndNode
      → drawGroupCollapsedBlocks()                              [unchanged body, uses GroupCollapsed kind directly]
      → drawLinks()                                             [unchanged]
    → ImNodes::EndNodeEditor()
    → interactions (unchanged)
```

### Label prefix → `NodeKind` table

The `nodeKindFromLabel()` helper is a single switch on the label prefix. Today every engine constructor uses a unique, stable prefix:

| Engine | Label prefix (from constructor) | Maps to |
|---|---|---|
| `SignalGeneratorEngine` | `"Generator "`        | `NodeKind::Generator` |
| `AmplifierEngine`      | `"Amplifier "`        | `NodeKind::Amplifier` |
| `SplitterEngine`       | `"Splitter "`         | `NodeKind::Splitter` |
| `MixerEngine`          | `"Mixer "`            | `NodeKind::Mixer` |
| `SParamEngine`         | `"S-Param "`          | `NodeKind::SParam` |
| `AdcEngine`            | `"ADC "`              | `NodeKind::Adc` |
| `PFBChannelizerEngine` | `"PFB "`              | `NodeKind::PFB` |
| `IdealFilterEngine`    | `"IdealFilter "`      | `NodeKind::IdealFilter` |
| `CoaxCableEngine`      | `"Coax Cable "`       | `NodeKind::CoaxCable` |
| any other / blank      | (no match)            | `NodeKind::Unknown` |

Each engine's constructor sets its label once at construction time and never changes it. The widget matches prefixes in order; first match wins. Hyphen in `"S-Param "` is part of the prefix.

### Group collapsed block

`drawGroupCollapsedBlocks()` already draws a single imnodes node per collapsed group. The widget uses `NodeKind::GroupCollapsed` directly in this function (no label lookup). The `(no external connections)` fallback text remains when `boundary_pins` is empty.

## 7. Error handling

| Failure mode | Behavior |
|---|---|
| Legacy caller of `addNode()` (no kind param, since none exists) | `nodeKindFromLabel()` returns `NodeKind::Unknown` for unrecognised labels → gray title bar, no symbol. Safe. |
| `ComponentRegistry::add<T>()` forgets to set a kind (e.g. new component type) | `if constexpr` switch falls through; kind stays `Unknown`. Visible bug (gray node) but not a crash. |
| `drawSchematicSymbol()` called for an unrecognized enum value | `default:` case draws nothing + emits a one-shot `LOG_WARN("Unknown NodeKind for symbol: %d", (int)kind)`. |
| imnodes version lacks `GetNodeBodyRect()` | Fallback to hardcoded center offset from `GetNodeScreenSpacePos()`. Documented with a `ponytail:` comment. |
| `PushColorStyle` not balanced by `Pop` | Per-frame push is auto-balanced by imnodes' `BeginNodeEditor` reset. Per-node `Push/Pop` lives inside the same `BeginNode/EndNode` block, balanced. |
| Body rect smaller than 60×40 (very small nodes) | Symbols are drawn relative to the center; if the body is too small, the symbol is clipped by imnodes, but no crash. The center is clipped, not the body. |
| Unknown kind reached at draw time | `themeColor()` returns the gray fallback; `drawSchematicSymbol()` does nothing. No crash. |
| Component added before `setupDarkTheme()` is called | Impossible — the widget's `draw()` always calls `setupDarkTheme()` before any node draw. |

Out of scope: runtime theme switching, per-component user colors, custom symbol files, animated probe glow.

## 8. Testing

### Unit tests (Catch2)

Add to `tests/test_node_graph_engine.cpp` (extend existing file rather than create a new one — ponytail says don't add a file until existing is too big):

1. **`nodeKindFromLabel()` round-trip** — for each of the 9 known label prefixes, the helper returns the expected `NodeKind`. Includes the `"S-Param "` hyphen case.
2. **`nodeKindFromLabel()` unknown fallback** — labels with no recognized prefix return `NodeKind::Unknown`. Empty string also returns `Unknown`.
3. **`themeColor()` returns non-zero for every enum value** — covers all 11 kinds including `Unknown`. Confirms the lookup is total.

### Regression

- All 67 existing unit tests + 6 benchmarks must pass (no DSP/engine behavior changes).
- Group rendering tests must pass (collapsed block uses `NodeKind::GroupCollapsed`).

### UI tests (imgui_test_engine)

- Re-run existing UI tests that render the node graph. No new assertions required — the test framework's existing checks (nodes render, links draw, no crashes) cover the new path automatically.

### Manual verification checklist (user, on a real build)

- [ ] Canvas is dark (`#14141c`-ish), grid is visible
- [ ] Each of the 9 component types has a distinct color
- [ ] Each of the 9 component types has a distinct schematic symbol
- [ ] Probes still color pins teal/orange/purple/blue on top of the new theme
- [ ] Group background still renders when expanded
- [ ] Group collapsed block still renders with boundary pins, using indigo color
- [ ] Links still draw between nodes (muted light gray)
- [ ] No regressions: nodes still drag, links still create on pin drag, inspector still updates on selection

## 9. Implementation outline

The actual implementation plan (with file-by-file changes, line estimates, and verification commands) will be written by the `writing-plans` skill after this spec is approved.

Sketch of the diff:

| File | Change | Approx. lines |
|---|---|---|
| `node_graph/include/node_graph_engine.h` | + `NodeKind` enum (no other change) | +15 |
| `node_graph/include/node_graph_widget.h` | + `setupDarkTheme()`, `drawSchematicSymbol()`, `nodeKindFromLabel()`, `themeColor()` declarations | +10 |
| `node_graph/src/node_graph_widget.cpp` | + dark theme, + 10 symbol helpers, + dispatch + label lookup in `drawNodes()`, + per-kind colors | +200 |
| `tests/test_node_graph_engine.cpp` | + 3 new test cases | +50 |

**Total: ~275 lines added.** No deletions. No engine changes. No other module touched.

---

## 10. Open questions

None at design time. The spec is self-contained and ready to plan.

### Self-review notes (post-write)

- **Placeholders**: none.
- **Internal consistency**: Section 4 (data model) now matches Section 6 (data flow) — both agree the engine is unchanged and the kind is derived at render time. Earlier draft mistakenly placed the kind on `GraphNode`; corrected after discovering `addNode()` is called from each engine constructor (9 sites) and would require a wider diff.
- **Scope**: scoped to node_graph module only. The whole change is one widget cpp file plus one new enum, one widget header, and tests.
- **Ambiguity**: the `nodeKindFromLabel()` prefix table is explicit (Section 6). `themeColor()` is total over all enum values. The per-frame push/pop balance is stated.
