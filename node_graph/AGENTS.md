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
- `include/node_layout.h` / `src/node_layout.cpp` — pure align and distribute geometry (no ImGui); compiled into `node_graph_engine`
- `src/node_graph_widget_layout.cpp` — the widget's align and distribute selection read, collapsed-subcircuit refusal, and refusal message
- `src/node_graph_widget_tooltips.cpp` — pin, node, link, and collapsed-subcircuit hover tooltips (signal summary plus interaction hints, plus the analyzer-measured SNR row on the node-body tooltip)
- `src/schematic_symbols.cpp` — per-`NodeKind` schematic symbol drawing
- `include/rewire.h` / `src/rewire.cpp` — `rewireComponentInputs()`, the shared rewire loop
- `CMakeLists.txt` — `simulator::node_graph_engine` (pure data, no ImGui) and `simulator::node_graph_widget` (ImNodes UI)

## Local Contracts

- **Engine/widget split:** `node_graph_engine` includes no ImGui/ImNodes header and holds only topology, probes, groups, and id counters; `node_graph_widget` owns all ImGui/ImNodes rendering and interaction. The engine never reads, stores, or returns `NodeKind`: it is derived from `GraphNode::label` at render time (`NodeGraphWidget::kindForLabel()`). `themeColor()` returns a plain `uint32_t` in ImGui's default `IM_COL32` layout (0xAABBGGRR, red in the low byte, built by `rgbToImU32()`), so the engine keeps no UI dependency and the widget casts it to `ImU32` without swapping channels. The label→`NodeKind` derivation resolves the **longest** matching label prefix, so overlapping prefixes (e.g. `SPDT Switch` and `SPDT Switch (2:1)`) cannot depend on registry order.
- **Node colour and symbol placement:** ImNodes copies `TitleBar` and `NodeOutline` at `BeginNode`, so the widget pushes the per-type colour before `BeginNode` and pops after `EndNode` (plain nodes in `drawNodes`, collapsed blocks in `drawGroupCollapsedBlocks`). Schematic symbols are centred on the real drawn extent, never on a hard-coded title height. A plain node's body is the 96×64 `Dummy`, centred on the cursor taken just before it (after the title bar and part-number line). A collapsed block has no reserved body rect: it is content-sized (title, then pin rows or the 120×30 fallback spacer with its text, then the Expand button). Its symbol is drawn after `EndNode`, centred horizontally on the block's measured rect and vertically in the free slot between the title bar and the Expand button (the pin rows, or the fallback spacer), so it never overlaps Expand or the fallback text.
- **Plain-node pin rows and title width:** a plain node puts input *r* on the left edge and output *r* on the right edge of the same row, so it is `max(inputs, outputs)` pin rows tall, not `inputs + outputs`. Output labels are right-aligned to the body edge and never overlap their row's input label (`drawNodes`). The title is trimmed with `...` to the 96 px body width (`fitTitleToWidth()`, `node_graph_widget.cpp`), so a node is body width plus node padding unless a pin label is wider. Do not pad nodes to a fixed pin-row count.
- **Schematic symbol box:** every `NodeKind` glyph in `schematic_symbols.cpp` draws inside one 48×32 design box centred on the 96×64 body, with 2 px strokes. Terminal leads run to the box edge at x = ±24. Labels (ATT, M) share one slot below the box, at +17 px from the centre. Equalizer, ADC, and Mixer keep a 20 px minimum extent. The Unknown kind draws a dashed frame with a vector "?" at half alpha. `drawSchematicSymbol()` is public so the geometry test can measure the drawn vertices. It keeps the signature `(ImDrawList*, ImVec2 center, NodeKind, ImU32)`.
- **The engine stays topology-only:** it stores no physical link policy. `graphLinkAllowed()` lives in `common/graph_link_policy.h`; `CircuitRuntime::connect()` validates live app links, and the app-bound `ProjectSerializer` restores links through the same runtime command. `rewireComponentInputs()` applies the physical policy before binding DSP inputs.
- `void rewireComponentInputs(std::span<IComponentEngine *const> components, const NodeGraphEngine &graph)` is the single rewire loop shared by `CircuitRuntime` and the `test_flow` harness. It builds source-output, linked-input, and source-component lookup maps once per call, preserving the first valid link and first graph output pin selected by `graph.getSourceForInput()` plus the first span component for a shared `SignalNode`. For every component and input pin `k` it resolves the linked upstream output, gates the connection with `graphLinkAllowed()`, and binds `node().inputs[k]` to `&source.node->outputs[source.output_index]`, or to `nullptr` when the pin is unlinked, physically disallowed, or the resolved port index is out of range.
- Groups (`Group`, `GroupBoundaryPin` from `common/include/group.h`) remain a visual layer stored in `NodeGraphEngine`; `GraphEditorActions` is the app-side mutation adapter, while `NodeGraphWidget` only renders and requests edits.
- Node, pin, and link IDs are monotonic and reset by `CircuitRuntime`; group and boundary-pin IDs are reset by `GraphEditorActions`. `.rfsim` persists only the component-ID counter; group IDs are freshly allocated on load.
- `CircuitRuntime::removeComponent()` removes an engine through its owned registry and rewires synchronously before returning, so no surviving component holds a dangling `Spectrum*` into the destroyed engine's `SignalNode`.
- `NodeGraphEngine::remapNodeReferences()` moves group membership and probe slots from a retired node onto its successor. It does not check ports; `EditorCommands::swapLibraryPart()` refuses port mismatches before calling it. `NodeGraphWidget::selectNode()` makes one node the ImNodes selection and needs the node in the pool, which `setNodeEditorSpacePosition()` guarantees.
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
- `setNodeEditorSpacePosition()` compares requested editor coordinates after subtracting the current pan, using a tolerance scaled by the pan and requested coordinates. It returns whether the stored grid origin changed; an unchanged origin is still registered and cached when a visible node has not reached `BeginNode`.
- `drawGroupCollapsedBlocks()` caches each rendered synthetic block's actual editor-space bounds after `EndNode`; `nodeBoundsExcluding()` unions those bounds only for groups still collapsed and recorded as rendered. It rebuilds the group cache each draw and `clearPositionCache()` clears it, while current-group checks prevent expanded or removed IDs from contributing.
- `editorPositionForSave()` supplies project-save, checkpoint, and agent positions. It reads
  directly from ImNodes only for nodes with initialized pool positions that are not hidden;
  unregistered nodes and collapsed-group members use the cached grid position, or the origin,
  plus current pan. Editor-space coordinates are grid space plus pan.
