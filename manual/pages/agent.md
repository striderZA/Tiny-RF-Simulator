# Agent (MCP)

The local `rf-sim-mcp` bridge connects an MCP client to the project open in Tiny RF Simulator. For setup instructions, see [Connect an agent in the README](https://github.com/striderZA/Tiny-RF-Simulator#connect-an-agent).

The bridge can read the circuit, edit its components, and run measurements. It cannot open, save, or name files. Use the simulator GUI to open and save projects.

## Move a component

Call `circuit_get` to read the current circuit. Each component has a `position` object with numeric `x` and `y` values. These are editor-space coordinates.

Send a `move` operation to `circuit_edit`. The operation uses an absolute position, not a movement offset. Use the component `id` from `circuit_get`, not its label.

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

Use the current `epoch` from `circuit_get`. The `x` and `y` values must be finite numbers.

A changed move advances `revision`, marks the project dirty, and creates a checkpoint. Use **Revert** in the Agent panel to restore the project to the state before the edit.
