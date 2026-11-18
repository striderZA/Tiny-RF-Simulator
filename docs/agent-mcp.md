# Agent (MCP)

The local `rf-sim-mcp` bridge connects an MCP client to the project that is open in Tiny RF Simulator.
For installation details, see [Install](install.md). For client configuration examples, see the [repository README](../README.md).

The bridge can read and edit the circuit, run measurements and sweeps, save or load `.rfsim` project files, edit receiver requirements and the RF budget, and create reusable component-library entries. Library authoring is separate from circuit editing and does not place an instance. The bridge cannot open file dialogs, and it cannot read or write arbitrary files: agents write only the project file, the `rf_data/` copies a save makes, and `library_create` JSON. Both project tools take absolute paths to `.rfsim` files.

## Connect an agent

Keep Tiny RF Simulator open. Select **View > Agent**, then enable **Agent Server**.
The MCP client can connect only while **Agent Server** is on — otherwise every tool call returns `SIMULATOR_UNAVAILABLE` with a message that says so. The setting is remembered between runs. One client is admitted at a time; a second gets `BUSY`.

The client launches the bridge over standard input/output. In the release layout the bridge sits beside the simulator executable, and no extra arguments are needed:

```json
{
  "mcpServers": {
    "rf-sim": {
      "command": "<install dir>/rf-sim-mcp"
    }
  }
}
```

On Windows, use `rf-sim-mcp.exe` in the `command`. For Claude Code:

```sh
claude mcp add rf-sim -- "<install dir>/rf-sim-mcp"
```

While the server is on, the app writes a private connection file in your user profile (never beside the executable) that the bridge reads. Do not edit or share it. If you keep the bridge somewhere other than beside the simulator, pass `--endpoint <file>` to point it at the matching connection file.

The bridge listens on the loopback interface only. It protects the connection from other local accounts and from web pages; it does not protect against other software running as your user, or administrators.

## The Agent panel

**View > Agent** shows:

- **Status** — `Off`, `Waiting for a client`, `Connected: <client>`, `Busy: calls are waiting`, or an error message. The menu bar shows the same state; clicking it opens the panel.
- **Recent calls** — the newest fifty calls with their result and duration.
- **Checkpoints** — one entry per call that changed the project, newest first, each with a **Revert to before this call** button.

Closing the Agent panel does not stop the server.

## The tools

Read-only — they never change the project and never create a checkpoint:

- `circuit_get` and `component_get` — the circuit, its components, positions, parameters, and the Network Analyzer settings.
- `component_types` and `library_search` — registered types with their fields, and library parts by part number, manufacturer, or description.
- `measure_port` — total power, peak, noise floor, tones, and SNR at one output port, with an optional trace.
- `receiver_measure` — output-power and IIP3 sweeps between two points; repeat the call while it reports `in_progress`.
- `requirements_get`, `budget_get`, `budget_cascade` — the receiver requirements, the budget and each mode's predicted verdicts, and one mode's per-frequency cascade.
- `data_file_read` — samples from an S-parameter file a component or library part already references.

Changing — each call that changes the project is one checkpoint and one undo step:

- `circuit_edit` — up to 64 ordered operations: add, remove, move, set_params, connect, disconnect, probe_add, probe_remove. A failed batch stops at the first error and keeps the applied prefix.
- `network_analyzer_sweep` — reads gain and noise figure between two points, and can set the analyzer's sweep settings.
- `test_flow_run` — a parameter sweep with measurements, up to 1000 rows; the circuit is restored afterwards. See [Test Flows](test-flows.md).
- `requirements_edit` and `budget_edit` — replace the whole requirements or budget set.

Library and project files:

- `library_create` — writes a validated JSON-only library part; it does not change the circuit or create a checkpoint.
- `project_save` and `project_load` — save to or load an absolute `.rfsim` path.

## Epoch and revision

Every read that carries state reports the project's `epoch`, `revision`, and `dirty` flag.

- The **epoch** changes only when the project is replaced: a new project, opening one, the tutorial, or a checkpoint revert. Ordinary edits never change it.
- The **revision** advances with every accepted change; `dirty` means the revision is ahead of the last save.

Send the current `epoch` with every edit. If the project was replaced since your last read, the call returns `STALE_EPOCH` naming the cause — read `circuit_get` again and retry. Component ids come from `circuit_get`, not from the label, and they restart at 100 after a replacement. Panning changes reported positions but not the epoch, so re-read positions before moving a component after a pan.

## Checkpoints and undo

Every agent call that changes the circuit, requirements, or budget leaves a checkpoint in the Agent panel, and one undo step labelled `Agent: <tool>` in the **Edit** menu.

Select **Revert to before this call** on a checkpoint to restore the project to its state before the edit. The dialog warns that a revert removes every change after the call — including your own later edits — and confirms with **Revert**. A revert also clears the undo history, so Ctrl+Z cannot bring back the circuit from before it, and every checkpoint from that call onward is removed. A project replacement — New, Open, or a load — clears the checkpoints, so a revert cannot undo a load. The bridge itself has no revert tool; reverting happens only in the GUI, after confirmation.

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

## Save and load a project

`project_save` writes the open project to a file. Without a path, it saves to the file that is already open.
A path must be absolute and end in `.rfsim`. Replacing a different existing file needs `overwrite` set to `true`.
An untitled project needs a path. Saving copies referenced RF data files outside the project folder into `rf_data/` beside the project, so GUI and agent saves include the models needed on reload. The result lists each copied project-relative path in `copied_data_files`; no copies are listed when every reference is already inside the project.
The save fails if a referenced file cannot be copied, if its destination would overwrite a different file or symlink, or, when a referenced file lies outside the project folder, if `rf_data/` is a symbolic link or Windows junction. Failed saves keep the existing `.rfsim` file intact.

`project_load` replaces the open circuit with a `.rfsim` file. Send the `epoch` from the latest circuit read.
The load is refused while the project has unsaved changes. Save first, or ask the user to save or discard the changes in the GUI.
A successful load clears the Agent panel's checkpoints, so a revert cannot restore the circuit from before the load.

## Limits

Calls are bounded so a runaway client cannot wedge the app: the queues hold at most 64 calls and 8 MiB, a call over the bound gets an immediate `OVERLOADED`, and no reply exceeds 1 MiB. While the app is showing a modal dialog, calls park and return `BUSY` after 30 seconds with a message asking the user to close it.
