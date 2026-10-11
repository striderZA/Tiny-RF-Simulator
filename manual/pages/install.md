# Install

Tiny RF Simulator ships as release archives on the [GitHub Releases page](https://github.com/striderZA/Tiny-RF-Simulator/releases). Download the archive for your system from the newest release.

| System | Archive | Contents |
| --- | --- | --- |
| Linux (x86_64) | `rf-simulator-linux-x86_64.tar.gz` | Two executables at the top level: `rf-simulator-linux` and the agent bridge `rf-sim-mcp` |
| Windows (x86_64) | `rf-simulator-windows-x86_64.zip` | A folder, `rf-simulator-windows`, with `tiny-rf-simulator.exe`, the agent bridge `rf-sim-mcp.exe`, and three runtime DLLs |

## Linux

Extract the archive, then start the simulator from the folder you extracted into:

```bash
tar -xzf rf-simulator-linux-x86_64.tar.gz
./rf-simulator-linux
```

Both executables in the archive are already marked executable. `rf-sim-mcp` is the bridge that an AI agent client starts for you. See [Connect an agent](https://github.com/striderZA/Tiny-RF-Simulator#connect-an-agent).

## Windows

Extract `rf-simulator-windows-x86_64.zip`, open the `rf-simulator-windows` folder, and run `tiny-rf-simulator.exe`.

Keep the DLL files and `rf-sim-mcp.exe` in the same folder as `tiny-rf-simulator.exe`. They are part of the archive. The Agent panel looks for the bridge in that folder.

## Files the app writes

The app keeps these files beside the executable: the window layout (`rf_simulator_layout.ini`), named layouts (in `layouts/`), the tutorial marker (`.tutorial_completed`), and extension trust records (`extension_trust.json`).

When **Agent Server** is on, the app also writes a connection file for the bridge. The file is kept in your user profile, not beside the executable. See [Connect an agent](https://github.com/striderZA/Tiny-RF-Simulator#connect-an-agent).

## Built-in parts

The release archives do not include the built-in component library. A new download starts with an empty **Component Library**, which lists only the parts you create and parts from extensions. To use the built-in parts, copy the `component_data` folder from a source checkout into the folder beside the executable. The result must be `component_data/library` beside the executable.

## First run

On the first launch, a **Welcome to Tiny RF Simulator** dialog offers the guided tutorial. Choose **Start Tutorial** to begin, or **Not Now** to skip it. The dialog also points to **Help > Tutorial** for running the tutorial again.
