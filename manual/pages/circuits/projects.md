# Projects

A project is a `.rfsim` file. It holds the circuit: components, links, probes, subcircuits, Network Analyzer settings, and Receiver Requirements. When you save, an external data file that a component uses (for example an S-parameter file) is copied into an `rf_data/` folder beside the project, and the component is pointed at the copy.

## New, open, and save

- **File > New** (Ctrl+N) starts an empty project.
- **File > Open...** (Ctrl+O) opens a `.rfsim` project. The Open dialog lists RF Simulator project files (`*.rfsim`), or all files.
- **File > Save** (Ctrl+S) saves the project to its current file. If the project has no file yet, Save opens the Save As dialog.
- **File > Save As...** (Ctrl+Shift+S) saves the project to a new file.

## Unsaved changes

If the project has unsaved changes, **New**, **Open...**, and **Exit** first ask: "You have unsaved changes. Save before continuing?" Choose **Save** to save first, **Discard** to continue without saving, or **Cancel** to stay where you are.

Adding or removing a probe marks the project as unsaved, so **New**, **Open...**, and **Exit** ask before they discard it. Save the project to keep probe changes.