- `NodeGraphWidgetActions` is the widget's command boundary for link/component/probe/group/selection edits. `onNodeMoved` and `onNodeHover` remain app callbacks for notification/tooltip data; no widget path mutates the graph or editor records directly.
- **Align and distribute:** `node_layout` computes new editor-space positions from sizes the widget measured and never touches ImNodes. The widget sends every selected node in one `NodeGraphWidgetActions::moveNodes` call, so `EditorCommands::moveNodes()` sees the whole selection. A collapsed block, or a member of a collapsed group, in the selection is refused before anything moves; the refusal text shows in a modal. Align uses the selection's bounding box; distribute keeps the outer two nodes fixed and spaces the rest with equal edge gaps. The right-clicked node must be in the selection for the Align and Distribute entries to be enabled.
- **Stale selection entries:** ImNodes keeps a selection entry after its node leaves the pool (a node hidden by a collapse, or removed), reading back as an invalid id, and reuses the freed slot later. `NodeGraphWidget::liveSelection()` drops every entry that is not a live component node or a collapsed block, so layout and the Align/Distribute count never see them. The widget records the collapsed subcircuits that had a selected member at the start of each draw (`rememberCollapsedSelection()`), so a refusal can still name the subcircuit after ImNodes has pruned its members. A stale entry alone does not refuse: an expanded block's entry is ignored.
- Hover tooltips are the discoverability surface for the editor's gestures, so the chords they print are the ones `NodeGraphWidget::handleProbeClick()` implements — **Ctrl+click adds a probe** (the Spectrum Analyzer plots that signal), **Shift+click removes it** — and Help/Tutorial wording must match. A tooltip is suppressed while a mouse button is dragging and while a pin owns the hover, so a pin hover never stacks the node tooltip on top of the pin tooltip. The node probe hint reports the probe slot already held by the node's *first* output, the port a node-body Ctrl+click targets.
- The probe hints are gated on the pin `handleProbeClick()` would actually target. For a collapsed block that pin is `NodeGraphEngine::firstOutputBoundaryPin()` — the handler reads it too, so the two cannot drift — and consequently a group with no cross-boundary output link prints no probe hint, while a block whose boundary pin already holds a probe reports its slot instead of advertising a probe (input-only boundary links are not a target).
- **Node-body hover carries the SNR row:** the widget prints the app callback's summary, then `SNR: <value> dB` or `SNR: --`, then the interaction hints. Measurement policy stays outside the widget: the app uses analyzer RBW for ordinary outputs and the PFB's integrated active-channel noise for PFB output 0. The widget has no analyzer/PFB measurement dependency. Empty summaries suppress the tooltip; pin, link, and collapsed-subcircuit tooltips remain unchanged.

