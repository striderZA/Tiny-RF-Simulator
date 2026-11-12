# rf-sim MCP reference

Read this when a call returns an error, when you change parameters, or when you interpret measurement fields. Argument shapes come from each tool's `inputSchema` in `tools/list`; this file covers meaning and rules.

Results are JSON in `structuredContent`; the same JSON is the text of `content`. Failures set `isError: true` and carry `{epoch, error: {code, message, hint?, op_index?, details?}}`.

## circuit_edit operations

- `{op:"add", type, ref?, params?, position?:{x,y}}`, or `{op:"add", library_part:{part_number, type?}, ref?, params?, position?}`.
- `{op:"remove", component}`
- `{op:"move", component, position:{x,y}}` moves an existing node to an absolute editor-space position. `circuit_get.components[].position` reports the current `{x,y}` in the same coordinates.
- Editor-space positions include the current canvas pan. A pan does not change `epoch`; call `circuit_get` again after a pan before a move.
- A move can target only components that existed before the current `circuit_edit` call. Set a new component's position in its `add` operation.
- `{op:"set_params", component | ref, params}`
- `{op:"connect", from, to}`: `from` is an output port and `to` is an input port. Each endpoint is `{component, port}` or `{ref, port}`.
- `{op:"disconnect", to}`: the input side only. An unconnected input returns `NOT_FOUND`.
- `{op:"probe_add" | "probe_remove", at}`: `at` names a port. `direction` is `"output"` (the default) or `"input"`. `circuit_get` lists each probe as `{component, port, direction}`, and that endpoint can be passed back as `at`.
- A changed move advances `revision`, marks the project dirty, and creates a checkpoint. A no-op move that differs only by pan-related float rounding returns `unchanged: true` without advancing `revision` or creating a checkpoint. **Revert to before this call** in the Agent panel discards later changes, including user edits, after confirmation. Invalid component ids return `NOT_FOUND`; non-finite coordinates, and finite ones outside the 32-bit float range, return `INVALID_ARGUMENT`.
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
| OVERLOADED | An MCP bridge or GUI request queue reached its count or payload-byte bound. Retry after queued work drains. Event-queue rejection is a JSON-RPC error (`error.data.code`); bridge call-queue and GUI inbox rejection are MCP tool results (`result.structuredContent.error.code`). Each echoes the request id when present. |
| VERSION_MISMATCH | Bridge and GUI catalog versions differ. Ask the user to install the same release of both. |
| STALE_EPOCH | The project was replaced. `details.cause` is `new_project`, `opened_project`, `tutorial`, or `reverted`; `details.undone` lists reverted edits; `details.epoch` is current. Call `circuit_get` and re-plan. |
| NOT_FOUND | Unknown component, output port, or `ref`, or an unconnected input for `disconnect`. Re-read. |
| INVALID_ARGUMENT | Malformed argument. `details.path` names it, for example `/ops` for a batch outside 1 to 64. |
| LINK_REJECTED | `details.reason` is `INPUT_OCCUPIED` (with `existing_source`), `CYCLE`, `ADC_TO_PFB_ONLY`, or `POLICY`. |
| PARAM_REJECTED | See Parameters. |
| PATH_PARAMS_UNSUPPORTED | A file-reference parameter was set. These are refused. |
| UNKNOWN_TYPE | Unknown component type. Check `component_types`. |
| UNKNOWN_PART, AMBIGUOUS_PART | Library lookup failed. `details.candidates` lists up to five matches; pass `type` to disambiguate. |
| NO_MEASUREMENT | `details.reason` is `NO_PATH` (no forward path between the points), `NO_VALID_POINTS` (the sweep has no valid points), or, from `budget_cascade`, `NOT_EVALUATED` (the budget or requirements are invalid, or the budget has no stages or outputs), `NOT_BUDGETED`, `NOT_CONFIGURED`, or `NOT_REACHED` (the message carries the unreached reason). |
| UNSAVED_CHANGES | The open project has unsaved changes, so `project_load` did not run. Save with `project_save` (with `path` if the project is untitled), or ask the user to save or discard the changes in RF Simulator. |
| INTERNAL | Unexpected failure, or a reply over the 1 MiB protocol limit. Re-read with `circuit_get`, then report it. |

## Projects

