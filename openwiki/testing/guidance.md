---
type: Engineering testing guide
title: Testing Guide
description: Explains the Catch2, standalone, UI, integration, and GUI-free Test Flow harnesses. Use it to choose focused regression coverage, isolate shared state, and validate tag-triggered releases.
tags: [testing, catch2, ui-testing, integration-testing, regression-testing, test-flow]
sources:
  - id: openwiki-source-4d1d392666be6dfdd7a91a2e
    resource: repo://.github/workflows/release.yml
  - id: openwiki-source-d7b5e27be67d03af04c422d5
    resource: repo://agent/api/include/agent_api.h
  - id: openwiki-source-e7932f8366579c2ce8c1865d
    resource: repo://app/src/test_flow_widget.cpp
  - id: openwiki-source-3ce882f1e6c92c2ec4ddc6c9
    resource: repo://common/session_state.h
  - id: openwiki-source-f317ee207e1653d2033c81a4
    resource: repo://CONTRIBUTING.md
  - id: openwiki-source-6236844d67c4b6a4f4573508
    resource: repo://layout/src/layout_manager.cpp
  - id: openwiki-source-8c357fec0d6783d0809a624a
    resource: repo://test_engine/CMakeLists.txt
  - id: openwiki-source-f23e5c266721037d2ad036be
    resource: repo://test_flow/CMakeLists.txt
  - id: openwiki-source-1f397e27ef8b0dc1f4f49d51
    resource: repo://test_flow/include/flow_boundary.h
  - id: openwiki-source-3d3e3b78daf6b95b69159369
    resource: repo://test_flow/include/flow_runner.h
  - id: openwiki-source-7cf82ec60f61d2c0858950cf
    resource: repo://test_flow/src/flow_boundary.cpp
  - id: openwiki-source-1425d7c9bb71c8e6d4145122
    resource: repo://test_flow/src/flow_runner.cpp
  - id: openwiki-source-5063b6aa8934c32dd8a94ee1
    resource: repo://tests/AGENTS.md
  - id: openwiki-source-fa68239bf614d837d7e5522c
    resource: repo://tests/CMakeLists.txt
  - id: openwiki-source-798feeba9e1cc5b47be50e7a
    resource: repo://tests/test_flow_boundary.cpp
  - id: openwiki-source-fb26cd54f157859706d14c13
    resource: repo://tests/test_issue87_flow.cpp
  - id: openwiki-source-86241b4bc592c332461c936b
    resource: repo://tests/test_temp_paths.h
  - id: openwiki-source-59642b0a96e98716b082cc11
    resource: repo://tests/test_test_flow_widget.cpp
  - id: openwiki-source-08f846c8582718824d718b09
    resource: repo://tutorial/src/tutorial_state.cpp
generated: { by: "omp", at: "2026-10-10T18:57:17.727Z" }
verified:
  - by: openwiki/0.7.0
    at: 2026-10-10T18:57:17.727Z
---

# Testing Guide

The repository has two test harnesses with different ownership boundaries:

- **Catch2 v3** tests DSP engines, serialization, graph behavior, application integration, security boundaries, and the Test Flow library. The main `tests` target is supplemented by standalone Catch2 executables.
- **imgui_test_engine** drives the application UI through `test_ui`. It verifies visible windows, menus, gestures, and panel interactions; it is not a substitute for testing the model or engine behind a panel.

CTest discovers the main Catch2 cases and standalone targets from `tests/CMakeLists.txt`. Rebuild before running tests so CMake discovery and executables reflect the current registration.

## Test-flow ownership

`test_flow` is an independent GUI-free library for flow schema, condition-path/value validation, metrics, results, sweep execution, and the shared run boundary `RunFlowWithinBoundary()`; it does not link the application or ImGui. Flow conditions use `IComponentEngine::id()` and engine `serialize()` keys. The app's `TestFlowWidget` authors and previews flow specs using the harness's `ValidateFlow()`, applies the UI row budget before a synchronous run, and then calls the shared boundary, which snapshots every live component, runs the flow, restores each snapshot, and rewires inputs. The agent's `test_flow_run` tool calls the same boundary. The graph topology is not mutated. A restoration failure clears results and latches the panel until circuit reload.

