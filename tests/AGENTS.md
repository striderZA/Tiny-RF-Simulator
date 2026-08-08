# Tests - AGENTS.md

## Purpose

Own the Catch2 v3.4.0 unit test suite and ImGui test engine UI tests. Verify all DSP engines, node graph operations, touchstone parser, and application components produce correct results. Prevent regressions across the modular build.

## Ownership

- **test_main.cpp** — Catch2 test runner (Catch2::Catch2WithMain linked)
- **test_*.cpp** — Per-module test files, one per engine/component under test
- **test_bench_dsp.cpp** — Dirty/clean performance benchmarks for DSP engines
- **test_signal_domain.cpp** — Standalone executable for cross-engine `Spectrum` propagation tests (`is_complex_baseband`, `fs_Hz`) and post-ADC chain integration tests (ADC → mixer/amp → PFB) (spans modules, so it doesn't fit the one-file-per-component pattern)
- **CMakeLists.txt** — Links all simulator module targets; test files listed explicitly, plus several standalone executables (see below)

## Local Contracts

- All tests use Catch2 v3 `TEST_CASE` / `SECTION` macros
- Floating-point comparisons use `Catch::Approx` from `<catch2/catch_approx.hpp>`
- Benchmark cases use `Catch::Benchmark::BENCHMARK` from `<catch2/benchmark/catch_benchmark_all.hpp>`
- Test files are named `test_<component>.cpp` matching the module name
- Build via `cmake --build build && ctest --test-dir build` or direct `build/bin/tests`
- Adding a new module? Add its test source to `TEST_SOURCES` in `CMakeLists.txt` and link the library target
- **MinGW-w64 test-registration ceiling:** this toolchain silently drops any `TEST_CASE` registered beyond the ~217 already linked into the main `tests` executable (confirmed via a from-scratch clean rebuild; see the comment above `test_component_authoring` in `CMakeLists.txt`). Do not add new `TEST_CASE`s to `test_main.cpp` or any file already compiled into the `tests` target — give the new coverage its own standalone executable instead (`add_executable(test_<name> test_<name>.cpp)` + `target_link_libraries` + `add_test`, following `test_attenuator`/`test_combiner`/`test_component_authoring`/`test_extensions`/`test_signal_domain`), and run it directly (`build/bin/test_<name>.exe`) rather than relying on `ctest`.
- Platform-specific tests (e.g., Windows-only session state) are gated with `#ifdef WIN32` in CMakeLists.txt
- `test_component_authoring` and `test_tutorial_state` are standalone executables, not part of `TEST_SOURCES`: the MinGW-w64 toolchain silently drops `TEST_CASE`s registered beyond the ~217 already linked into `tests`. New test files that must run on Windows should follow that pattern.

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