- `project_save {path?, overwrite?}` writes the open project to `path`, or to the file it already has when `path` is omitted. An untitled project needs `path`. `path` must be absolute, end in `.rfsim`, contain no byte below 0x20 and, on Windows, no `:` after its root name, not name a folder, and have an existing parent folder (`INVALID_ARGUMENT` at `/path`). A different existing project file needs `overwrite: true` (`INVALID_ARGUMENT` at `/overwrite`).
- Success returns `{epoch, path, name, copied_data_files}`. The array lists each project-relative file copied into `rf_data/` to preserve an external component reference; it is empty when no copy was needed. Existing destinations are reused only when they are regular non-symlink files with identical bytes. The save takes no checkpoint and changes neither `epoch` nor `revision`. The project becomes clean, so `UNSAVED_CHANGES` clears.
- GUI and agent saves both copy external `sparam_filepath`, legacy `sparam_path`, `amp_filepath`, and `ampf_filepath` data into `rf_data/`. Sources must be regular non-symlink files up to 256 MiB each; a save copies at most 256 unique files and 512 MiB total. Missing, non-regular, symlink, oversize, or colliding data files fail the save with `INTERNAL`; the prior `.rfsim` remains intact. Agents may write only the selected `.rfsim` and these save-triggered data copies, not arbitrary files.
- `project_load {epoch, path}` needs `epoch` from the latest read, or it returns `STALE_EPOCH`. `path` must be an absolute `.rfsim` file with no byte below 0x20 and, on Windows, no `:` after its root name (`INVALID_ARGUMENT` at `/path`). A missing file is `NOT_FOUND`, and a folder is `INVALID_ARGUMENT`.
- `project_load` returns `UNSAVED_CHANGES` without loading anything while the project has unsaved changes. Save first with `project_save`, or ask the user to save or discard the changes. After a test-flow latch, `project_save` is refused too, so ask the user.
- A file that cannot be parsed leaves the circuit as it was (`INVALID_ARGUMENT` at `/path`). A restore failure that already cleared the circuit returns `INTERNAL` at the new epoch, so call `circuit_get`.
- Success returns `{epoch, path, name}` at the new epoch. The load clears checkpoints, so `revert` cannot undo it, and the project is clean. Older epochs then return `STALE_EPOCH` with `details.cause` `opened_project`.
- A test-flow restore failure marks the project unsaved and latches `project_save` (`INTERNAL`) until RF Simulator replaces the project (open, new, revert, or a successful `project_load`). A GUI save clears the unsaved flag but not the latch, so after the user saves, `project_load` can replace the project.

## Measurements

- `measure_port` takes `at = {component, port}`; `port` is an output index, and one past the last output is `NOT_FOUND`. It recomputes the live circuit before reading, so a just-made edit is visible.
- Optional arguments: `max_tones` 1 to 32 (default 8), and `trace` with `start_Hz` below `stop_Hz` and `points` 2 to 401 (default 201).
- Result fields: `total_power_dBm`, `peak` (`freq_Hz`, `power_dBm`), `noise_floor_dBm_per_Hz`, `tones` (`freq_Hz`, `power_dBm`, `phase_deg`), `tone_count`, `snr_dB`, `snr_basis`, `fs_Hz`, `is_complex_baseband`, and `trace` (`frequencies_Hz` and `noise_dBm_per_Hz`, parallel arrays).
- `null` marks an unavailable or non-finite value. It is never zero.
- `snr_basis.kind` is `rbw` (with `rbw_Hz`) for analyzer SNR, or `pfb_channel` (with `enbw_Hz` and `channel_noise_dBm`) for PFB outputs.
- For real-domain signals, the reported single-sided tone power is 3.01 dB above the spectrum analyzer's displayed peak.

## Receiver requirements

