# Agent (MCP)

The local `rf-sim-mcp` bridge connects an MCP client to the project that is open in Tiny RF Simulator.
For installation details, see [Install](install.md). For client configuration examples, see the [README](https://github.com/striderZA/Tiny-RF-Simulator/blob/master/README.md).

The bridge can read the circuit, edit its components, run measurements, and save or load `.rfsim` project files. It cannot open file dialogs. `project_save` is the only way it names a file.

## Connect an agent

Keep Tiny RF Simulator open. Select **View > Agent**, then enable **Agent Server**.
The MCP client can connect while **Agent Server** is on.

## Move a component

Call `circuit_get` to read the current circuit and each component's position.
Each position has numeric `x` and `y` values in editor space.
The editor-space values include the current canvas pan.

A new component has a position before the editor shows the component.
A pan changes these values but does not change `epoch`.
After a pan, call `circuit_get` again before you move a component.

Send a `move` operation to `circuit_edit`. The operation uses an absolute position, not a movement offset.
Use the component `id` from `circuit_get`, not its label. A move applies only to components that existed before the current `circuit_edit` call.
Set a new component's position in its `add` operation.

```json
{
  "epoch": 12,
  "ops": [
    {
      "op": "move",
      "component": 101,
      "position": { "x": 420, "y": 220 }
    }
  ]
}
```

Send the current `epoch` from `circuit_get`. The `x` and `y` values must be finite numbers.

A changed move advances `revision`, marks the project dirty, and creates a checkpoint.
A move to the current position returns `unchanged: true`.
The move does not change `revision` or create a checkpoint.

Select **Revert to before this call** on a checkpoint to restore the project to its state before the edit.
The dialog warns that a revert removes every change after the call. This also removes your later edits.
Select **Revert** only if you want to discard those changes.

## Save and load a project

`project_save` writes the open project to a file. Without a path, it saves to the file that is already open.
A path must be an absolute path that ends in `.rfsim`. Replacing a different existing file needs `overwrite` set to `true`.
An untitled project needs a path.

`project_load` replaces the open circuit with a `.rfsim` file. Send the `epoch` from the latest circuit read.
The load is refused while the project has unsaved changes. Save first, or ask the user to save or discard the changes in the GUI.
A successful load clears the Agent panel's checkpoints, so a revert cannot restore the circuit from before the load.
