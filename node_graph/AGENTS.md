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
- **The engine stays topology-only:** it holds no physical link rule. `graphLinkAllowed()` lives in `common/graph_link_policy.h` and reaches creation/load paths through `NodeGraphWidget::onLinkCreating`; the DSP pass that applies it is `rewireComponentInputs()`.
- `void rewireComponentInputs(std::span<IComponentEngine *const> components, const NodeGraphEngine &graph)` is the single rewire loop shared by the app and the `test_flow` harness. For every component and input pin `k` it resolves the linked upstream output through `graph.getSourceForInput()`, gates the connection with `graphLinkAllowed()`, and binds `node().inputs[k]` to `&source.node->outputs[source.output_index]`, or to `nullptr` when the pin is unlinked, physically disallowed, or the resolved port index is out of range. `RfSimulatorApp::rewireInputs()` delegates to it, so the GUI and the harness cannot drift on what a circuit is wired to.
- Groups (`Group`, `GroupBoundaryPin` from `common/include/group.h`) are a visual layer: `NodeGraphEngine` owns them and `NodeGraphWidget` renders/collapses them; no DSP engine consumes them.
- Node, pin, link, group, and boundary-pin id counters are monotonic and restored on project load through `setNextIds()`, `setNextGroupId()`, and `setNextBoundaryPinId()`.
- Node removal must re-run `rewireComponentInputs()` synchronously with `ComponentRegistry::remove()` so no surviving component holds a dangling `Spectrum*` into the destroyed engine's `SignalNode` (issue #37).
- `topologicalOrder()` counts only links whose start and end pins both resolve to graph nodes;
  stale or dangling links are not treated as graph edges or cycles.
- `NodeGraphEngine::canAddLink()` — backed by `inputHasLink()` and `wouldCreateCycle()` — is the
  editing policy for link creation: it rejects a second link into an occupied input pin and any
  link that would close a directed cycle. `addLink()` itself stays permissive so callers that
  intentionally build multi-source inputs keep working; the app's `onLinkCreating` and the project
  loader both gate on `canAddLink()`, so the GUI cannot build a circuit the `test_flow` harness
  rejects.
- `NodeGraphWidget` caches each node's pan-independent grid position: `drawNodes()` refreshes it for
  visible nodes and `captureGridPositions()` snapshots every node after a project load. Collapsed
  group members stop being drawn and are dropped from the imnodes pool, so
  `drawGroupCollapsedBlocks()` places each block from that cache (never from the per-frame
  screen-position map). `drawLinks()` treats "both endpoints hidden" as an internal link only when
  both belong to the *same* collapsed group; a link between two different collapsed groups is drawn
  through both groups' synthesized boundary pins.
- Widget callbacks (`onNodeMoved`, `onRemoveNode`, `onDuplicateNode`, `onLinkChanged`, `onLinkCreating`, `onNodeHover`) are the only channel from widget to app; the engine itself takes no app-level callbacks.
- Hover tooltips are the discoverability surface for the editor's gestures, so the chords they print are the ones `NodeGraphWidget::handleProbeClick()` implements — **Ctrl+click adds a probe** (the Spectrum Analyzer plots that signal), **Shift+click removes it** — and Help/Tutorial wording must match. A tooltip is suppressed while a mouse button is dragging and while a pin owns the hover, so a pin hover never stacks the node tooltip on top of the pin tooltip. The node probe hint reports the probe slot already held by the node's *first* output, the port a node-body Ctrl+click targets.
- The probe hints are gated on the pin `handleProbeClick()` would actually target. For a collapsed block that pin is `NodeGraphEngine::firstOutputBoundaryPin()` — the handler reads it too, so the two cannot drift — and consequently a group with no cross-boundary output link prints no probe hint, while a block whose boundary pin already holds a probe reports its slot instead of advertising a probe (input-only boundary links are not a target).
- **Node-body hover carries the SNR row:** `NodeGraphWidget::onNodeHover` returns `NodeHoverInfo { summary, snr_dB }` (not a bare string), and the node-body tooltip prints the app's summary, then exactly one row — `SNR: <value> dB` when the optional is set, `SNR: --` when it is empty — and then the interaction hints above. The widget renders the number but never measures it: the analyzer's binning/RBW/grid rules and the port choice live in the app's callback (`app/AGENTS.md`), so the widget keeps no analyzer dependency. An empty `summary` still suppresses the whole node tooltip, as before. Pin, link, and collapsed-subcircuit tooltips are unchanged by this: they show their own text and never a node summary or an SNR row.

## Work Guidance

- Add a component type = one `NodeKind` enumerator plus a `themeColor()` case and a `schematic_symbols.cpp` drawing case; the app registers the label prefix through `NodeGraphWidget::registerNodeKind()` from the `ComponentTypeRegistry` row's `label_prefix` + `kind`.
- Keep `rewireComponentInputs()` the only rewire implementation; do not add a second rewire loop in `app/` or `test_flow/`.

## Verification

- `ctest --test-dir build` must pass with zero failures.
- `tests/test_node_graph_engine.cpp` covers add/remove, link topology, probes, id counters, groups (`firstOutputBoundaryPin()` with no link, an input-only boundary link, an output boundary link, and after the link is removed), and `themeColor()`; `tests/test_issue87_flow.cpp` covers the shared link policy and rewire behavior.
- `test_engine/ui_tests.cpp::hover_tooltips_node_link_subcircuit` covers the hover tooltips end-to-end: a node body, a link, and a collapsed subcircuit block must each raise a tooltip window (any `ImGuiWindowFlags_Tooltip` window, read through `WasActive` because the test engine runs between frames), and an empty-canvas point must raise none; the collapsed-block case deliberately uses a group with no output boundary pin (asserted). Before hovering, the case also calls the app's `onNodeHover` for the seeded generator node and asserts the data the node tooltip renders — a non-empty summary and a populated `snr_dB`. That window-existence check does not read tooltip text — ImGui registers text items with id 0, so the test engine cannot query them — which means the hint text itself is covered by `firstOutputBoundaryPin()` at the engine level rather than by this test, and the SNR row's *numeric* content is owned by the standalone `tests/test_node_hover_snr.cpp`.

## Child DOX Index

No child docs. `node_graph/` is a flat two-target module.
