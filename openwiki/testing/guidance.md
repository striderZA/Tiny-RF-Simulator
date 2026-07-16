---
type: Testing Guide
title: Testing Guide
description: Guide to the RF Simulator test suite — how to run tests, test structure, writing new tests, CI configuration, and coverage priorities.
tags: [testing, catch2, unit-tests, ui-tests]
---

# Testing Guide

RF Simulator has **~188 test cases** (including 14 benchmarks) across **21 test source files**, covering all DSP engines, the node graph, touchstone parser, PFB channelizer, amplifier nonlinear model, project save/load, subcircuits, and UI. Two additional standalone test executables cover the attenuator and combiner engines. The test suite uses **two frameworks**: Catch2 for unit/benchmark tests and **imgui_test_engine** for UI interaction tests.

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

These test files are compiled into the main `tests` executable (17 files; 18 on Windows with `test_session_state.cpp`). Two additional standalone executables — `test_attenuator` and `test_combiner` — are built separately because they link only specific engine libraries.

| Test File | Tags | What It Tests |
|---|---|---|
| `test_main.cpp` | `[common]`, `[generator]`, `[splitter]`, `[mixer]`, `[amplifier]`, `[phase]` | Core math utils, generator, splitter, mixer, basic amplifier |
| `test_node_graph_engine.cpp` | `[node_graph]`, `[appearance]` | Topology, linking, probes, `nodeKindFromLabel`, `themeColor` |
| `test_touchstone.cpp` | `[touchstone]` | .sNp parser: real files, synthetic files, error cases |
| `test_adc.cpp` | `[adc]` | ADC DDC, aliasing, NSD noise, Fs clamping |
| `test_pfb.cpp` | `[pfb]` | PFB channel routing, noise distribution, two outputs, flatness |
| `test_ideal_filter.cpp` | `[filter]`, `[edge]` | LPF/HPF/BPF/BSF, exact cutoff, noise, dirty flags |
| `test_ideal_filter_sparam.cpp` | `[filter]`, `[sparam]` | Filter S-param mode |
| `test_amplifier_sparam.cpp` | `[amp]`, `[sparam]`, `[nf]`, `[nonlinear]` | Amp S-param mode, NF, nonlinearity |
| `test_component_registry.cpp` | `[registry]` | ComponentRegistry add/find/remove |
| `test_coax_cable_engine.cpp` | `[coax]`, `[datasheet]`, `[noise]`, `[phase]`, `[connectors]`, `[edge]`, `[caching]` | Coax loss, phase, noise, connectors, clamping, caching |
| `test_coax_cable_presets.cpp` | `[coax]`, `[presets]` | Cable preset table validation |
| `test_equalizer.cpp` | `[equalizer]`, `[sparam]` | Equalizer ideal mode, S-param mode, NaN guards |
| `test_group.cpp` | `[group]`, `[integration]` | Group operations, boundary pins, signal flow through groups |
| `test_iq_plot.cpp` | `[iq_plot]` | `build_iq_spectrum` IFFT, Parseval, empty/degenerate grids |
| `test_project_file.cpp` | `[project_file]` | Save/load round-trip: empty project, linked components, newProject, parameter values, groups, invalid JSON |
| `test_session_state.cpp` | `[session]` | Windows-only: INI save/load round-trip |
| `test_bench_dsp.cpp` | `[bench]`, `[generator]`, `[amplifier]`, `[mixer]`, `[splitter]`, `[pfb]`, `[spectrum]` | Per-engine dirty/clean benchmarks |
| `test_bench_groups.cpp` | `[benchmark]`, `[group]` | Group operation benchmarks |

**Standalone executables:**

| Test File | Executable | What It Tests |
|---|---|---|
| `test_attenuator.cpp` | `test_attenuator` | Pass-through, flat attenuation, passive noise model, noise floor convergence, S-param, clamping, dirty-flag, hover |
| `test_combiner.cpp` | `test_combiner` | Basic combination, single/both inputs, dirty-flag, S-param mode |

### UI Tests (`test_engine/`)

**Build target:** `test_ui` (links against `imgui_test_engine`).

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
| `properties_window_exists` | Properties window is focusable |

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

From `.github/workflows/build.yml`:

```yaml
# CI runs on Linux (GCC 14) and Windows (MinGW-w64 via MSYS2)
# Excludes:
#   - Benchmark tests (too variable in CI)
#   - test_ui (needs X display — Xvfb not configured)
#   - "ADC DDC output grid spans" (Catch2 test concatenation quirk on Windows)
ctest --test-dir build --output-on-fire -E "Benchmark|test_ui|ADC DDC output grid spans"
```

The UI test engine requires a display server. For headless Linux CI, set up Xvfb:

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