```mermaid
flowchart TD
    file["Flow JSON"] --> loader["test_flow LoadFlowFile"]
    loader --> validate["ValidateFlow"]
    validate -->|valid| panel["TestFlowWidget preview and Run"]
    validate -->|errors| displayError["Panel shows harness wording"]
    panel --> boundary["RunFlowWithinBoundary"]
    boundary --> snapshot["Snapshot every live engine"]
    snapshot --> runner["RunFlow"]
    runner --> restore["Restore each snapshot and rewire inputs"]
    restore --> outcome["Rows, run failure, or restore failure"]
    outcome --> display["Panel table and JSON export"]
```

*The harness owns flow semantics, measurements, and the shared run boundary; the panel owns authoring, UI limits, presentation, and the restore-failure latch.*

Flow conditions use dot-separated paths and optional zero-based array indices such as `tones[0].power_dBm`; they address serialized engine state, not inspector labels. `component` values are positional `IComponentEngine::id()` values, not graph node IDs. Deleting or reordering components can silently redirect a later flow reference, so revalidate flows after circuit edits. Built-in metrics are `power_dBm`, `peak_power_dBm`, `peak_freq_Hz`, and `noise_floor_dBm_per_Hz`; unavailable readings are invalid/NaN in the model and JSON `null` in results.

`ValidateFlow()` exhaustively checks every candidate value and measurement reference in the same order and wording used by `RunFlow()`. It resolves each condition slot once, then checks type, finiteness, integral-ness, and range without mutating the circuit. `RunFlow()` refuses invalid specs before producing rows and starts each sweep row from the targeted components' baseline snapshots. The widget refuses a cartesian sweep above `kMaxRunRows` before execution. `RunFlowWithinBoundary()` snapshots every live engine first, and a snapshot failure refuses the run. It then runs `RunFlow()`, restores each snapshot independently by id so one failure never stops later restores, and rewires graph inputs whatever the outcome. A restore failure outranks the run's own outcome and discards its result. Successful restoration leaves serialized component state and graph links unchanged. A restore failure is distinct from an ordinary run failure and requires circuit reload.

## Running the suite

```bash
cmake --build build
ctest --test-dir build --output-on-failure

# Main Catch2 binary and tag filters
build/bin/tests
build/bin/tests [sparam]
build/bin/tests [edge]
build/bin/tests [bench]

# Focused Test Flow harness, widget, and shared run boundary
ctest --test-dir build -R 'test_issue87_flow|test_test_flow_widget|test_flow_boundary' --output-on-failure

# UI tests (a display server is required)
build/bin/test_ui
# Headless Linux
xvfb-run --auto-servernum build/bin/test_ui
```

`test_ui` registers all ImGui cases without an argv filter, so a panel-menu case cannot be selected like a Catch2 case. Run the whole target for UI behavior. The Test Flow model and harness remain filterable through `test_test_flow_widget`, `test_issue87_flow`, and `test_flow_boundary`.

## Current Catch2 inventory

### Main `tests` target

`tests/CMakeLists.txt` currently compiles these sources into `tests` (with `test_session_state.cpp` additionally on Windows):

`test_main.cpp`, `test_node_graph_engine.cpp`, `test_touchstone.cpp`, `test_adc.cpp`, `test_pfb.cpp`, `test_bench_dsp.cpp`, `test_bench_groups.cpp`, `test_ideal_filter.cpp`, `test_component_registry.cpp`, `test_component_library.cpp`, `test_coax_cable_presets.cpp`, `test_coax_cable_engine.cpp`, `test_group.cpp`, `test_project_file.cpp`, `test_amplifier_sparam.cpp`, `test_ideal_filter_sparam.cpp`, `test_equalizer.cpp`, `test_iq_plot.cpp`, `test_nonlinear_p1db.cpp`, `test_amplifier_p1db.cpp`, and `test_layout_manager.cpp`.

These cover core DSP and caching, node topology and probes, Touchstone formats and errors, ADC/DDC behavior, PFB routing and noise, ideal filters and S-parameter modes, component registry/library instantiation, coax/presets, groups, project round trips, amplifier and nonlinear/P1dB behavior, equalizer and IQ plotting, layout paths, and benchmarks. `test_project_file.cpp` is the broad application serialization integration point: it covers links, groups, parameter values, S-parameter and Network Analyzer state, stale-probe clearing, invalid JSON, and new-project behavior.

### Standalone Catch2 targets

Each name below is an independently registered CTest target. The source is the same-named `tests/test_*.cpp` file unless noted otherwise.

