# Agent (MCP)

The local `rf-sim-mcp` bridge connects an MCP client to the project that is open in Tiny RF Simulator.
For installation details, see [Install](install.md). For client configuration examples, see the [README](https://github.com/striderZA/Tiny-RF-Simulator/blob/master/README.md).

The bridge can read and edit the circuit, run measurements, save or load `.rfsim` project files, and create reusable component-library entries. Library authoring is separate from circuit editing and does not place an instance. The bridge cannot open file dialogs. Both project tools take absolute paths to `.rfsim` files.

## Connect an agent

Keep Tiny RF Simulator open. Select **View > Agent**, then enable **Agent Server**.
The MCP client can connect while **Agent Server** is on.

## Create a reusable library entry

Call `component_types` with a `type` to check `authorable` and read `library_fields`. Use non-file fields only when `authorable` is true; runtime `param_info` may use different serialized keys.

```json
{
  "type": "attenuator",
  "part_number": "ATT-10DB",
  "manufacturer": "Example",
  "parameters": { "attenuation_dB": 10.0 },
  "scope": "project"
}
```

`scope` is `project` by default or `global`. Project entries persist under `./rf-sim-libraries`, relative to the simulator process's current working directory. Global entries persist under `$HOME/.rf-sim/libraries` on POSIX or `%USERPROFILE%/.rf-sim/libraries` on Windows. Successful entries are available immediately to `library_search` and can be placed with `circuit_edit`.

Creation writes only a validated JSON definition. Invalid definitions are rejected without publishing an entry; existing type/part-number pairs and destination files are never overwritten. The tool does not accept arbitrary paths, file references, `data_files`, asset uploads, or `.rflib` imports. It does not change circuit epoch, revision, or dirty state, and it does not create an Agent checkpoint; checkpoint reverts cannot undo library creation.

To place the reusable part, use `circuit_edit` with an `add` operation and its `library_part`. Send the latest `epoch` from `circuit_get`:

```json
{
  "epoch": 12,
  "ops": [
    {
      "op": "add",
      "library_part": { "part_number": "ATT-10DB", "type": "attenuator" }
    }
  ]
}
```

This adds an instance to the open circuit; `library_create` itself does not. Moving an existing instance remains the separate `move` operation below.

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
A path must be absolute and end in `.rfsim`. Replacing a different existing file needs `overwrite` set to `true`.
An untitled project needs a path. Saving copies referenced RF data files outside the project folder into `rf_data/` beside the project, so GUI and agent saves include the models needed on reload. The result lists each copied project-relative path in `copied_data_files`; no copies are listed when every reference is already inside the project.
The save fails if a referenced file cannot be copied, if its destination would overwrite a different file or symlink, or, when a referenced file lies outside the project folder, if `rf_data/` is a symbolic link or Windows junction. Failed saves keep the existing `.rfsim` file intact.

`project_load` replaces the open circuit with a `.rfsim` file. Send the `epoch` from the latest circuit read.
The load is refused while the project has unsaved changes. Save first, or ask the user to save or discard the changes in the GUI.
A successful load clears the Agent panel's checkpoints, so a revert cannot restore the circuit from before the load.
