# Utility Views

The auxiliary panels — [Receiver Requirements](receiver-requirements.md), the [RF Budget](rf-budget.md), the Filter Calculator, [Test Flows](test-flows.md), the [Agent](agent-mcp.md), this help window, and **Tools > Extensions** — open docked in the **Utility Space** window. The Utility Space is its own OS window beside the main application window, so the big tables stay readable without crowding the circuit. On first run it opens beside the main window, clamped onto the screen when the display is too narrow for it to fit beside; it stays a separate window in either case, even where it overlaps the main one.

The main window keeps the editing surfaces and instruments: the Node Editor, Properties, the Component Library, the Log, and the [Spectrum Analyzer](spectrum-analyzer.md), [Network Analyzer](network-analyzer.md), and [Power Meter](power-meter.md).

## Opening and closing

- **View > Utility Space** opens and closes the second window. It also has a close button in its title bar.
- The utility panels hide with the group and return when it is reopened, keeping their dock layout. Opening any utility view from **View**, **Help > How to Use**, or **Tools > Extensions** reopens the group if it was closed.
- The group cannot be collapsed, so a panel reopened from its **View** item is always visible again. Panels you close with their **X** button reopen docked where they were.

## Docking and moving

The **Utility Space** is a normal dock host: its panels stack as tabs inside it.

- Drag a tab out to float it as its own window, or drop it into the main window to dock it there. Panels move freely between the two groups, and a panel moved to the main window stays there.
- Drag the **Utility Space** title bar to move the whole group, or drag its edges to resize it. The group itself cannot be docked into the main window; it always stays a separate window.

## Persistence

The Utility Space's position and size persist in `rf_simulator_layout.ini` beside the executable, along with the dock memberships and panel positions of both windows. **Settings > Save As...** saves the whole arrangement — both windows — under a name, **View > Layouts > Load** restores it, and **Settings > Manage...** renames or deletes saved layouts. Panel visibility and the group's open state use the same per-panel session settings as the other window toggles; those session settings are persisted on Windows.
