---
type: Testing Guide
title: Testing Guide
description: Guide to the RF Simulator test suite — how to run tests, test structure, writing new tests, CI configuration, and coverage priorities.
tags: [testing, catch2, unit-tests, ui-tests]
---

# Testing Guide

RF Simulator has **~300 test cases** (including 14 benchmarks) across **30 test source files** (21 compiled into the main `tests` executable — 22 on Windows with `test_session_state.cpp` — plus **9 standalone executables**), covering all DSP engines, the node graph, touchstone parser, PFB channelizer, amplifier nonlinear model, P1dB, component library, project save/load, subcircuits, extensions, the guided tutorial, and UI. The test suite uses **two frameworks**: Catch2 for unit/benchmark tests and **imgui_test_engine** for UI interaction tests.

---

## Running Tests

```bash
# All unit tests (excluding benchmarks and UI)
cmake --build build
ctest --test-dir build --output-on-failure

# Benchmarks only
build/bin/tests [bench]

# Specific tag
build/bin/tests [sparam]
build/bin/tests [edge]
build/bin/tests [amplifier]

# UI tests (requires display, may need Xvfb on headless Linux)
build/bin/test_ui
```

---

## Test Structure

### Catch2 Unit Tests (`tests/`)

**Build target:** `tests` (links against `Catch2::Catch2WithMain`).

