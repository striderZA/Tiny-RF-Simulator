---
name: using-rf-sim-mcp
description: Use when working with RF Simulator circuits or component libraries via MCP, including library authoring, projects, measurements, or MCP errors.
---

# Using the rf-sim MCP server

Tools act on the open project. Use `tools/list` for arguments and [reference.md](reference.md) for error and field details.

## Before the first call

- Enable Agent Server (**View > Agent > Agent Server**) or calls return `SIMULATOR_UNAVAILABLE`.
- The catalog has 13 tools; report any missing from `tools/list`.
- For library entries, call `component_types {type}`; create only when `authorable` is true and use non-file `library_fields` in `library_create`. It writes validated JSON only; no circuit placement, state/checkpoint changes, or overwrite. See the reference for roots and limits.
- Only `project_save`/`project_load` read or write `.rfsim`; MCP offers no dialogs or checkpoint reverts.

## Workflow

1. `circuit_get` (`include_params: true` for values). Use each component's `id`, not its label number.
2. Before changing a component, call `component_get {epoch, component}` for its `param_info`.
3. Make changes in one `circuit_edit` (1 to 64 ops, applied in order). `move` takes an existing id and an absolute position; `add.position` places a new component.
4. Measure with `measure_port` or `network_analyzer_sweep`. `null` means unavailable, never zero.

## Epochs

- Edits do not change `epoch`. `revision` advances only when an edit changes project state.
- On `STALE_EPOCH`, re-read `circuit_get`: project ids restart at 100 and may be reassigned.
- Panning changes positions, not epoch; re-read before moving.

## Projects

- `project_save` writes the open file, or `path` when given. `project_load {epoch, path}` replaces the circuit. Both use absolute `.rfsim` paths.
- `project_load` returns `UNSAVED_CHANGES` while the project has unsaved edits. Save first, or ask the user to save or discard them.
- A successful load clears checkpoints and changes the epoch. Call `circuit_get` afterwards.

## circuit_edit rules

- `add` takes a lowercase registry key; unknown types return `UNKNOWN_TYPE`.
- Ops stop at first error, but earlier ops remain applied. Use `op_index`/`applied`, re-read, then send remaining work; confirm before retrying `NOT_FOUND`.
- To rewire an occupied input (`INPUT_OCCUPIED`), put `{op:"disconnect", to}` before the `connect` in the same call.
- PFB channelizer input 0 accepts only RF ADC output 0 (`ADC_TO_PFB_ONLY`). If no ADC exists, add one (`type: "adc"`, with a `ref`) and route through it in the same call.

## Parameters

- Use top-level `param_info` keys; units appear in suffixes. Array roots such as `tones` replace the whole array.
- Send each array element with all fields. Dotted/indexed keys return `UNKNOWN_KEY`.

## Errors

Ask the user about `SIMULATOR_UNAVAILABLE`, `BUSY`, and `UNSAVED_CHANGES`. Retry `OVERLOADED` when the queue drains; re-read on `STALE_EPOCH`; fix the named argument for `NOT_FOUND`/`INVALID_ARGUMENT`.
