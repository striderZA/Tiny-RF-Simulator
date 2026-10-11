# rf-sim MCP reference

Read this when a call returns an error, when you change parameters, or when you interpret measurement fields. Argument shapes come from each tool's `inputSchema` in `tools/list`; this file covers meaning and rules.

Results are JSON in `structuredContent`; the same JSON is the text of `content`. Failures set `isError: true` and carry `{epoch, error: {code, message, hint?, op_index?, details?}}`.

## circuit_edit operations

- `{op:"add", type, ref?, params?, position?:{x,y}}`, or `{op:"add", library_part:{part_number, type?}, ref?, params?, position?}`.
- `{op:"remove", component}`
- `{op:"set_params", component | ref, params}`
- `{op:"connect", from, to}`: `from` is an output port and `to` is an input port. Each endpoint is `{component, port}` or `{ref, port}`.
- `{op:"disconnect", to}`: the input side only. An unconnected input returns `NOT_FOUND`.
- `{op:"probe_add" | "probe_remove", at}`: `at` is an output port.
- Ports are zero-based. A batch holds 1 to 64 ops.
- `ref`: a letter, then letters, digits, or underscores, at most 32 characters. It names a component added earlier in the same call and is unusable outside that call.
- Success returns `epoch`, `revision`, `applied`, and `refs` (name to id). Each `applied` entry has `op_index` and `op`, plus `component`, `ref`, `label`, `params`, `also_changed` (`{path, old_value, new_value}` records), and `unchanged` where relevant.
- An error returns `error` plus `applied` for the committed prefix. `op_index` names the failing op.

## Parameters

- Keys are the top-level names from `param_info`. An array field such as `tones[].freq_Hz` is written under its top-level key, `tones`.
- Units are in the key suffix: `_Hz`, `_dB`, `_dBm`, `_dBm_per_Hz`. Numeric values are plain JSON numbers.
- Array parameters are replaced whole, so elements you omit are removed. Each element must contain every member field.
- `add` and `set_params` follow the same rules.
- `PARAM_REJECTED` `details.reason` values:
  - `UNKNOWN_KEY`: not a top-level name. `details.suggestions` lists close names. Dotted or indexed keys such as `tones[0].power_dBm` land here.
  - `TYPE_MISMATCH`: wrong JSON type, or an array element missing a member field. `details.path` names it, such as `tones[0]`.
  - `READ_ONLY`: a derived value. Do not write it.
  - `ENGINE_ADJUSTED`: the engine would store another value. `details.requested` and `details.stored` show both; send a value the engine keeps.
- `PATH_PARAMS_UNSUPPORTED`: `sparam_filepath` and `sparam_path` are refused. Agents cannot set file references.

## Errors

| Code | Meaning and action |
|---|---|
| SIMULATOR_UNAVAILABLE | RF Simulator is not reachable. Ask the user to open it and turn on View > Agent > Agent Server, then retry. |
| BUSY | "showing a dialog": ask the user to close it, then retry. "another agent is connected": RF Simulator admits one agent client; ask the user to stop the other agent. |
| VERSION_MISMATCH | Bridge and GUI catalog versions differ. Ask the user to install the same release of both. |
| STALE_EPOCH | The project was replaced. `details.cause` is `new_project`, `opened_project`, `tutorial`, or `reverted`; `details.undone` lists reverted edits; `details.epoch` is current. Call `circuit_get` and re-plan. |
| NOT_FOUND | Unknown component, output port, or `ref`, or an unconnected input for `disconnect`. Re-read. |
| INVALID_ARGUMENT | Malformed argument. `details.path` names it, for example `/ops` for a batch outside 1 to 64. |
| LINK_REJECTED | `details.reason` is `INPUT_OCCUPIED` (with `existing_source`), `CYCLE`, `ADC_TO_PFB_ONLY`, or `POLICY`. |
| PARAM_REJECTED | See Parameters. |
| PATH_PARAMS_UNSUPPORTED | A file-reference parameter was set. These are refused. |
| UNKNOWN_TYPE | Unknown component type. Check `component_types`. |
| UNKNOWN_PART, AMBIGUOUS_PART | Library lookup failed. `details.candidates` lists up to five matches; pass `type` to disambiguate. |
| NO_MEASUREMENT | `details.reason` is `NO_PATH` (no forward path between the points) or `NO_VALID_POINTS` (the sweep has no valid points). |
| INTERNAL | Unexpected failure, or a reply over the 1 MiB protocol limit. Re-read with `circuit_get`, then report it. |

