# Tests - AGENTS.md

## Purpose

Own the Catch2 v3.4.0 unit test suite and ImGui test engine UI tests. Verify all DSP engines, node graph operations, touchstone parser, and application components produce correct results. Prevent regressions across the modular build.

## Ownership

- **test_main.cpp** — Catch2 test runner (Catch2::Catch2WithMain linked)
- **test_*.cpp** — Per-module test files, one per engine/component under test
- **test_bench_dsp.cpp** — Dirty/clean performance benchmarks for DSP engines
- **test_signal_domain.cpp** — cross-engine `Spectrum` propagation tests (`is_complex_baseband`, `fs_Hz`) and post-ADC chain integration tests (ADC → mixer/amp → PFB) (spans modules, so it doesn't fit the one-file-per-component pattern)
- **CMakeLists.txt** — Links all simulator module targets; test files listed explicitly, plus several standalone executables (see below)

## Local Contracts

- All tests use Catch2 v3 `TEST_CASE` / `SECTION` macros
- Floating-point comparisons use `Catch::Approx` from `<catch2/catch_approx.hpp>`
- Benchmark cases use `Catch::Benchmark::BENCHMARK` from `<catch2/benchmark/catch_benchmark_all.hpp>`
- Test files are named `test_<component>.cpp` matching the module name
- Build via `cmake --build build && ctest --test-dir build` or direct `build/bin/tests`
- Adding a new module? Add its test source to `TEST_SOURCES` in `CMakeLists.txt` and link the library target
- **MinGW-w64 test-registration ceiling:** this toolchain silently drops any `TEST_CASE` registered beyond the ~223 already linked into the main `tests` executable (verified 2026-08-09; the release.yml Windows job enforces the 223 floor with a `--list-tests` count guard). Do not add new `TEST_CASE`s to `test_main.cpp` or any file already compiled into the `tests` target — give the new coverage its own standalone executable instead (`add_executable(test_<name> test_<name>.cpp)` + `target_link_libraries` + `add_test`, following `test_attenuator`, `test_combiner`, `test_network_analyzer`, `test_component_authoring`, `test_tutorial_state`, `test_extensions`, `test_issue37_pfb_input_removal`, `test_issue70_pfb_reconnect`, `test_issue42_multi_output`, `test_component_dispatch`, `test_signal_domain`, `test_path_containment`, and `test_issue48_json_loader`), and run it directly (`build/bin/test_<name>.exe`) rather than relying on `ctest`. `test_issue48_json_loader` covers malformed project/library input via `TEST_CASE`s in its own standalone executable, outside the main `tests` registration ceiling, so it must be run directly (`build/bin/test_issue48_json_loader.exe`).
- Platform-specific tests (e.g., Windows-only session state) are gated with `#ifdef WIN32` in CMakeLists.txt

## Work Guidance

- Every DSP engine must have at least a basic correctness test and a dirty/clean benchmark
- Test both nominal and edge cases: zero inputs, max values, parameter extremes
- Node graph tests must verify probe routing, link topology, and topological sort
- Touchstone parser tests must cover all format variants (DB, MA, RI) and frequency units
- Do not depend on ImGui or GLFW in pure DSP-engine tests; app-integration tests may create ImGui, ImPlot, and ImNodes contexts to construct `RfSimulatorApp` safely
- App-level tests may include `simulator::app` for integration scenarios

## Verification

- `cmake --build build && ctest --test-dir build` must pass with zero failures
- `build/bin/tests [bench]` must run benchmarks without crashing
- New test files appear in CTest discovery (`catch_discover_tests`)

## Child DOX Index

No child docs. All test artifacts live at this level.