## Work Guidance

- Add a component type = one `NodeKind` enumerator plus a `themeColor()` case and a `schematic_symbols.cpp` drawing case; the app registers the label prefix through `NodeGraphWidget::registerNodeKind()` from the `ComponentTypeRegistry` row's `label_prefix` + `kind`.
- Keep `rewireComponentInputs()` the only rewire implementation; do not add a second rewire loop in `app/` or `test_flow/`.

## Verification

- `ctest --test-dir build` must pass with zero failures.
- `tests/test_node_graph_engine.cpp` covers graph add/remove, link topology, probes, id counters, groups, and `themeColor()`; `tests/test_node_graph_rewire.cpp` characterizes empty, unlinked, and graph-only source bindings plus first-link precedence and dangling-link skipping, and benchmarks two-input chains. `tests/test_issue87_flow.cpp` covers shared link policy and flow behavior.
- `test_engine/ui_tests.cpp::hover_tooltips_node_link_subcircuit`: node-body, link, and collapsed-subcircuit hovers each raise a tooltip window, and an empty-canvas point raises none. The node-body case asserts the `onNodeHover` summary and `snr_dB`. The test checks window existence only, not tooltip text. The SNR value is asserted in `tests/test_node_hover_snr.cpp` up to the callback's `NodeHoverInfo`. No test asserts the rendered `SNR:` string.
- `test_engine/ui_tests.cpp::node_one_in_one_out_has_one_pin_row` and `node_long_title_clamped_to_body_width`: a 1-in/1-out Amplifier is the same height as a 0-in/1-out Generator, and a long-label SPDT 2:1 node stays within body width plus node padding (112 px).
- `test_schematic_symbols` (`tests/test_schematic_symbols.cpp`): every glyph's drawn vertices stay in the 48×32 box (1 px AA slack, label text only in the label slot), the Equalizer/ADC/Mixer 20 px floor holds, and the Unknown glyph differs from every other symbol.
- `tests/test_node_layout.cpp` covers align and distribute for every mode, with two and three or more nodes, and the no-op cases. `tests/test_editor_commands.cpp` `[layout]` cases cover `moveNodes`, the collapsed refusal, one-step undo, and the undo invalidation. `test_engine/ui_tests.cpp::node_align_menu` drives the right-click menu.

## Child DOX Index

No child docs. `node_graph/` is a flat two-target module.
