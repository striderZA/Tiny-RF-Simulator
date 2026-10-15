# Tests

## Purpose

Define binding rules for the Catch2 unit tests and ImGui Test Engine UI tests. The test inventory and target registration live in `tests/CMakeLists.txt` and `test_engine/CMakeLists.txt`.

## Ownership

- `tests/CMakeLists.txt` is the authoritative inventory of Catch2 test sources and standalone CTest targets; `test_engine/CMakeLists.txt` owns the `test_ui` target.
- Coverage is organized by DSP engines, graph and application behavior, serialization and integration, and UI workflows. See [CONTRIBUTING.md — Testing](../CONTRIBUTING.md#testing) for test commands and general guidance.

## Local Contracts

- Applicable parent DOX rules apply. Do not weaken them. Update the owning child DOX and its parent index when changing a child contract; keep index entries accurate.
- **MinGW registration ceiling:** put new `TEST_CASE`s in standalone CTest targets using `add_standalone_test` in `tests/CMakeLists.txt`, unless extending coverage already in the main `tests` target. The workflow enforces `MINGW_TEST_CASE_FLOOR` in `.github/workflows/release.yml`.
- **App/UI fixtures:** construct `RfSimulatorApp` only after ImGui, ImPlot, and ImNodes contexts exist. Pure DSP tests must not depend on UI or GLFW.
- **Scratch files:** make scratch paths unique across processes with `test_temp_paths::processTag()` from `tests/test_temp_paths.h` (plus a per-process counter when a test creates multiple files).
- **Shared state:** `RfSimulatorApp` writes the executable-relative `app.ini` when it is destroyed, so targets that construct it must be `RUN_SERIAL TRUE`, as must `test_ui` (`test_engine/CMakeLists.txt`). Tests that mutate `PROJECT_SOURCE_DIR/extensions` must be serialized too. Do not run concurrent CTest invocations against the same build tree.
- **Receiver mid-sweep assertions:** frame-count-dependent assertions must cap runs per update with `setMaxRunsPerUpdate()` and restore the prior/unlimited setting on every exit path; wall-clock update budgets are nondeterministic.
- **Test Flow boundary:** positional `id()` and serialized-key contracts belong to `test_flow/AGENTS.md`; keep them there.
- `test_external_tool_launch` is registered only under `if(NOT WIN32)` in `tests/CMakeLists.txt`.
- The `[receiver_measure]` error-path contract in `tests/test_agent_api.cpp` checks `/iip3`, `/start_Hz`, and `/epoch`; see `agent/api/src/tool_receiver.cpp` for implementation.
- **test_session_state_unicode.cpp** — Windows-only standalone `SessionState` persistence coverage under a unique Unicode temp directory: Unicode filesystem-path round-trip plus exact round-trip of a distinguishing non-ASCII legacy ACP value in a pre-existing ANSI `.ini` fixture; this pins legacy ACP bytes, not a UTF-8 setting-value contract; links only `common` and does not construct `RfSimulatorApp`
- App and serializer fixtures inspect live circuits through const `testGraphEngine()` / `testComponents()` views and set up topology/editor state through explicit app test commands; do not mutate the live graph or registry directly. Those commands forward to the editor's `EditorCommands`, so fixture setup is a real project edit that advances the revision and dirties the project (accepted probe add/remove included; a refused one changes nothing): to prove an operation recorded no edit, compare `projectRevision()` before and after (non-vacuous even from a dirty baseline) rather than asserting a clean `isDirty()`.
- `test_agent_api` stays app/UI-free: its `ApiFixture` uses `CircuitRuntime`, `EditorCommands`, analyzer engines, and a test `IAgentHost`; scratch clones must use `ComponentTypeRegistry`, never a handwritten engine factory.
- Platform-specific tests are gated in `tests/CMakeLists.txt` with `if(WIN32)` or `if(NOT WIN32)`, for example `test_session_state_unicode` and `test_external_tool_launch`.

## Work Guidance

- Follow [CONTRIBUTING.md — Testing](../CONTRIBUTING.md#testing) for test commands and general coverage guidance.
- `test_ui` shares `<exe_dir>/layouts/UITestLayout.ini`; `test_engine/CMakeLists.txt` registers `add_test(NAME test_ui COMMAND test_ui)` and sets `RUN_SERIAL TRUE`. `test_ui` is not defined in `tests/CMakeLists.txt`; run the whole `test_ui` target, not individual UI cases.
- `perf_app` (`test_engine/perf_main.cpp`) is opt-in: `cmake --build build --target perf_app`. It is declared `EXCLUDE_FROM_ALL` and is not a CTest target.
- Matched perf runs: `cmake --build build --target perf_app`, then `build/bin/perf_app <scenario>` from the repo root on each tag. Delete `build/bin/app.ini` first so both trees start from defaults, and interleave tags.
- Every DSP engine must have at least a basic correctness test and a dirty/clean benchmark
- Node graph tests must verify probe routing, link topology, and topological sort
- Touchstone parser tests must cover all format variants (DB, MA, RI) and frequency units
- AMP parser tests must cover scalar and per-frequency IP3 sections, default and explicit units, empty or non-numeric S/Y/Z/POUT sections, and file:line diagnostics (`tests/test_amp_file.cpp`)

## Verification

Focused CTest targets confirmed in `tests/CMakeLists.txt`:

- `ctest --test-dir build -R '^test_flow_boundary$' --output-on-failure`
- `ctest --test-dir build -R '^test_issue174_parameter_keys$' --output-on-failure`
- `ctest --test-dir build -R '^test_path_containment$' --output-on-failure` — project path containment and component-library asset checks.
- `ctest --test-dir build -R '^test_external_project_data$' --output-on-failure` — external RF data save/reload, atomic failure, and MCP result.
- `ctest --test-dir build -R '^test_sparam_legacy_key$' --output-on-failure`
- `ctest --test-dir build -R '^test_agent_api_project$' --output-on-failure`
- `ctest --test-dir build -R '^test_agent_project$' --output-on-failure`
- `ctest --test-dir build -R '^test_external_tool_launch$' --output-on-failure` (non-Windows only)
- `ctest --test-dir build -R '^test_ui$' --output-on-failure` (run the whole target)
- `test_editor_services_boundary` is a standalone CTest that verifies the public `simulator::editor_services` target exposes no ImGui, ImNodes, ImPlot, or GLFW include paths and supports a real editor command edit
- `ctest --test-dir build -j8` must also pass with zero failures; start it with a clean `extensions/` and never run a second `ctest` against the same build tree at the same time (see Local Contracts)

Use `ctest --test-dir build --output-on-failure` for the full suite; this command is documented in `CONTRIBUTING.md`.

## Child DOX Index

No child `AGENTS.md` files. `tests/flows` contains fixture data, not DOX.
