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
- `src/node_graph_widget_tooltips.cpp` — pin, node, link, and collapsed-subcircuit hover tooltips (signal summary plus interaction hints, plus the analyzer-measured SNR row on the node-body tooltip)
- `src/schematic_symbols.cpp` — per-`NodeKind` schematic symbol drawing
- `include/rewire.h` / `src/rewire.cpp` — `rewireComponentInputs()`, the shared rewire loop
- `CMakeLists.txt` — `simulator::node_graph_engine` (pure data, no ImGui) and `simulator::node_graph_widget` (ImNodes UI)

## Local Contracts

- **Engine/widget split:** `node_graph_engine` includes no ImGui/ImNodes header and holds only topology, probes, groups, and id counters; `node_graph_widget` owns all ImGui/ImNodes rendering and interaction. The engine never reads, stores, or returns `NodeKind`: it is derived from `GraphNode::label` at render time (`NodeGraphWidget::kindForLabel()`), and `themeColor()` returns a plain `uint32_t` ARGB value (ImU32 bit layout) so the engine keeps no UI dependency. The label→`NodeKind` derivation resolves the **longest** matching label prefix, so overlapping prefixes (e.g. `SPDT Switch` and `SPDT Switch (2:1)`) cannot depend on registry order.
- **The engine stays topology-only:** it stores no physical link policy. `graphLinkAllowed()` lives in `common/graph_link_policy.h`; `CircuitRuntime::connect()` validates live app links, and the app-bound `ProjectSerializer` restores links through the same runtime command. `rewireComponentInputs()` applies the physical policy before binding DSP inputs.
- `void rewireComponentInputs(std::span<IComponentEngine *const> components, const NodeGraphEngine &graph)` is the single rewire loop shared by `CircuitRuntime` and the `test_flow` harness. For every component and input pin `k` it resolves the linked upstream output through `graph.getSourceForInput()`, gates the connection with `graphLinkAllowed()`, and binds `node().inputs[k]` to `&source.node->outputs[source.output_index]`, or to `nullptr` when the pin is unlinked, physically disallowed, or the resolved port index is out of range.
- Groups (`Group`, `GroupBoundaryPin` from `common/include/group.h`) remain a visual layer stored in `NodeGraphEngine`; `GraphEditorActions` is the app-side mutation adapter, while `NodeGraphWidget` only renders and requests edits.
- Node, pin, and link IDs are monotonic and reset by `CircuitRuntime`; group and boundary-pin IDs are reset by `GraphEditorActions`. `.rfsim` persists only the component-ID counter; group IDs are freshly allocated on load.
- `CircuitRuntime::removeComponent()` removes an engine through its owned registry and rewires synchronously before returning, so no surviving component holds a dangling `Spectrum*` into the destroyed engine's `SignalNode` (issue #37).
- `topologicalOrder()` counts only links whose start and end pins both resolve to graph nodes;
  stale or dangling links are not treated as graph edges or cycles.
- `NodeGraphEngine::canAddLink()` — backed by `inputHasLink()` and `wouldCreateCycle()` — is the editing policy for link creation: it rejects a second link into an occupied input pin and any link that would close a directed cycle. `CircuitRuntime::connect()` applies this policy and `graphLinkAllowed()` before committing; `addLink()` itself stays permissive for low-level callers that intentionally build multi-source inputs.
- `NodeGraphWidget` reads a const graph and sends topology/editor requests through `NodeGraphWidgetActions`; it does not commit links or mutate probes/groups itself. The graph engine takes no app-level callbacks.
- `NodeGraphWidget` caches each node's pan-independent grid position: `drawNodes()` refreshes it for
  visible nodes and `captureGridPositions()` snapshots every node after a project load. Collapsed
  group members stop being drawn and are dropped from the imnodes pool, so
  `drawGroupCollapsedBlocks()` places each block from that cache (never from the per-frame
  screen-position map). `drawLinks()` treats "both endpoints hidden" as an internal link only when
  both belong to the *same* collapsed group; a link between two different collapsed groups is drawn
  through both groups' synthesized boundary pins.
