# Guided tutorial

The tutorial walks you through the basic workflow — add a component, connect it, configure it, probe a node, navigate the graph — then the panels that check, budget, and automate a design. Start it from **Help > Tutorial**, or accept it on first launch.

The tutorial has sixteen steps. Each step has **Back**, **Next**, **Skip**, and **Exit** buttons. The last step shows **Finish** in place of **Next**. Starting it opens the panels the core steps highlight, including the Component Library; the later steps say which **View** menu item opens the panel they cover.

## Welcome

This short walkthrough covers adding a component, connecting it, configuring it, and reading its output, then the panels that check, budget, and automate a design. Click Next to begin.

## Add a Component

Click a component in the highlighted Component Library panel to insert it into the canvas. You can also right-click empty canvas in the Node Editor to pick from a context menu.

## Connect Pins

In the Node Editor, click an output pin and drag to an input pin, then release to create a link. To remove one, select the link and press Delete. Ctrl+Z brings it back.

## Configure Parameters

Left-click a node to load it into the Properties panel, then adjust a parameter such as gain or frequency. Changes take effect immediately. To remove the selected component, click **Remove** in the Properties header; Ctrl+Z restores it.

## View Results

Ctrl+click an output pin to probe it — the Spectrum Analyzer plots that node's live signal. Up to four probes can be active, each with its own trace color.
Noise traces show power integrated over the selected RBW. Changing RBW, VBW, or Avg Count clears Max Hold, Min Hold, and Video Average history.

## Navigate the Graph

Pan the canvas by middle-click dragging. Rubber-band select, or Ctrl+click, to pick several nodes at once — Ctrl+click on a node body also probes that node's first output. Press Delete to remove them. Delete works only while the Node Editor has focus, so it does nothing while you are typing in a field.

## Undo and Redo

Press Ctrl+Z to undo your last change and Ctrl+Shift+Z or Ctrl+Y to redo it, up to 100 steps back. **Edit** names the change each will act on next. Undo covers component adds and removals, links, parameter edits, probes, groups, node layout, library-part swaps, receiver requirements, RF budget edits, network analyzer points, and agent tool calls.

## Align and Snap

With two or more selected nodes, right-click one of them and choose **Align** (Left, Right, Top, Bottom, Centre on Vertical Line, Centre on Horizontal Line) or **Distribute** (Horizontally, Vertically) to tidy the layout. **Snap selection to grid** rounds the selected nodes onto the 24 px grid. Turn on **View > Snap to Grid** to make dragged nodes snap too; it is off by default and lasts for the session.

## Author Library Parts

**New Component...** at the top of the Component Library opens a form that defines a reusable part — parameters, ranges, and data files such as Touchstone models — and saves it to the project or your global library. To pick up library changes made while the simulator is open, click **Refresh**. A user-defined part's **Edit** button reopens it in the same form.

## Swap a Placed Part

Select a library part's node, open the **Part** list under the component's name in the Properties panel, and pick another part. The new part keeps the node's position, links, probes, and group membership, and starts from its own library parameters. If a connected port, probe, or instrument point has no matching port on the new part, the swap is refused and the reason appears under the list.

## Set Receiver Requirements

Open **View > Receiver Requirements** if it is not already on screen — View items toggle, and panel visibility persists between runs. It checks the circuit against named requirement modes — each has its own band, gain, noise-figure, output-power, and IIP3 limits, plus its own input level, and **Add mode** defines a new one. Out-of-band suppression points set how far below the in-band gain the cascade must fall outside the band, and an SNR minimum with its bandwidth judges the output signal-to-noise.

## Budget a Receiver

Open **View > RF Budget** if it is not already on screen. The budget appears as a signal chain reading left to right: the budget input on the left, one compact block per stage — amplifier, attenuator, filter, coax, equalizer, splitter, or RF switch — each block taking a library part or edited settings and naming what feeds its input ports, and the outputs on the right, with **Add Stage** and **Add Output** building the chain. **Budget this mode** gives the selected mode a budget record so it is predicted even with no overrides; the table shows each stage's gain, noise figure, and cumulative Friis and IIP3 with a verdict per output.

## Automate with Test Flow

Open **View > Test Flow** if it is not already on screen. The panel authors and runs parameter sweeps against the live circuit: **New from Circuit** scaffolds a flow you can edit — pick a component and one of its real parameter keys, seed the values from the circuit, then add conditions and measurements. **Run** applies the sweep row by row, **Export...** saves the results as JSON, and **Save Flow...** writes the flow for a later **Reload**.

## Drive it from an Agent

Open **View > Agent** if it is not already on screen, then enable **Agent Server** and point a client such as Claude Code at the `rf-sim-mcp` bridge that ships beside the simulator. An agent can read and edit the circuit, run measurements, sweeps, and test flows, and author library parts. Calls that change circuit, requirements, or budget state leave a checkpoint you can revert to after confirmation; read-only calls and library creation do not.

## Move the Utility Space

The auxiliary panels open together in the Utility Space beside the main window. If the group is docked into the main window, choose **Window > Utility Space to its own window** to move it back out without dragging — the command also works while the group floats inside the main window. The new position is saved with the layout, and the group stays dockable.

## Wrap Up

The panels stay under the **View** menu, **Help > How to Use** keeps the full reference, and **Help > Tutorial** restarts this walkthrough any time. Click **Finish** to mark it complete.