- `requirements_get` takes no arguments. Both tools return `{epoch, revision, dirty, requirements, invalid_reason}`. `epoch` changes only when the project is replaced, so an edit never advances it; `revision` advances by one per applied edit; `dirty` is true while the project has unsaved changes.
- `requirements` is the format-3 object: `{format: 3, selected_mode, modes: [{id, name, config}]}`. `selected_mode` is a zero-based index (0 when there are no modes). `id` is the mode's stable identity: send it back unchanged to keep the mode, so a renamed mode keeps its id and its RF budget record; omit `id` to add a mode. A mode's `config` is `null` when the mode is unconfigured, otherwise `{band_start_hz, band_stop_hz, gain_min_db?, gain_max_db?, nf_max_db?, output_power_min_dbm?, output_power_max_dbm?, iip3_min_dbm?, snr?, measurement_conditions?, oob_suppression?}`. Band endpoints are required; gain and output-power minimum/maximum are sent in pairs; `measurement_conditions` holds `input_level_dbm?` (from -174 to 10 dBm) and `iip3?` (`tone_spacing_hz`, `input_start_dbm`, `input_stop_dbm`, `input_step_db`).
- `oob_suppression` is a list of up to 16 `{anchor, offset_hz, min_db}` points. `anchor` is `band_start` or `band_stop`; the resolved frequency must be positive and outside the band, `min_db` must be non-negative, and the mode needs gain limits. A budget verdict fails when the cascade gain at a point exceeds `gain_min_db - min_db`; the live Requirements panel ignores these points.
- `snr` is `{min_db, bandwidth_hz}`, with both numbers required; `bandwidth_hz` must be finite and positive. A budget verdict is worst case over the band: output SNR = `input_level_dbm` minus kTB at `bandwidth_hz` (290 K) minus the cumulative NF. The live Requirements panel does not measure SNR, so a mode with `snr` cannot show Pass there.
- An output-power or SNR limit without `measurement_conditions.input_level_dbm` stays Incomplete: the engine returns no output-power samples for it, and SNR has no signal level to reference. The GUI refuses to save either combination; the agent path does not.
- At most 64 modes. Names are compared ignoring case and must arrive already trimmed: leading or trailing spaces are refused (`Mode name must not have leading or trailing spaces.`), as are an empty name, a name longer than 64 characters, or a name containing `#`.
- When the project retained malformed requirement data, `invalid_reason` carries the diagnostic and `requirements` is the empty format-3 object. Fix the modes and send a complete valid object to clear it; there is no partial edit.
- `requirements_edit {epoch, revision?, requirements}` replaces the whole set. It checks, in order: `epoch` (`STALE_EPOCH`), unknown top-level keys, `revision` when given (a non-negative integer, else `INVALID_ARGUMENT` at `/revision`), `requirements` is an object (`INVALID_ARGUMENT` at `/requirements`), `format` is 2 or 3 (`INVALID_ARGUMENT` at `/requirements/format`), unknown keys at every nested level, including `next_mode_id` (`INVALID_ARGUMENT` at the full path, such as `/requirements/modes/0/config/gain_min_dB`), the mode ids (`INVALID_ARGUMENT` at `/requirements/modes/N/id`: an unknown id gives "no mode has id K; omit id to add a mode", a repeated id gives "id K is used twice"), the decoded set validates (`INVALID_ARGUMENT` at `/requirements` with the diagnostic as the message), then `revision` equals the current project revision (`BUSY` otherwise). Send the `revision` from `requirements_get`. A GUI mode selection does not advance `revision`, so it does not cause `BUSY`; the edit's `selected_mode` replaces it.
- An edit equal to the stored set returns success without opening a checkpoint, marking the project modified, or advancing `revision`. A different set is checkpointed as `receiver requirements` and can be reverted from the Agent panel. If the GUI refuses the write, the call returns `INTERNAL` "the host rejected the receiver requirements" and nothing changes; if the write throws, the checkpoint is committed as `interrupted: receiver requirements`.
- Both tools report `INTERNAL` "receiver requirements are unavailable" when the GUI provides no requirements state.

## RF budget

The budget predicts each requirements mode's cascade without touching the circuit. A Pass is a prediction from component models, never verification: confirm with `measure_port`, `network_analyzer_sweep`, or `receiver_measure`.

- `budget_get {}` takes no arguments. It returns `{epoch, revision, dirty, budget, invalid_reason, not_evaluated_reason, modes, orphaned_mode_ids}`.
  - `budget` is the format-1 object `{format: 1, point_count, stages, outputs, modes}`. When the stored budget is invalid, it is the empty format-1 object and `invalid_reason` carries the diagnostic. `not_evaluated_reason` explains why no mode was evaluated (invalid budget or requirements, or no stages).
  - `modes[]` is in requirements order: `{mode_id, name, status, outputs, unreached}`. `status` is `budgeted`, `not_budgeted` (no budget record), or `not_configured` (no receiver config). `outputs[]` is `{output, verdict}` for each reached output; `unreached[]` is `{output, reason}`.
  - `verdict` is `{overall, gain, noise_figure, output_power, iip3, snr, oob_suppression}`. Each metric is `{status, min, max}` with `status` in `not_configured`, `invalid_configuration`, `incomplete`, `pass`, `fail`; `min`/`max` are the observed extremes; `null` means no value and an infinite extreme is the string `"Infinity"` or `"-Infinity"`. `oob_suppression` reports the observed suppression below `gain_min_db` (suppression dB = `gain_min_db` minus cascade gain) across the mode's out-of-band points.
  - `orphaned_mode_ids` lists budget records whose mode no longer exists.
