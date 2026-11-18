# Undo and redo

Press **Ctrl+Z** to undo the last change and **Ctrl+Shift+Z** or **Ctrl+Y** to redo it, up to 100 steps back. The **Edit** menu names the change each will act on next, for example `Undo Move nodes`. A confirmation line at the bottom-left of the canvas repeats the label for a few seconds.

The shortcuts do not fire while a text field has the keyboard — a field keeps Ctrl+Z for its own text editing.

## What is covered

One undo step each:

- adding, removing, and duplicating components;
- creating and removing links;
- parameter edits — an inspector edit records once, when you release the control;
- adding and removing probes;
- creating, removing, renaming, expanding, and collapsing groups;
- node layout: moves, align, distribute, and snap;
- swapping a placed [library part](component-library.md);
- [receiver requirements](receiver-requirements.md) and [RF budget](rf-budget.md) edits;
- [network analyzer](network-analyzer.md) point and sweep settings;
- [agent](agent-mcp.md) tool calls — each call is one step, labelled `Agent: <tool>`.

Panning the canvas is not an edit.

Removing a component shows a notice that names it and reminds you of Ctrl+Z.

## When the history is cleared

Undo cannot reach across a project replacement. The history is cleared by starting a **New** project, opening a project, and by reverting to an agent checkpoint (which also warns you in its dialog). A checkpoint revert removes every change made after that call, including your own.
