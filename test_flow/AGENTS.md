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

- This library must not link `simulator::app`, ImGui, implot, or imnodes. It takes
  `std::span<IComponentEngine *const>` rather than `ComponentRegistry`, which lives in `app/`.
- Components are addressed by `IComponentEngine::id()`, never by graph node id.
- Condition `path`s address the target engine's **`serialize()` keys**, not inspector field keys
  (`atten_dB`, not `attenuation_dB`).
- Condition `path`s are dot-separated keys with optional zero-based array indices, e.g. `gain_dB` or
  `tones[0].power_dBm`.
- Patches are type-preserving: an integer slot requires an integral value.
- Non-finite metric values encode as JSON `null`; `valid` distinguishes a measurement from a
  failure.
- A fatal flow error yields `ok = false` with zero rows.

## Work Guidance

- Add a metric: one function in `src/flow_metrics.cpp` returning `NaN` when not computable, plus one
  `m_defs.emplace(...)` registration.
- Add a condition capability: extend the path grammar and patch in `src/flow_params.cpp`.
- Flow fixtures live in `tests/flows/`; the circuit a flow addresses is built by the test in C++.

## Verification

- `ctest --test-dir build -R test_issue87_flow --output-on-failure`

## Child DOX Index

No child docs.
