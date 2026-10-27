# Guided tutorial

The tutorial walks you through the basic workflow: add a component, connect it, configure it, probe a node, and navigate the graph. Start it from **Help > Tutorial**, or accept it on first launch.

The tutorial has six steps. Each step has **Back**, **Next**, **Skip**, and **Exit** buttons. The last step shows **Finish** in place of **Next**.

## Welcome

This short walkthrough covers adding a component, connecting it, configuring it, and reading its output. Click Next to begin.

## Add a Component

Click a component in the highlighted Component Library panel to insert it into the canvas. To pick
up library changes made while the simulator is open, click **Refresh** at the top of the panel. You
can also right-click empty canvas in the Node Editor to pick from a context menu.

## Connect Pins

In the Node Editor, click an output pin and drag to an input pin, then release to create a link. To remove one, select the link and press Delete.

## Configure Parameters

Left-click a node to load it into the Properties panel, then adjust a parameter such as gain or frequency. Changes take effect immediately. To swap a library part for another one, open the **Part** list under the component's name and pick a part. The new part keeps the node's position, links, probes, and group membership, and starts from its own library parameters. If a connected port, probe, or instrument point has no matching port on the new part, the swap is refused and the reason appears under the list.

## View Results

Ctrl+click an output pin to probe it — the Spectrum Analyzer plots that node's live signal. Up to four probes can be active, each with its own trace color.
Noise traces show power integrated over the selected RBW. Changing RBW, VBW, or Avg Count clears Max Hold, Min Hold, and Video Average history.

## Navigate the Graph

Pan the canvas by middle-click dragging. Rubber-band select, or Ctrl+click, to pick several nodes at once — Ctrl+click on a node body also probes that node's first output. Press Delete to remove them. With two or more selected, right-click one of them to align or distribute the selection. That's the whole workflow — click Finish.