- `budget_cascade {mode_id, output, start_index?, count?}` is read-only and takes no epoch. `start_index` defaults to 0 and `count` to 50 (range 1–50). It returns `{epoch, mode_id, mode_name, output, point_count, start_index, next_index, frequencies_Hz, reasons, stages, verdict}`; `next_index` is the next page's first point, `null` on the last page. The point list is the in-band sweep plus each configured out-of-band suppression point at its exact frequency.
  - `stages[]` is `{stage, input_port, output_port}` plus nine series: `gain_db`, `nf_db`, `oip3_dbm`, `cumulative_gain_db`, `cumulative_nf_db`, `cumulative_iip3_dbm`, `level_dbm`, `nf_share_percent`, `p1db_headroom_db`. Each series is `{values, reasons}` with one entry per returned point; a value is a number, an infinite value is the string `"Infinity"` or `"-Infinity"`, and `null` means the point has no value. A `null` or infinite value has a non-zero index into `reasons`, index 0 is empty, and `reasons` is the whole interned table.
  - A finite cumulative gain below -100 dB gives `null` for `cumulative_gain_db`, `cumulative_nf_db`, `level_dbm`, and `p1db_headroom_db`, and its reason names the stage output. `cumulative_iip3_dbm` is not floored, and a later stage whose running gain is back above -100 dB has readouts again.
  - Verdicts use the unfloored values, so a chain below -100 dB still fails a gain minimum.
  - Checks, in order: argument shape (`INVALID_ARGUMENT` at the path; `count` 1–50, `start_index` ≥ 0); an evaluation that could not run (`NO_MEASUREMENT`, `details.reason` `NOT_EVALUATED`); unknown mode (`NOT_FOUND`); non-budgeted or unconfigured mode (`NO_MEASUREMENT`, `NOT_BUDGETED` or `NOT_CONFIGURED`); unknown output (`NOT_FOUND`); unreached output (`NO_MEASUREMENT`, `NOT_REACHED`, with the unreached reason as the message); `start_index` at or past `point_count` (`INVALID_ARGUMENT` at `/start_index`).