These test files are compiled into the main `tests` executable (21 files; 22 on Windows with `test_session_state.cpp`). Nine standalone executables are built separately: `test_attenuator` and `test_combiner` link only specific engine libraries; the newer ones link `simulator::app` or `simulator::tutorial` (and were kept out of the main `tests` binary because this project's MinGW-w64 toolchain silently drops TEST_CASEs registered beyond the ~217 already linked into `tests.exe`).

| Test File | Tags | What It Tests |
|---|---|---|
| `test_main.cpp` | `[common]`, `[generator]`, `[splitter]`, `[mixer]`, `[amplifier]`, `[phase]` | Core math utils, generator, splitter, mixer, basic amplifier |
| `test_node_graph_engine.cpp` | `[node_graph]`, `[appearance]` | Topology, linking, probes, `themeColor` (label→`NodeKind` mapping is covered by `test_component_dispatch`, see standalone executables) |
| `test_touchstone.cpp` | `[touchstone]` | .sNp parser: real files, synthetic files, error cases |
| `test_adc.cpp` | `[adc]` | ADC DDC, aliasing, NSD noise, Fs clamping |
| `test_nonlinear_p1db.cpp` | `[nonlinear]`, `[p1db]` | NonlinearModel P1dB default, setter, OIP3 derivation |
| `test_pfb.cpp` | `[pfb]` | PFB channel routing, noise distribution, two outputs, flatness |
| `test_ideal_filter.cpp` | `[filter]`, `[edge]` | LPF/HPF/BPF/BSF, exact cutoff, noise, dirty flags |
| `test_ideal_filter_sparam.cpp` | `[filter]`, `[sparam]` | Filter S-param mode |
| `test_amplifier_p1db.cpp` | `[amplifier]`, `[p1db]` | P1dB default, setter, serialize/deserialize |
| `test_amplifier_sparam.cpp` | `[amp]`, `[sparam]`, `[nf]`, `[nonlinear]` | Amp S-param mode, NF, nonlinearity |
| `test_component_library.cpp` | `[library]` | JSON loading, directory scanning, instantiation for 7 component types, part number |
| `test_component_registry.cpp` | `[registry]` | ComponentRegistry add/find/remove |
| `test_coax_cable_engine.cpp` | `[coax]`, `[datasheet]`, `[noise]`, `[phase]`, `[connectors]`, `[edge]`, `[caching]` | Coax loss, phase, noise, connectors, clamping, caching |
| `test_coax_cable_presets.cpp` | `[coax]`, `[presets]` | Cable preset table validation |
| `test_equalizer.cpp` | `[equalizer]`, `[sparam]` | Equalizer ideal mode, S-param mode, NaN guards |
| `test_group.cpp` | `[group]`, `[integration]` | Group operations, boundary pins, signal flow through groups |
| `test_iq_plot.cpp` | `[iq_plot]` | `build_iq_spectrum` IFFT, Parseval, empty/degenerate grids |
| `test_layout_manager.cpp` | `[layout]` | Layout path derivation, name sanitization, named-preset save/load |
| `test_project_file.cpp` | `[project_file]` | Save/load round-trip: empty project, linked components, newProject, parameter values, groups, invalid JSON |
| `test_session_state.cpp` | `[session]` | Windows-only: INI save/load round-trip |
| `test_bench_dsp.cpp` | `[bench]`, `[generator]`, `[amplifier]`, `[mixer]`, `[splitter]`, `[pfb]`, `[spectrum]` | Per-engine dirty/clean benchmarks |
| `test_bench_groups.cpp` | `[benchmark]`, `[group]` | Group operation benchmarks |

**Standalone executables:**

| Test File | Executable | What It Tests |
|---|---|---|
| `test_attenuator.cpp` | `test_attenuator` | Pass-through, flat attenuation, passive noise model, noise floor convergence, S-param, clamping, dirty-flag, hover |
| `test_combiner.cpp` | `test_combiner` | Basic combination, single/both inputs, dirty-flag, S-param mode |
| `test_component_authoring.cpp` | `test_component_authoring` | ComponentTypeRegistry descriptors, ComponentLibrary validate, ComponentFormModel build/validate/round-trip |
| `test_extensions.cpp` | `test_extensions` | Extension manifest parsing/rejection, discovery across built-in/global/project-local roots, ExternalToolRunner request/result flow |
| `test_issue37_pfb_input_removal.cpp` | `test_issue37_pfb_input_removal` | Issue #37 regression: removing an upstream node immediately nulls downstream dangling input pointers |
| `test_component_dispatch.cpp` | `test_component_dispatch` | Registry-driven dispatch: menu add marks project dirty, `kindForLabel` label→NodeKind mapping, all 11 types round-trip through save/load, legacy `.rfsim` type strings backward compat |
| `test_issue42_multi_output.cpp` | `test_issue42_multi_output` | Issue #42 regression: Splitter OUT2 routes to Combiner IN1 via `outputs[1]`; probing Splitter/PFB OUT2 resolves output index 1 |
| `test_signal_domain.cpp` | `test_signal_domain` | `is_complex_baseband` defaults and propagation through every engine, `conjugateSymmetricExpand` expansion |
| `test_tutorial_state.cpp` | `test_tutorial_state` | TutorialState marker path derivation, completed/markCompleted round-trip, catalog non-empty and addressable, inactive until started, navigation stays within bounds |

### UI Tests (`test_engine/`)

**Build target:** `test_ui` (links against `imgui_test_engine`).

The UI test suite uses a **test helper library** (`test_engine/test_helpers.h` / `test_engine/test_helpers.cpp`) that provides reusable helpers:

- `NodeHelper` — `addComponent()` (canvas context menu → click menu item), `selectNode()` (set imnodes selection), `deleteSelectedNode()` (press Delete), `findComponentNodeId<T>()`
- `InspectorHelper` — `waitForPopulated()`, `clickButton()`, `toggleCheckbox()`, `setInputDouble()`, `selectComboItem()`

| Test Name | What It Tests |
|---|---|
| `node_editor_exists` | Node Editor window is focusable |
| `single_generator_present` | Default Generator 0 exists with Measure item |
| `single_amplifier_present` | Default Amplifier 0 exists with Measure item |
| `canvas_context_menu` | Right-click on canvas opens context menu |
| `node_context_menu` | Right-click on node opens context menu |
| `properties_window_exists` | Properties window is focusable |
| `subcircuit_rubber_band_creates_group` | Shift-drag creates subcircuit group |
| `subcircuit_create_group_and_verify_popup` | Second group creation |
| `subcircuit_expand_and_collapse` | Expand/collapse rendering |
| `inspector_amplifier_gain` | Set Gain (dB) on default amplifier via Inspector panel, verify engine state |
| `inspector_amplifier_nf` | Set NF (dB) on default amplifier via Inspector panel, verify engine state |
| `connection_valid_generator_to_amplifier` | Programmatic add/remove a valid link |
| `connection_multi_fanout` | Programmatic fanout from one output to two inputs |
| `connection_delete` | Programmatic link add then remove |
| `connection_output_to_output_accepted` | Documents accepted output→output (validation gap — no validation) |
| `connection_input_to_input_accepted` | Documents accepted input→input (validation gap — no validation) |
| `connection_self_loop_accepted` | Documents accepted self-loop (validation gap — no validation) |
| `connection_duplicate_accepted` | Documents accepted duplicate links (validation gap — no deduplication) |
| `navigation_pan_programmatic` | Programmatic pan offset via imnodes API |
| `navigation_drag_node` | Programmatic node drag via imnodes API + mouse click to flush cache |
| `layout_save_as_creates_file` | View > Layouts Save As writes a named preset file |
| `layout_manage_delete_removes_file` | View > Layouts Manage deletes a preset file |
| `tutorial_launches_from_help_menu` | Help > Tutorial starts the walkthrough, shows "Tutorial Guide"; Exit deactivates |
| `tutorial_start_guards_unsaved_changes` | Dirty project: tutorial waits behind the Unsaved Changes modal (Discard then starts) |
| `tutorial_step_navigation` | Next/Back/Skip navigation stays within bounds; Skip jumps to last step without finishing |
| `tutorial_completes_and_persists` | Walking to the last step + Finish writes the `.tutorial_completed` marker (not before Finish) |
| `tutorial_first_run_prompt_marks_completed` | Dismissing the first-run "Welcome" offer ("Not Now") marks completed so it never nags again |

---

## Test Patterns

### Floating-Point Comparisons

Use `Catch::Approx` from `<catch2/catch_approx.hpp>`:

```cpp
REQUIRE(result == Catch::Approx(expected).margin(1e-12));
```

### DSP Engine Test Pattern

```cpp
TEST_CASE("AmplifierEngine applies gain", "[amplifier]") {
    auto gen = std::make_unique<SignalGeneratorEngine>(...);
    auto amp = std::make_unique<AmplifierEngine>(...);

    gen->update(0.0);
    amp->node().inputs[0] = &gen->node().outputs[0];
    amp->update(0.0);

    // Verify output
    auto& out = amp->node().outputs[0];
    REQUIRE(out.tones[0].power_dBm == Catch::Approx(expectedPower).margin(0.01));
}
```

### Testing S-Param Mode

```cpp
TEST_CASE("Amplifier S-param applies S21 gain", "[amp][sparam]") {
    auto amp = std::make_unique<AmplifierEngine>(...);
    amp->setSParamFilepath(PROJECT_SOURCE_DIR "/component_data/amp.s2p");

    // Connect generator
    amp->node().inputs[0] = &gen->node().outputs[0];
    amp->update(0.0);

    REQUIRE(amp->node().outputs[0].tones[0].power_dBm ==
            Catch::Approx(expected).margin(0.5));
}
```

### Benchmark Pattern

```cpp
TEST_CASE("AmplifierEngine dirty/clean benchmark", "[bench][amplifier]") {
    auto amp = makeAmplifier();

    BENCHMARK("AmplifierEngine update (dirty)") {
        amp->update(0.0);
    };

    BENCHMARK("AmplifierEngine update (clean)") {
        amp->update(0.0);
    };
}
```

---

## CI Test Configuration

Tests run in `.github/workflows/release.yml` on minor/major release tags:

```yaml
# Linux (GCC/Clang, Debug/Release): full suite under Xvfb
xvfb-run --auto-servernum ctest --test-dir build --output-on-failure -E "Benchmark"

# Windows (MinGW-w64): UI tests need a display; ADC DDC grid spans test excluded
ctest --test-dir build --output-on-failure -E "Benchmark|test_ui|ADC DDC output grid spans"

# AddressSanitizer job (Linux):
cd build
ASAN_OPTIONS=halt_on_error=1:detect_leaks=0 ctest --output-on-failure -E "Benchmark|test_ui"
```

The UI test engine requires a display server. For headless Linux CI, Xvfb is used (`xvfb-run --auto-servernum`). Locally, run:

```bash
xvfb-run build/bin/test_ui
```

---

## Writing Tests

### Adding a New Test

1. Create `tests/test_<component>.cpp`.
2. Add the file to `tests/CMakeLists.txt`.
3. Choose descriptive tags: `[component]`, `[feature]` (e.g., `[coax][phase]`).
4. Use meaningful section names that describe the scenario.

### Test Coverage Priorities

| Priority | What to Test |
|---|---|
| P0 | Core DSP correctness (gain, noise, phase, filtering) |
| P0 | Edge cases (empty input, zero freq, negative freq, NaN, clamping) |
| P1 | S-param mode (loading, S21 interpolation, out-of-band) |
| P1 | Dirty-flag caching |
| P2 | Parameter clamping and validation |
| P2 | Signal chain integration (multiple components connected) |

### Things to Watch For

- **NaN guarding:** `log10(0)` must never reach math functions. Test with zero-frequency tones.
- **Generation bumps:** Every parameter change must increment `outputs[0].generation`.
- **Clamping:** Negative length (coax), frequencies below 1 Hz (equalizer ref freq), NF < 0 dB.
- **S-param file errors:** Missing file, bad format, out-of-range frequency interpolation.
- **Multi-output components:** Test both `outputs[0]` and `outputs[1]` for Splitter, PFB.