- `test_attenuator`, `test_combiner`, `test_rf_switch`, and `test_rf_switch_2to1`: engine behavior, routing, noise, clamping, serialization, hover summaries, and orientation-specific multi-output behavior.
- `test_rf_switch_project`: SPDT project type lookup, serialization, and restoration of a link on the second switch output.
- `test_network_analyzer`: isolated-chain stimulus, gain/NF, non-perturbing probes, path ambiguity/disconnection, mixer translation, clamping, serialization, widget drawing, and probe points.
- `test_power_meter` and `test_power_meter_app`: engine and app integration.
- `test_component_authoring`, `test_component_dispatch`, `test_issue79_component_validation`: registry descriptors, authoring validation/round trips, menu dispatch, all registered component types, legacy type strings, load/upsert rejection, deserialize rollback, and path containment.
- `test_extensions`, `test_issue45_extension_trust`, `test_issue80_extension_hardening`, and `test_issue130_extension_menu_labels`: manifest parsing/discovery, trust persistence and fail-closed approval, duplicate shadowing, workspace isolation and containment, result limits, and unique menu labels.
- `test_issue37_pfb_input_removal`, `test_issue70_pfb_reconnect`, `test_issue42_multi_output`, `test_issue78_multi_output`, and `test_pfb_sampling_ratio`: dangling-input cleanup, reconnect behavior, correct OUT2 routing/probing and round trips, pin reporting, and sampling-ratio/output-grid/noise contracts.
- `test_signal_domain`, `test_signal_generator_noise`, `test_adc_configuration`, `test_pfb_filter_design`, `test_pfb_calculator`, and `test_issue117_numeric_correctness`: cross-engine signal-domain propagation, source-versus-added noise, ADC configuration, filter design/calculator selection, and numeric regressions including signed-frequency, stale-noise, dirty propagation, combiner validation, and S-parameter latching.
- `test_path_containment`: project/library S-parameter containment, Touchstone size and point limits, and safe component-authoring data-file names/copy destinations.
- `test_issue48_json_loader`: malformed project/library JSON is isolated: valid siblings survive, wrong-shaped sections are rejected, malformed probes/groups are skipped, integer bounds are enforced, component rollback occurs, and scanning continues after malformed files.
- `test_tutorial_state`: marker persistence, catalog addressing, inactive-before-start, and bounded navigation.
- `test_issue87_flow`: flow loading/validation, type-preserving condition writes, authoring helpers, metric math, JSON encoding, atomic flow-file writes, rollback, and shared link policy.
- `test_issue77_save_failure` and `test_issue113_project_load`: failed saves preserve dirty state/path and atomicity; failed loads clear unsafe save targets, reject malformed state shapes, and preserve the original file on save failure.
- `test_issue116_collapsed_groups`: first-frame collapsed-group placement, links through both group boundaries, and rejection of duplicate-input and cyclic links.
- `test_spectrum_jitter`, `test_spectrum_analyzer_snr`, and `test_node_hover_snr`: stable tone peaks despite display noise, analyzer SNR guards/read-only behavior, and node-hover first-output SNR/data fallback.
- `test_test_flow_widget`: the app panel's run outcomes through the shared boundary (whole-circuit restoration observed through the panel, ordinary and latched failures, reload recovery), validation and row limits, authoring controls, preview, export, and bounded rendering.
- `test_flow_boundary`: the UI-free shared run boundary (`RunFlowWithinBoundary()`) used by the Test Flow panel and the agent tool. A flow without conditions measures one row and leaves the circuit as it was; a restore failure is reported while later engines are still restored; an execution failure is reported and the circuit is still restored; a snapshot failure refuses the run before anything executes. Links `simulator::test_flow`, `simulator::editor_services`, and `common`.

- `test_circuit_runtime`, `test_graph_editor_actions`, and `test_editor_commands`: UI-free circuit lifecycle and link policy, probe/group mutation, revision-based dirty semantics, rejected-command behavior, and component-view synchronization.
- `test_receiver_requirements`, `test_receiver_performance_measurements`, and `test_receiver_requirements_project`: requirement validation/status, real-engine gain/NF/output-power/IIP3 measurement, partial/cache behavior, persistence, and New/reset behavior.

This inventory is intentionally derived from current CMake registration rather than a historical executable count. New targets must be registered with `add_standalone_test`; otherwise CTest will not run them.

## Isolation and temporary state

Parallel CTest is useful but not universally safe; use the isolation rules below.

The following shared resources require care:

