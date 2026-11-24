# Install

Tiny RF Simulator ships as release archives on the [GitHub Releases page](https://github.com/striderZA/Tiny-RF-Simulator/releases). Download the archive for your system from the newest release.

| System | Archive | Contents |
| --- | --- | --- |
| Linux (x86_64) | `rf-simulator-linux-x86_64.tar.gz` | Two executables at the top level, `rf-simulator-linux` and the agent bridge `rf-sim-mcp`, plus the `component_data/` folder with the built-in component library |
| Windows (x86_64) | `rf-simulator-windows-x86_64.zip` | A folder, `rf-simulator-windows`, with `tiny-rf-simulator.exe`, the agent bridge `rf-sim-mcp.exe`, three runtime DLLs, and the `component_data/` folder with the built-in component library |

## Linux

Extract the archive, then start the simulator from the folder you extracted into:

```bash
tar -xzf rf-simulator-linux-x86_64.tar.gz
./rf-simulator-linux
```

Both executables in the archive are already marked executable. `rf-sim-mcp` is the bridge that an AI agent client starts for you. See [Agent (MCP)](agent-mcp.md).

## Windows

Extract `rf-simulator-windows-x86_64.zip`, open the `rf-simulator-windows` folder, and run `tiny-rf-simulator.exe`.

Keep the DLL files, `rf-sim-mcp.exe`, and the `component_data` folder in the same folder as `tiny-rf-simulator.exe`. They are part of the archive. The Agent panel looks for the bridge in that folder, and the built-in parts are read from `component_data/library` there.

## Files the app writes

The app keeps these files beside the executable: the window layout (`rf_simulator_layout.ini`), named layouts (in `layouts/`), the tutorial marker (`.tutorial_completed`), and extension trust records (`extension_trust.json`).

When **Agent Server** is on, the app also writes a connection file for the bridge. The file is kept in your user profile, not beside the executable. See [Agent (MCP)](agent-mcp.md).

## Built-in parts

The release archives include the built-in component library beside the executable, so a fresh download lists the 19 built-in parts in **View > Component Library**. The definitions and their S-parameter files live in `component_data/library` (`rf-simulator-windows/component_data/library` in the Windows zip); keep that folder beside the executable.

## First run

On the first launch, a **Welcome to Tiny RF Simulator** dialog offers the guided tutorial. Choose **Start Tutorial** to begin, or **Not Now** to skip it. The dialog also points to **Help > Tutorial** for running the tutorial again.
