# test_flow — AGENTS.md

## Purpose

Own the GUI-free test-flow (automated test / ATP) harness: load a machine-readable flow file that
sweeps simulator parameters, run the addressed circuit, and capture measurements from selected
component output ports.

## Ownership

- `include/flow_types.h` — `FlowSpec`, `Condition`, `Measurement`, `FlowError`/`FlowErrorCode`
- `include/flow_params.h` — `applyConditionValue()`: type-preserving write into a component's
  `serialize()` snapshot
- `include/flow_metrics.h` — `MetricSample`, `MetricDefinition`, `MetricRegistry` (four built-ins)
- `include/flow_result.h` — `ConditionValue`, `FlowRow`, `FlowResult`, `toJson()`
- `include/flow_runner.h` — `LoadFlowFile()`, `RunFlow()`
- `CMakeLists.txt` — `simulator::test_flow` STATIC target

## Local Contracts

- Flow files are JSON objects: `version` (required, must be `1`), optional `name` (defaults to the file stem), optional `conditions[]`, and required `measure[]`. A condition is `{component, path, values}`; a measurement is `{component, port, metric}`. `component` is an `IComponentEngine::id()`, never a graph node id.
- Execution runs the cartesian product of every condition's `values`; one `FlowRow` is emitted per combination, and each row records the applied condition values plus every measurement reading.
- This library must not link `simulator::app`, ImGui, implot, or imnodes. It takes
  `std::span<IComponentEngine *const>` rather than `ComponentRegistry`, which lives in `app/`.
- Condition `path`s address the target engine's **`serialize()` keys**, not inspector field keys
  (`atten_dB`, not `attenuation_dB`).
- Condition `path`s are dot-separated keys with optional zero-based array indices, e.g. `gain_dB` or
  `tones[0].power_dBm`.
- Patches are type-preserving: a signed integer slot requires an integral value; an unsigned slot
  requires a finite, non-negative, integral value and stays unsigned; a float slot requires a finite
  value. Boolean, string, null, object and array slots are rejected.
- Built-in metrics: `power_dBm` (total power, the same measurement the GUI power meter reports),
  `peak_power_dBm` and `peak_freq_Hz` (strongest tone), and `noise_floor_dBm_per_Hz` (mean noise
  density). A metric returns `NaN` when the input is not measurable.
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
- Flow fixtures live in `tests/flows/`; the circuit a flow addresses is built by the test in C++.

## Verification

- `ctest --test-dir build -R test_issue87_flow --output-on-failure`

## Child DOX Index

No child docs.