- Scratch paths must be process-unique because `catch_discover_tests` can launch separate cases as separate processes; `tests/test_temp_paths.h` provides a process-id tag for filenames.
- `SessionState` writes the exe-relative `app.ini` on Windows and is a no-op elsewhere. `LayoutManager` and `TutorialState` use exe-relative files on all supported platforms. Extension tests also mutate the shared source-tree `extensions/` root.
- CMake marks `test_ui` (in `test_engine/CMakeLists.txt`) and `test_network_analyzer`, `test_extensions`, `test_issue45_extension_trust`, `test_issue113_project_load`, `test_project_json`, `test_node_hover_snr`, `test_test_flow_widget`, `test_editor_commands`, `test_receiver_requirements_project`, and `test_agent_app` (in `tests/CMakeLists.txt`) `RUN_SERIAL` for shared executable-relative state, fixtures, or `app.ini`. This serializes only one CTest invocation; do not run two CTest processes against the same build tree. Start extension tests with a clean `extensions/` directory.
- UI tests need a display. Linux CI supplies Xvfb; Windows release CI excludes `test_ui`. The ASan job excludes benchmarks and UI; Windows also checks the main-binary case count against the workflow's MinGW registration floor.

## Choosing coverage for a change

Pair the smallest test that exercises the changed ownership boundary with an integration/regression test when state crosses boundaries:

| Change | First focused coverage |
|---|---|
| DSP engine math, noise, phase, filtering, dirty flags | engine test or `test_issue117_numeric_correctness`; cover nominal, zero/negative/non-finite input, clamping, and clean-vs-dirty updates |
| Serialization, project load/save, component registry or library | `test_project_file`, `test_component_dispatch`, `test_issue48_json_loader`, `test_issue79_component_validation`, or issue #77/#113; assert round trips, malformed input, rollback, and dirty/path invariants |
| Runtime topology and editor commands | `test_circuit_runtime`, `test_graph_editor_actions`, `test_editor_commands`, and node/group regression tests; assert link policy, pointer cleanup, accepted/rejected revisions, and view synchronization |
| Receiver requirement measurement or persistence | `test_receiver_requirements`, `test_receiver_performance_measurements`, `test_receiver_requirements_project`; cover validation/status, real-engine measurement, partial results, caching, and project round trips |
| Extensions, external tools, paths, or trust | `test_extensions` plus the focused #45/#80/#130/path-containment targets; use unique temp roots and test refusal as well as success |
| Test Flow grammar, metrics, authoring, or sweep execution | `test_issue87_flow`; include invalid references, every sweep value, metric-unavailable results, atomic writes, and rollback |
| Test Flow run, snapshot, restore, or rewire behavior | `test_flow_boundary` for snapshot refusal, restore continuation, and execution-failure restoration; then `test_test_flow_widget` for how the panel shows and latches the outcome |
| Test Flow panel, preview, export, or state lifecycle | `test_test_flow_widget`; add a UI case only for visible interaction, and run `test_ui` for menu/window wiring |
| UI display, menus, gestures, layout, tutorial, or tooltips | `test_engine/ui_tests.cpp` through `test_ui`, with model/data assertions in a standalone Catch2 test where ImGui text is not queryable |

Do not put ImGui/GLFW dependencies into pure engine tests. Do not duplicate harness validation rules in the panel: call `ValidateFlow()` and preserve its wording. For new coverage, prefer a standalone target because MinGW-w64 can silently drop registrations appended beyond the main `tests` executable's ceiling; verify the target appears in CTest and run it directly when platform discovery is suspect.

## CI and local verification

Pull requests run no automated CI. Release-tag validation uses CMake/Ninja; Linux tests run under Xvfb excluding benchmarks, Windows tests exclude `test_ui`, and the ASan pass excludes both benchmarks and UI. The `strict-build` matrix runs one Linux GCC 14 Debug leg for patch tags and the four-leg Linux GCC Debug/Release, Clang 18, and MinGW matrix for minor/major tags. Every tag also tests the optimized Release package builds; these are the shipped configurations. The workflow checks the `MINGW_TEST_CASE_FLOOR` (currently 223) and creates a draft only after required jobs pass.

Before submitting a change:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -j8 --output-on-failure   # only with clean extensions/ and one CTest invocation
ctest --test-dir build -R 'test_issue87_flow|test_test_flow_widget|test_flow_boundary' --output-on-failure
```

For a new test: add the source/target to `tests/CMakeLists.txt`, use stable process-unique temporary paths, keep app fixtures behind the ImGui context fixture, choose descriptive tags, and confirm CTest discovery. For engine, serialization, graph, extension/path, or UI changes, the focused tests should fail before the fix and pass after it; that is the regression signal that matters more than a stale total count.