- App placements use `NodeGraphWidget::nodeBoundsExcluding()` and `setNodeEditorSpacePosition()` so graph enumeration, position caches, and ImNodes dimensions stay inside the widget. Bounds exclude supplied current-batch IDs. Visible component nodes cache measured dimensions after `EndNode`; hidden/unregistered components use cached positions/dimensions or a 260×160 estimate, and missing positions default to the origin. No invalid ImNodes dimension lookup is made.
- `drawGroupCollapsedBlocks()` caches each rendered synthetic block's actual editor-space bounds after `EndNode`; `nodeBoundsExcluding()` unions those bounds only for groups still collapsed and recorded as rendered. It rebuilds the group cache each draw and `clearPositionCache()` clears it, while current-group checks prevent expanded or removed IDs from contributing.
- `NodeGraphWidgetActions` is the widget's command boundary for link/component/probe/group/selection edits. `onNodeMoved` and `onNodeHover` remain app callbacks for notification/tooltip data; no widget path mutates the graph or editor records directly.
- Hover tooltips are the discoverability surface for the editor's gestures, so the chords they print are the ones `NodeGraphWidget::handleProbeClick()` implements — **Ctrl+click adds a probe** (the Spectrum Analyzer plots that signal), **Shift+click removes it** — and Help/Tutorial wording must match. A tooltip is suppressed while a mouse button is dragging and while a pin owns the hover, so a pin hover never stacks the node tooltip on top of the pin tooltip. The node probe hint reports the probe slot already held by the node's *first* output, the port a node-body Ctrl+click targets.
- The probe hints are gated on the pin `handleProbeClick()` would actually target. For a collapsed block that pin is `NodeGraphEngine::firstOutputBoundaryPin()` — the handler reads it too, so the two cannot drift — and consequently a group with no cross-boundary output link prints no probe hint, while a block whose boundary pin already holds a probe reports its slot instead of advertising a probe (input-only boundary links are not a target).
- **Node-body hover carries the SNR row:** the widget prints the app callback's summary, then `SNR: <value> dB` or `SNR: --`, then the interaction hints. Measurement policy stays outside the widget: the app uses analyzer RBW for ordinary outputs and the PFB's integrated active-channel noise for PFB output 0. The widget has no analyzer/PFB measurement dependency. Empty summaries suppress the tooltip; pin, link, and collapsed-subcircuit tooltips remain unchanged.

## Work Guidance

- Add a component type = one `NodeKind` enumerator plus a `themeColor()` case and a `schematic_symbols.cpp` drawing case; the app registers the label prefix through `NodeGraphWidget::registerNodeKind()` from the `ComponentTypeRegistry` row's `label_prefix` + `kind`.
- Keep `rewireComponentInputs()` the only rewire implementation; do not add a second rewire loop in `app/` or `test_flow/`.

## Verification

- `ctest --test-dir build` must pass with zero failures.
- `tests/test_node_graph_engine.cpp` covers add/remove, link topology, probes, id counters, groups (`firstOutputBoundaryPin()` with no link, an input-only boundary link, an output boundary link, and after the link is removed), and `themeColor()`; `tests/test_issue87_flow.cpp` covers the shared link policy and rewire behavior.
- `test_engine/ui_tests.cpp::hover_tooltips_node_link_subcircuit` covers the hover tooltips end-to-end: a node body, a link, and a collapsed subcircuit block must each raise a tooltip window (any `ImGuiWindowFlags_Tooltip` window, read through `WasActive` because the test engine runs between frames), and an empty-canvas point must raise none; the collapsed-block case deliberately uses a group with no output boundary pin (asserted). Before hovering, the case also calls the app's `onNodeHover` for the seeded generator node and asserts the data the node tooltip renders — a non-empty summary and a populated `snr_dB`. That window-existence check does not read tooltip text — ImGui registers text items with id 0, so the test engine cannot query them — which means the hint text itself is covered by `firstOutputBoundaryPin()` at the engine level rather than by this test, and the SNR *value* the row prints is asserted against the analyzer by the standalone `tests/test_node_hover_snr.cpp` (which stops at the callback's `NodeHoverInfo`); no test asserts the rendered tooltip string, so the widget's `SNR: %.1f dB` / `SNR: --` formatting is covered by source review of `showNodeHoverTooltips()`, not by an assertion.

## Child DOX Index

No child docs. `node_graph/` is a flat two-target module.