## Measurements

- `measure_port` takes `at = {component, port}`; `port` is an output index, and one past the last output is `NOT_FOUND`. It recomputes the live circuit before reading, so a just-made edit is visible.
- Optional arguments: `max_tones` 1 to 32 (default 8), and `trace` with `start_Hz` below `stop_Hz` and `points` 2 to 401 (default 201).
- Result fields: `total_power_dBm`, `peak` (`freq_Hz`, `power_dBm`), `noise_floor_dBm_per_Hz`, `tones` (`freq_Hz`, `power_dBm`, `phase_deg`), `tone_count`, `snr_dB`, `snr_basis`, `fs_Hz`, `is_complex_baseband`, and `trace` (`frequencies_Hz` and `noise_dBm_per_Hz`, parallel arrays).
- `null` marks an unavailable or non-finite value. It is never zero.
- `snr_basis.kind` is `rbw` (with `rbw_Hz`) for analyzer SNR, or `pfb_channel` (with `enbw_Hz` and `channel_noise_dBm`) for PFB outputs.
- For real-domain signals, the reported single-sided tone power is 3.01 dB above the spectrum analyzer's displayed peak.

## network_analyzer_sweep

- Takes two output endpoints, `point_a` and `point_b`. Optional: `start_Hz`, `stop_Hz`, `points`, `stimulus_dBm`, and `arrays`. If you give both `start_Hz` and `stop_Hz`, `start_Hz` must be below `stop_Hz`. If you give only one, it is checked against the current value of the other. If `arrays` is given, `arrays.max_points` is required (2 to 401).
- Omitted settings keep their current values. The engine clamps `points`; `settings.points` shows the value used.
- A changed setting is recorded as a checkpoint the user can revert in the Agent panel. Repeating the same call changes nothing.
- Result: `summary` (`gain_dB` and `nf_dB`, each with `min`, `max`, `mean`, `at_start`, `at_center`, `at_stop`) and optional `arrays`.

## Other tools

- `component_types {type?}`: registered types and their ports. With `type`, also `default_params` and `param_info`.
- `library_search {query?, type?, limit?}`: case-insensitive substring match on part number, manufacturer, and description. `limit` is 1 to 50 (default 20); `total` counts every match.
- `data_file_read`: summarizes an S-parameter file; it takes no paths and never writes. Use exactly one route:
  - `{component, epoch}`: the file a component references. `epoch` is required and must match the current epoch, or the call returns `STALE_EPOCH`.
  - `{part_number, type?, epoch?}`: a library part. `type` applies only here. `epoch` is not compared with the current epoch; if supplied, it must be an unsigned integer (`INVALID_ARGUMENT` otherwise).
- `s_row` and `s_col` (0 to 3, defaults 1 and 0) and `max_points` (2 to 401, default 201) apply to both routes.
- `test_flow_run {epoch, conditions?, measure}`: sweeps up to four parameters (each `{component, path, values}`) and measures 1 to 8 metrics. A sweep of more than 1000 rows is refused with `INVALID_ARGUMENT`. The circuit is restored afterwards. Its `path` uses dotted, indexed syntax such as `tones[0].power_dBm`; `set_params` does not accept that syntax.
- `receiver_measure {epoch, point_a, point_b, start_Hz, stop_Hz, points?, output_power?, reference_tone_Hz?, iip3?}`: receiver output power and IIP3 across a sweep. Each call advances up to 4 ms, so repeat while `in_progress` is true. Result: `in_progress`, `frequencies_Hz`, `output_power_dBm`, and `iip3_dBm`; unavailable points are `null`. It is read-only.

## Limits

- You cannot open, save, name, or revert projects, or read and write files. `data_file_read` reads only files that a component or library part already references.
- RF Simulator admits one agent client at a time.
- Each reply is capped at 1 MiB. An oversized result becomes `INTERNAL`; request fewer points.
