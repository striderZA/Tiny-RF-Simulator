# test_flow — AGENTS.md

## Purpose

Own the GUI-free test-flow (automated test / ATP) harness: load a machine-readable flow file that
sweeps simulator parameters, run the addressed circuit, and capture measurements from selected
component output ports.

## Ownership

- `include/flow_types.h` — `FlowSpec`, `Condition`, `Measurement`, `FlowError`/`FlowErrorCode`
- `include/flow_params.h` — `applyConditionValue()`: type-preserving write into a component's
  `serialize()` snapshot, plus `resolveConditionSlot()`/`conditionSlotAccepts()` — the same path and
  value rules split so a sweep can be checked by resolving the slot once instead of writing per value
  — and `describeConditionPaths()`, the discovery pass an authoring UI offers instead of making the
  author guess `atten_dB` from the inspector's `attenuation_dB`
- `include/flow_author.h` — `buildFlowDocument()` (the document `LoadFlowFile()` reads back),
  `parseConditionValues()`/`formatConditionValues()` (the value text field's grammar and its inverse),
  `writeFlowFile()` (atomic pretty-JSON write)
- `include/flow_metrics.h` — `MetricSample`, `MetricDefinition`, `MetricRegistry` (four built-ins)
- `include/flow_result.h` — `ConditionValue`, `FlowRow`, `FlowResult`, `toJson()`
- `include/flow_runner.h` — `LoadFlowFile()`, `ValidateFlow()`, `RunFlow()`
- `CMakeLists.txt` — `simulator::test_flow` STATIC target

## Local Contracts

- Flow files are JSON objects: `version` (required, must be `1`), optional `name` (defaults to the file stem), optional `conditions[]`, and required `measure[]`. A condition is `{component, path, values}`; a measurement is `{component, port, metric}`. `component` is an `IComponentEngine::id()`, never a graph node id.
- **Addressing is positional and volatile.** A load re-creates components in saved order from the counter's base of 100 (`ProjectSerializer::reset()`), so ids assigned in save order survive a clean save/load, but any deletion or reorder shifts every later id — and because ids stay dense, a stale flow reference can then *silently bind to a different component* (often the same type, so the path still applies) instead of failing. Only `ValidateFlow()`/`RunFlow()`'s resolution decides; a flow file is not portable across a circuit edit until flows address something stable.
- `ValidateFlow(spec, components)` is the single pre-flight: every condition's component and path/value compatibility, then every measurement's component, output port and metric, as a `FlowError` list in the order `RunFlow()` checks them. `RunFlow()` runs this same pass and reports the first issue verbatim, so an attached UI can never call a flow runnable that the harness refuses. It strictly reads the circuit.
- `ValidateFlow()` is exhaustive — every value of every condition, not a sample — and stays affordable per frame because it resolves each condition's `path` once with `resolveConditionSlot()` and then checks the values arithmetically with `conditionSlotAccepts()`, with no JSON write per candidate.
- Execution runs the cartesian product of every condition's `values`; one `FlowRow` is emitted per combination, and each row records the applied condition values plus every measurement reading. Neither the loader nor the runner caps the row count, and the product is executed synchronously, so a caller that faces a user must bound it itself (the app panel refuses above `TestFlowWidget::kMaxRunRows`).
- This library must not link `simulator::app`, ImGui, implot, or imnodes. It takes
  `std::span<IComponentEngine *const>` rather than `ComponentRegistry`, which lives in `app/`.
- Condition `path`s address the target engine's **`serialize()` keys**, not inspector field keys
  (`atten_dB`, not `attenuation_dB`).
- Condition `path`s are dot-separated keys with optional zero-based array indices, e.g. `gain_dB` or
  `tones[0].power_dBm`.
- Patches are type-preserving: a signed integer slot requires an integral value; an unsigned slot
  requires a finite, non-negative, integral value and stays unsigned; a float slot requires a finite
  value. Boolean, string, null, object and array slots are rejected. `conditionSlotAccepts()` is
  exactly these value rules, which is what lets the pre-flight check a value without writing it.
- Built-in metrics: `power_dBm` (total power, the same measurement the GUI power meter reports),
  `peak_power_dBm` and `peak_freq_Hz` (strongest tone), and `noise_floor_dBm_per_Hz` (mean noise
  density). A metric returns `NaN` when the input is not measurable.
- `describeConditionPaths(snapshot)` lists every scalar leaf of a snapshot in nlohmann's sorted key
  order, containers descended and never listed (`tones` yields `tones[0].freq_Hz`; an empty container
  yields nothing), so a picker built on it cannot offer a container, a missing key or a non-numeric
  slot as if it were sweepable. Non-numeric leaves are still listed with `numeric == false` and their
  JSON type, because "the key exists but cannot be swept" is the answer the author needs; `numeric`
  entries resolve to the same `ConditionSlotKind` they report, and writing an entry's own `value` back
  at its `path` is a no-op.
- `buildFlowDocument(spec)` writes the document `LoadFlowFile()` reads back — `version` 1, `name`
  only when non-empty (absent means the file stem), `conditions` only when there is one, and `measure`
  always, even when empty so the loader's own "must not be empty" rule is what answers. Ids and paths
  are copied from `spec` verbatim: nothing is invented, so a caller must have taken them from the
  live circuit (`id()` and `describeConditionPaths()`).
- `writeFlowFile()` mirrors `ProjectSerializer::save()`'s atomic discipline (issue #113): pretty JSON
  plus a trailing newline into a sibling `<path>.tmp`, renamed over the target only after write,
  flush and close all succeed, so a failed write never truncates a hand-authored flow and never leaves
  the temp file behind.
- `parseConditionValues()` accepts numbers separated by commas, semicolons or whitespace and rejects
  (rather than skips) a token that is not a complete finite number, and an empty list;
  `formatConditionValues()` is its inverse in `std::to_chars`' shortest round-trip form, so editing
  one value of a list cannot perturb the value the author did not touch.
- Non-finite metric values encode as JSON `null`; `valid` distinguishes a measurement from a
  failure.
- A fatal flow error yields `ok = false` with zero rows, and the loaded spec is reset — a failed load
  never leaks partially parsed conditions.
- `RunFlow()` converts component `deserialize()` exceptions into `DeserializeFailed`, attempts to restore
  targeted baseline snapshots, reports rollback failure details, and returns no rows.
- `test_flow` is one of the mirrored format-check directory lists in `scripts/format.sh`,
  `.githooks/pre-commit`, and `.github/workflows/release.yml`; keep all three in lockstep when
  directories are added or removed.

## Work Guidance

- Add a metric: one function in `src/flow_metrics.cpp` returning `NaN` when not computable, plus one
  `m_defs.emplace(...)` registration.
- Add a condition capability: extend the path grammar and patch in `src/flow_params.cpp`.
- Add an authoring aid (issue #155): a UI-free helper in `src/flow_author.cpp` (or
  `src/flow_params.cpp` when it is path/slot rule work) with its own unit test here, and let the
  widget in `app/` drive it. The panel must not carry a second copy of the document shape, the value
  grammar or the applicability rules: it calls `ValidateFlow()` for every verdict it shows.
- Flow fixtures live in `tests/flows/`; the circuit a flow addresses is built by the test in C++.

## Verification

- `ctest --test-dir build -R test_issue87_flow --output-on-failure` — the harness, the pre-flight and
  the authoring helpers (`[issue155]` runs just the authoring aids).

## Child DOX Index

No child docs.
