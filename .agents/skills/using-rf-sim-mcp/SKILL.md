---
name: using-rf-sim-mcp
description: Use when reading or editing an RF Simulator project through the rf-sim MCP tools, saving or loading project files, running port measurements or network-analyzer sweeps, or when a call returns STALE_EPOCH, LINK_REJECTED, PARAM_REJECTED, UNSAVED_CHANGES, BUSY, OVERLOADED, or SIMULATOR_UNAVAILABLE.
---

# Using the rf-sim MCP server

Tools act on the project open in the user's RF Simulator window. Argument shapes come from `tools/list`. Error codes and field details are in [reference.md](reference.md).

## Before the first call

- Agent Server must be on (**View > Agent > Agent Server**). Otherwise calls return `SIMULATOR_UNAVAILABLE`; ask the user to enable it.
- The catalog defines 12 tools. If `tools/list` omits one named here, report the gap rather than guessing its arguments.
- Only `project_save` and `project_load` touch project files. File dialogs and checkpoint reverts are not available.

## Workflow

1. `circuit_get` (`include_params: true` for values). Use each component's `id`, not its label number.
2. Before changing a component, call `component_get {epoch, component}` for its `param_info`.
3. Make changes in one `circuit_edit` (1 to 64 ops, applied in order). `move` takes an existing id and an absolute position; `add.position` places a new component.
4. Measure with `measure_port` or `network_analyzer_sweep`. `null` means unavailable, never zero.

## Epochs

- Edits do not change `epoch`. `revision` advances only when an edit changes project state.
- `STALE_EPOCH` means the project was replaced. Call `circuit_get` and re-plan: ids restart at 100, so an old id may name something else.
- A canvas pan changes positions, not `epoch`. Re-read before moving a component.

## Projects

- `project_save` writes the open file, or `path` when given. `project_load {epoch, path}` replaces the circuit. Both use absolute `.rfsim` paths.
- `project_load` returns `UNSAVED_CHANGES` while the project has unsaved edits. Save first, or ask the user to save or discard them.
- A successful load clears checkpoints and changes the epoch. Call `circuit_get` afterwards.

## circuit_edit rules

- `add` takes `type` as the exact lowercase registry key (`amplifier`, `attenuator`, `generator`, `adc`, `pfb`); unknown types return `UNKNOWN_TYPE`.
- Ops stop at the first error, but earlier ops stay applied. Read `op_index` and `applied`, re-read, and send only the remaining work. On `NOT_FOUND`, confirm intent with the user rather than reassigning the op.
- To rewire an occupied input (`INPUT_OCCUPIED`), put `{op:"disconnect", to}` before the `connect` in the same call.
- PFB channelizer input 0 accepts only RF ADC output 0 (`ADC_TO_PFB_ONLY`). If no ADC exists, add one (`type: "adc"`, with a `ref`) and route through it in the same call.

## Parameters

- Keys are the top-level names from `param_info`; `tones[].freq_Hz` is written as `tones`. Units are in the key suffix (`_Hz`, `_dB`, `_dBm`, `_dBm_per_Hz`).
- Array parameters are replaced whole. Read with `component_get`, send every element with all its fields, and change only the one you mean. Dotted keys such as `tones[0].power_dBm` return `UNKNOWN_KEY`.

## Errors

`SIMULATOR_UNAVAILABLE`, `BUSY`, and `UNSAVED_CHANGES` need the user. `OVERLOADED` means an agent request queue is full; retry after queued work drains. `STALE_EPOCH` needs `circuit_get`. `NOT_FOUND` and `INVALID_ARGUMENT` need a re-read and a fix to the named argument.
