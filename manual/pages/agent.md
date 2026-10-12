# Agent (MCP)

The local `rf-sim-mcp` bridge connects an MCP client to the project that is open in Tiny RF Simulator.
For installation details, see [Install](install.md). For client configuration examples, see the [README](https://github.com/striderZA/Tiny-RF-Simulator/blob/master/README.md).

The bridge can read the circuit, edit its components, and run measurements. It cannot open, save, or name files. Use the simulator GUI to open and save projects.

## Connect an agent

Keep Tiny RF Simulator open. Select **View > Agent**, then enable **Agent Server**.
The MCP client can connect while **Agent Server** is on.

## Move a component

Call `circuit_get` to read the current circuit. Each component has a `position` object with numeric `x` and `y` values.
These are editor-space coordinates. They include the current canvas pan.
A pan changes these coordinates but does not change `epoch`. After a pan, call `circuit_get` again before you move a component.

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
Select **Revert to before this call** on a checkpoint to restore the project to its state before the edit.
The dialog warns that a revert removes every change after the call. This also removes your later edits.
Select **Revert** only if you want to discard those changes.