- `budget_edit {epoch, revision?, budget}` replaces the whole budget and answers with the `budget_get` shape. Send each mode record's `mode_id` back to keep it. Checks, in order: `epoch` (`STALE_EPOCH`); unknown top-level keys; `revision` when given (non-negative integer, else `INVALID_ARGUMENT` at `/revision`); `budget` is an object; `format` is 1 (`INVALID_ARGUMENT` at `/budget/format`, "expected 1"); unknown keys at every nested level except inside `params` and `overrides/<stage>`; the decoded budget validates (`INVALID_ARGUMENT` at `/budget`); a record `mode_id` that neither names a requirements mode nor an already stored record (`INVALID_ARGUMENT` at `/budget/modes/N/mode_id`, "no requirements mode has id K"); `checkRfBudgetParams` (`PARAM_REJECTED`, with `op_index` absent and `details.path` such as `/budget/stages/1/params`); host availability; then `revision` equality (`BUSY`).
  - An equal budget changes nothing. Otherwise the edit opens a checkpoint named `rf budget` and marks the project modified. A host that refuses the write returns `INTERNAL` "the host rejected the RF budget" with nothing applied.
  - A changing edit whose reply would not fit one 1 MiB line is refused before the checkpoint: `INVALID_ARGUMENT` at `/budget`, nothing stored. The check reserves 4 KiB for the request id, so a much longer id can still get `INTERNAL` after the commit; if so, re-read with `budget_get`.
  - A stage is `{name, type, source: "idealized"|"library", inputs}`; idealized stages also carry `params`, library stages a `part_number`. Each input feed is `null` (unfed), `{from: "input"}`, or `{from: "stage", stage, port}`. Outputs are `{name, stage, port}`. A mode record is `{mode_id, overrides}`, where `overrides` maps a stage name to bare parameter keys and values (the same runtime names as that stage's `params`) applied for that mode only, for example `{"ATT": {"att_dB": 6.0}}`. Runtime parameter names come from `component_types {type}`; existing records in `budget_get` show their shape.
  - Both budget tools report `INTERNAL` "the RF budget is unavailable" or "receiver requirements are unavailable" when the GUI provides no such state.

## network_analyzer_sweep

- Takes two output endpoints, `point_a` and `point_b`. Optional: `start_Hz`, `stop_Hz`, `points`, `stimulus_dBm`, and `arrays`. If you give both `start_Hz` and `stop_Hz`, `start_Hz` must be below `stop_Hz`. If you give only one, it is checked against the current value of the other. If `arrays` is given, `arrays.max_points` is required (2 to 401).
- Omitted settings keep their current values. The engine clamps `points`; `settings.points` shows the value used.
- A changed setting is recorded as a checkpoint the user can revert in the Agent panel. Repeating the same call changes nothing.
- Result: `summary` (`gain_dB` and `nf_dB`, each with `min`, `max`, `mean`, `at_start`, `at_center`, `at_stop`) and optional `arrays`.

## Library authoring

Use `component_types {type}` first. Its `library_fields` list registered keys and constraints; use them only when `authorable` is true and omit fields whose kind is `file_path`. `param_info` describes serialized engine state and can use different keys (`attenuation_dB` in a library definition versus `atten_dB` at runtime).

`library_create {type, part_number, parameters, manufacturer?, description?, notes?, scope?}` creates a new reusable JSON definition. `type` must be authorable. `parameters` may contain only registered non-file library fields and must pass required, type, enum, and range validation. `data_files` are refused.

```json
{
  "type": "attenuator",
  "part_number": "ATT-10DB",
  "manufacturer": "Example",
  "parameters": { "attenuation_dB": 10.0 },
  "scope": "project"
}
```

- `scope` is `project` by default or `global`. Project writes under `./rf-sim-libraries` relative to the simulator process's current working directory, the same root the app scans. Global writes under `$HOME/.rf-sim/libraries` (POSIX) or `%USERPROFILE%/.rf-sim/libraries` (Windows).
- Success returns `{epoch, scope, library_path, part_number, type, manufacturer}`. `library_path` is relative to the selected root. The completed JSON is indexed immediately, so `library_search` and `circuit_edit` placement can use it in the same session; future app scans load it from disk.
- Invalid definitions return `INVALID_ARGUMENT` with `details.path` and publish no definition. A duplicate type/part number or destination collision is refused; existing entries are never overwritten.
- Creation does not add or move a circuit instance, change circuit epoch/revision/dirty state, or create a checkpoint. Agent-panel checkpoint reverts cannot undo library creation.
- This tool cannot accept arbitrary paths, upload or copy assets, edit existing entries, or import/export `.rflib` files.

## Other tools

- `component_types {type?}`: registered types and their ports. With `type`, also `authorable`, `default_params`, runtime `param_info`, and `library_fields`; `library_create` accepts only non-file fields on authorable types.
- `library_search {query?, type?, limit?}`: case-insensitive substring match on part number, manufacturer, and description. `limit` is 1 to 50 (default 20); `total` counts every match.
- `data_file_read`: summarizes an S-parameter file; it takes no paths and never writes. Use exactly one route:
  - `{component, epoch}`: the file a component references. `epoch` is required and must match the current epoch, or the call returns `STALE_EPOCH`.
  - `{part_number, type?, epoch?}`: a library part. `type` applies only here. `epoch` is not compared with the current epoch; if supplied, it must be an unsigned integer (`INVALID_ARGUMENT` otherwise).
- `s_row` and `s_col` (0 to 3, defaults 1 and 0) and `max_points` (2 to 401, default 201) apply to both routes.
- `test_flow_run {epoch, conditions?, measure}`: sweeps up to four parameters (each `{component, path, values}`) and measures 1 to 8 metrics. A sweep of more than 1000 rows is refused with `INVALID_ARGUMENT`. The circuit is restored afterwards. Its `path` uses dotted, indexed syntax such as `tones[0].power_dBm`; `set_params` does not accept that syntax.
- `receiver_measure {epoch, point_a, point_b, start_Hz, stop_Hz, points?, output_power?, reference_tone_Hz?, iip3?}`: receiver output power and IIP3 across a sweep. Each call advances up to 4 ms, so repeat while `in_progress` is true. Result: `in_progress`, `frequencies_Hz`, `output_power_dBm`, and `iip3_dBm`; unavailable points are `null`. It is read-only.

## Limits

- You cannot open file dialogs or revert projects. `.rfsim` project files are read and written only by `project_save` and `project_load`; `library_create` writes only a validated JSON definition under an existing library root, and `data_file_read` reads only files that a component or library part already references.
- RF Simulator admits one agent client at a time.
- Each reply is capped at 1 MiB. An oversized result becomes `INTERNAL`. `budget_cascade` pages at most 50 points per call, so page through larger budgets. `budget_get` echoes the whole budget and cannot page, so an oversized budget reads back as `INTERNAL`. `budget_edit` refuses an edit whose reply would not fit, before anything is stored (see the `budget_edit` notes above).
