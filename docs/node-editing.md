# Node editing

The Node Editor is the canvas where the signal chain lives. Place components, wire their pins, and arrange the layout.

## Navigate

- **Pan**: middle-click drag.
- **Zoom**: mouse wheel.
- **Select**: left-click a node. **Ctrl+click** adds to the selection.
- **Rubber band**: **Shift+left-drag** on empty canvas draws a box; on release it collects every ungrouped node inside.

## Add a component

Right-click empty canvas and pick a component from the **Add** list, or click a part in the [Component Library](component-library.md) panel. The new component appears where you clicked (canvas menu) or at the canvas origin (library) — drag it from there.

## Connect pins

Click an output pin, drag to an input pin, and release to create a link. Each input takes one link, and links cannot close a cycle; both refusals are silent. To disconnect, right-click the link and choose **Remove**, or select the link and press **Delete**.

## Delete

**Delete** removes the selected links first, then the selected nodes, as one undo step. It acts only while the Node Editor has focus, so it never fires while you are typing in a field.

## Probe

**Ctrl+click** an output pin to show that node's signal in the [Spectrum Analyzer](spectrum-analyzer.md); **Ctrl+click** a node body probes its first output. **Shift+click** a probed pin removes the probe.

## Subcircuits

Shift+left-drag a box around **two or more ungrouped nodes** and release: the **CreateSubcircuit** dialog opens, listing the members and asking for a **Name**. **Create** collapses the members into one block.

- A **collapsed** subcircuit is one block with boundary pins — one per link that crosses the group edge, labelled with the member node and pin. Links that stay inside are hidden. With no cross-boundary links the block shows `(no external connections)`. The block sits at the centre of its members; drag the members to move it.
- **Expand** (the button in the block body) spreads the members back out inside a titled rectangle; the **▼** button in the title bar collapses it again.
- Right-click a collapsed block to **Expand**/**Collapse**, **Rename**, or **Ungroup** it.
- Groups cannot nest, and membership is fixed at creation — ungroup and recreate to change it. Deleting members until fewer than two remain dissolves the group on its own.

Groups are for organizing the canvas; they do not change how the signal is processed.

## Align and distribute

With **two or more** nodes selected, right-click one of the selected nodes and choose from **Align**:

- **Left**, **Right**, **Top**, **Bottom** — edges to the selection's bounding box on that side.
- **Centre on Vertical Line (same x)**, **Centre on Horizontal Line (same y)** — centres on one line through the box centre.

**Distribute > Horizontally** or **Vertically** keeps the outer two nodes fixed and spaces the rest with equal gaps between edges. It needs three or more nodes to move anything.

A selection that touches a collapsed subcircuit is refused — nothing moves, and the message tells you to expand that subcircuit first.

## Snap to grid

- Right-click a selected node and choose **Snap selection to grid** to round the selected nodes onto the 24 px grid.
- **View > Snap to Grid** makes dragged nodes snap too. It is off by default and lasts for the session.

Every node's title bar and outline use its component type's colour, so the kinds are distinguishable at a glance.
