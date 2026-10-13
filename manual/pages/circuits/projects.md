# Projects

A project is a single `.rfsim` file. It holds the circuit: components, links, probes, subcircuits, Network Analyzer settings, and Receiver Requirements.

## New, open, and save

- **File > New** (Ctrl+N) starts an empty project.
- **File > Open...** (Ctrl+O) opens a `.rfsim` project. The Open dialog lists RF Simulator project files (`*.rfsim`), or all files.
- **File > Save** (Ctrl+S) saves the project to its current file. If the project has no file yet, Save opens the Save As dialog.
- **File > Save As...** (Ctrl+Shift+S) saves the project to a new file.

## Unsaved changes

If the project has unsaved changes, **New**, **Open...**, and **Exit** first ask: "You have unsaved changes. Save before continuing?" Choose **Save** to save first, **Discard** to continue without saving, or **Cancel** to stay where you are.

Adding or removing a probe does not mark the project as unsaved. If a probe is your only change, **New**, **Open...**, and **Exit** continue without asking, and the probe change is lost. Save the project after you change probes.
