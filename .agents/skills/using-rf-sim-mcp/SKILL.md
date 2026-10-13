---
name: using-rf-sim-mcp
description: Use when reading or editing an RF Simulator project through the rf-sim MCP tools, running port measurements or network-analyzer sweeps, or when a call returns STALE_EPOCH, LINK_REJECTED, PARAM_REJECTED, BUSY, or SIMULATOR_UNAVAILABLE.
---

# Using the rf-sim MCP server

## Overview

The rf-sim tools act on the project open in the user's RF Simulator window. Argument shapes come from `tools/list`. This skill covers call order, epochs, partial failures, and parameter rules. Error codes, field lists, and measurement details are in [reference.md](reference.md).

## Before the first call

- RF Simulator must be running with **View > Agent > Agent Server** on. Otherwise tool calls return `SIMULATOR_UNAVAILABLE` (`tools/list` still works); ask the user to enable it.
- You cannot open, save, name, or revert projects, or touch files.
- The catalog defines 10 tools, including `data_file_read`, `test_flow_run`, and `receiver_measure`. If `tools/list` omits a tool named here, that bridge does not include it; report the gap rather than guessing its arguments.

## Workflow

1. `circuit_get` (`include_params: true` for values). Use each component's `id` field, not its label number. The response includes positions before the editor draws new nodes.
2. Before changing a component, call `component_get {epoch, component}`; its `param_info` gives keys, units, and read-only flags.
3. Make changes in one `circuit_edit` (1 to 64 ops, applied in order). `move` uses the component id and position from the latest `circuit_get`; use `add.position` for a new component.
4. Measure with `measure_port` or `network_analyzer_sweep`. `null` means unavailable, never zero.

## Epochs

- Your edits do not change `epoch`, so the epoch from `circuit_get` stays valid. `revision` advances only when an edit changes project state.
- `STALE_EPOCH` means the project was replaced. Call `circuit_get` and re-plan; component ids restart at 100, so an old id may now name something else.
- A canvas pan changes editor-space positions but does not change `epoch`. Call `circuit_get` again after a pan before you move a component.

## circuit_edit rules

- `add` takes `type` as the exact lowercase registry key (`amplifier`, `attenuator`, `generator`, `adc`, `pfb`); unknown types return `UNKNOWN_TYPE`.
- Ops stop at the first error, but earlier ops stay applied. Read `op_index` and `applied`, call `circuit_get`, and send only the remaining work. If a target is `NOT_FOUND`, confirm the intent with the user rather than reassigning the op.
- `from` is an output port and `to` is an input port. To rewire an occupied input (`LINK_REJECTED`, `INPUT_OCCUPIED`), put `{op:"disconnect", to}` before the `connect` in the same call.
- PFB channelizer input 0 accepts only RF ADC output 0 (`ADC_TO_PFB_ONLY`). If no ADC exists, add one (`type: "adc"`, with a `ref`) and route through it in the same call.
- Probes name a side. `probe_add` and `probe_remove` take `at` as a port with an optional `direction`, `"output"` by default or `"input"`. `circuit_get` reports each probe as `{component, port, direction}`; pass that object back as `at` to remove the same probe.
- A repeated `move` target can differ slightly from read-back coordinates because of float rounding after a pan. The API returns `unchanged: true` and creates no checkpoint when that difference does not change the position.

## Parameters

- Keys are the top-level names from `param_info`; `tones[].freq_Hz` is written as `tones`. Units are in the key suffix: `_Hz`, `_dB`, `_dBm`, `_dBm_per_Hz`.
- Array parameters are replaced whole. Read the array with `component_get`, send every element with all its fields, and change only the one you mean. Dotted keys such as `tones[0].power_dBm` return `UNKNOWN_KEY`.

## Errors

Look up any code in [reference.md](reference.md). `SIMULATOR_UNAVAILABLE` and `BUSY` need the user; `STALE_EPOCH` needs `circuit_get`; `NOT_FOUND` and `INVALID_ARGUMENT` need a re-read and a fix to the named argument.
