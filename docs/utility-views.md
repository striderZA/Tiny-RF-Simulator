# Utility Views

The auxiliary panels — [Receiver Requirements](receiver-requirements.md), the [RF Budget](rf-budget.md), the Filter Calculator, [Test Flows](test-flows.md), the [Agent](agent-mcp.md), this help window, and **Tools > Extensions** — open docked in the **Utility Space** window, a second docking group beside the main window. Under multi-viewport it is its own OS window, so the big tables stay readable without crowding the circuit.

The main window keeps the editing surfaces and instruments: the Node Editor, Properties, the Component Library, the Log, and the [Spectrum Analyzer](spectrum-analyzer.md), [Network Analyzer](network-analyzer.md), and [Power Meter](power-meter.md).

## Docking and moving

The **Utility Space** is a normal dock host: its panels stack as tabs inside it.

- Drag a tab out to float it as its own window, or drop it into the main window to dock it there. Panels move freely between the two groups, and a panel moved to the main window stays there.
- Drag the **Utility Space** title bar to move the whole group, or drop it into the main window to dock the group itself. Dragging it outside the main window turns it back into a separate OS window.
- The group cannot be closed and cannot be collapsed, so a panel reopened from its **View** item is always visible again. Panels you close with their **X** button reopen docked where they were.

## Persistence

Both docking groups persist in `rf_simulator_layout.ini` beside the executable: panel positions, dock memberships, and the group's position and size are restored on the next launch. **View > Layouts > Save As...** saves the whole arrangement — both groups — under a name, and **View > Layouts > Load** restores it. Panel visibility (which panels are open) is saved separately and also persists between runs.
