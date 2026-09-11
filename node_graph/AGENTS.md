# node_graph — AGENTS.md

## Purpose

Own the node-graph model and its editor UI, plus the shared DSP rewire pass: the topology-only
`NodeGraphEngine` (nodes, links, probes, groups), the ImNodes-based `NodeGraphWidget` (rendering,
interaction, schematic symbols), and `rewireComponentInputs()`.

## Ownership

- `include/node_graph_engine.h` — `GraphNode`, `GraphLink`, `SignalSource`, `NodeGraphEngine`, and the view-layer `NodeKind` enum with `themeColor()`
- `src/node_graph_engine.cpp` — topology, probe, and group implementation
- `include/node_graph_widget.h` — `NodeGraphWidget`
- `src/node_graph_widget.cpp` — node/link rendering, canvas menu, link creation/deletion, hover
- `src/node_graph_widget_groups.cpp` — group backgrounds, collapsed group blocks, rubber-band selection, group rename
- `src/node_graph_widget_tooltips.cpp` — pin and node hover tooltips
- `src/schematic_symbols.cpp` — per-`NodeKind` schematic symbol drawing
- `include/rewire.h` / `src/rewire.cpp` — `rewireComponentInputs()`, the shared rewire loop
- `CMakeLists.txt` — `simulator::node_graph_engine` (pure data, no ImGui) and `simulator::node_graph_widget` (ImNodes UI)

## Local Contracts

- **Engine/widget split:** `node_graph_engine` includes no ImGui/ImNodes header and holds only topology, probes, groups, and id counters; `node_graph_widget` owns all ImGui/ImNodes rendering and interaction. The engine never reads, stores, or returns `NodeKind`: it is derived from `GraphNode::label` at render time (`NodeGraphWidget::kindForLabel()`), and `themeColor()` returns a plain `uint32_t` ARGB value (ImU32 bit layout) so the engine keeps no UI dependency.
- **The engine stays topology-only:** it holds no physical link rule. `graphLinkAllowed()` lives in `common/graph_link_policy.h` and reaches creation/load paths through `NodeGraphWidget::onLinkCreating`; the DSP pass that applies it is `rewireComponentInputs()`.
- `void rewireComponentInputs(std::span<IComponentEngine *const> components, const NodeGraphEngine &graph)` is the single rewire loop shared by the app and the `test_flow` harness. For every component and input pin `k` it resolves the linked upstream output through `graph.getSourceForInput()`, gates the connection with `graphLinkAllowed()`, and binds `node().inputs[k]` to `&source.node->outputs[source.output_index]`, or to `nullptr` when the pin is unlinked, physically disallowed, or the resolved port index is out of range. `RfSimulatorApp::rewireInputs()` delegates to it, so the GUI and the harness cannot drift on what a circuit is wired to.
- Groups (`Group`, `GroupBoundaryPin` from `common/include/group.h`) are a visual layer: `NodeGraphEngine` owns them and `NodeGraphWidget` renders/collapses them; no DSP engine consumes them.
- Node, pin, link, group, and boundary-pin id counters are monotonic and restored on project load through `setNextIds()`, `setNextGroupId()`, and `setNextBoundaryPinId()`.
- Node removal must re-run `rewireComponentInputs()` synchronously with `ComponentRegistry::remove()` so no surviving component holds a dangling `Spectrum*` into the destroyed engine's `SignalNode` (issue #37).
- `topologicalOrder()` counts only links whose start and end pins both resolve to graph nodes;
  stale or dangling links are not treated as graph edges or cycles.
- Widget callbacks (`onNodeMoved`, `onRemoveNode`, `onDuplicateNode`, `onLinkChanged`, `onLinkCreating`, `onNodeHover`) are the only channel from widget to app; the engine itself takes no app-level callbacks.

## Work Guidance

- Add a component type = one `NodeKind` enumerator plus a `themeColor()` case and a `schematic_symbols.cpp` drawing case; the app registers the label prefix through `NodeGraphWidget::registerNodeKind()` from the `ComponentTypeRegistry` row's `label_prefix` + `kind`.
- Keep `rewireComponentInputs()` the only rewire implementation; do not add a second rewire loop in `app/` or `test_flow/`.

## Verification

- `ctest --test-dir build` must pass with zero failures.
- `tests/test_node_graph_engine.cpp` covers add/remove, link topology, probes, id counters, groups, and `themeColor()`; `tests/test_issue87_flow.cpp` covers the shared link policy and rewire behavior.

## Child DOX Index

No child docs. `node_graph/` is a flat two-target module.
