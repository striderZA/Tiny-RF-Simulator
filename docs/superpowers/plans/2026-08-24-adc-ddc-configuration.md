# Configurable RF ADC DDC Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add user-configurable power-of-two ADC DDC decimation and signed NCO tuning as a factor of the ADC input sample rate while preserving current ADC behavior for existing projects.

**Architecture:** Keep the existing `AdcEngine` frequency-domain model and ideal brick-wall filter. Add two validated engine parameters, derive the complex output grid and `Spectrum::fs_Hz` from `Fs/D`, and retain the current alias → NCO shift → coherent tone accumulation flow. Wire the parameters through the existing inspector, registry, component-library definitions, and JSON serialization paths without introducing a reusable DDC abstraction.

**Tech Stack:** C++20, CMake/Ninja, Catch2 v3, nlohmann/json, ImGui, existing `simulator::*` targets.

## Global Constraints

- `decimation` supports exactly `1`, `2`, `4`, and `8`; compatibility default is `2`.
- `nco_fs_fraction` is a signed factor of the ADC input `Fs`, accepted in the closed interval `[-0.5, +0.5]`; compatibility default is `0.25`.
- Positive NCO tuning shifts positive sampled frequencies toward DC.
- Output sample rate is exactly `Fs / decimation`.
- Output passband is `[-Fs/(2*decimation), Fs/(2*decimation))` and output remains complex baseband.
- Existing JSON files missing the new fields must retain current behavior (`decimation=2`, `nco_fs_fraction=0.25`).
- Noise vectors remain PSD in W/Hz; do not multiply PSD by an invented process-gain factor.
- Keep `Spectrum::is_complex_baseband` set only by `AdcEngine`; pass-through propagation remains unchanged.
- Do not add FIR/filter-ripple modeling, `IQStream` processing, or PFB changes.
- New test coverage must remain outside the main `tests` executable if needed to avoid the MinGW Catch2 registration ceiling.

---

## File Map

- Modify `adc/include/adc_engine.h`: expose validated decimation/NCO accessors and store the two parameters.
- Modify `adc/src/adc_engine.cpp`: generalize DDC mapping, output grid, noise lookup, empty-output metadata, and JSON persistence.
- Modify `app/src/inspector_panel.cpp`: draw the decimation combo and normalized NCO input.
- Modify `app/src/component_type_registry.cpp`: expose both ADC parameters to authoring/validation metadata.
- Modify `component_data/library/adcs/analog-devices/ad9226.json`: record compatibility DDC parameters.
- Modify `component_data/library/adcs/ti/adc12d1600.json`: record compatibility DDC parameters.
- Create `tests/test_adc_configuration.cpp`: standalone focused ADC DSP, persistence, and validation tests.
- Modify `tests/CMakeLists.txt`: register `test_adc_configuration` with `add_standalone_test`.
- Modify `tests/test_signal_domain.cpp`: verify configurable ADC output `fs_Hz` reaches the existing ADC → PFB chain.
- Modify `AGENTS.md`: record the durable ADC DDC parameter contract in the project user preferences.

## Interfaces Produced by Task 1

The implementation uses these exact `AdcEngine` methods:

```cpp
int decimation() const;
void setDecimation(int decimation);
double ncoFsFraction() const;
void setNcoFsFraction(double fraction);
```

`serialize()` writes `decimation` and `nco_fs_fraction`. `deserialize()` accepts those keys when present and applies compatibility defaults when absent.

---

### Task 1: Add failing standalone ADC configuration tests

**Files:**
- Create: `tests/test_adc_configuration.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: the `AdcEngine` methods defined above; existing `Spectrum`, `NodeGraphEngine`, and Catch2 conventions.
- Produces: executable `test_adc_configuration` and behavioral tests that fail until the engine implementation exists.

- [ ] **Step 1: Create the test fixture and rate/grid tests**

Create `tests/test_adc_configuration.cpp` with the existing Catch2 includes and a helper that creates a frequency grid over `0..Fs/2` with initialized noise vectors. Add these tests:

```cpp
TEST_CASE("ADC decimation derives output rate and complex grid", "[adc][configuration]") {
    NodeGraphEngine graph;
    Spectrum input = makeInput(101, 0.0, 500e6);

    for (int d : {1, 2, 4, 8}) {
        AdcEngine adc(d, graph);
        adc.setFs_Hz(Fs);
        adc.setDecimation(d);
        adc.setNcoFsFraction(0.0);
        adc.node().inputs[0] = &input;
        adc.update(0.0);

        const auto &out = adc.node().outputs[0];
        REQUIRE(out.fs_Hz == Approx(Fs / d));
        REQUIRE(out.frequencies.front() == Approx(-Fs / (2.0 * d)).margin(1.0));
        REQUIRE(out.frequencies.back() < Fs / (2.0 * d));
        REQUIRE(out.is_complex_baseband);
    }
}
```

Use distinct IDs if the fixture requires them. Keep the grid assertions tolerant of the existing `ceil`-based bin count.

- [ ] **Step 2: Add NCO, combined filtering, empty metadata, and validation tests**

Add focused cases with the following observable behavior:

```cpp
TEST_CASE("ADC NCO factor tunes a positive tone to DC", "[adc][configuration]") {
    NodeGraphEngine graph;
    AdcEngine adc(10, graph);
    adc.setFs_Hz(Fs);
    adc.setDecimation(4);
    adc.setNcoFsFraction(0.125);

    Spectrum input = makeInput(101, 0.0, 500e6);
    input.is_complex_baseband = true;
    input.tones.push_back({125e6, -20.0, 12.0});
    adc.node().inputs[0] = &input;
    adc.update(0.0);

    REQUIRE(adc.node().outputs[0].tones.size() == 1);
    REQUIRE(adc.node().outputs[0].tones[0].freq_Hz == Approx(0.0).margin(1.0));
    REQUIRE(adc.node().outputs[0].tones[0].phase_deg == Approx(12.0));
}

TEST_CASE("ADC NCO and decimation reject tones outside output Nyquist", "[adc][configuration]") {
    NodeGraphEngine graph;
    AdcEngine adc(11, graph);
    adc.setFs_Hz(Fs);
    adc.setDecimation(4);
    adc.setNcoFsFraction(0.125);

    Spectrum input = makeInput(101, 0.0, 500e6);
    input.is_complex_baseband = true;
    input.tones.push_back({300e6, -20.0, 0.0});
    adc.node().inputs[0] = &input;
    adc.update(0.0);

    REQUIRE(adc.node().outputs[0].tones.empty());
}

TEST_CASE("ADC empty input preserves configured output sample rate", "[adc][configuration]") {
    NodeGraphEngine graph;
    AdcEngine adc(12, graph);
    adc.setFs_Hz(Fs);
    adc.setDecimation(8);
    adc.node().inputs[0] = nullptr;
    adc.update(0.0);
    REQUIRE(adc.node().outputs[0].fs_Hz == Approx(Fs / 8.0));
}
```

Also add tests that `setDecimation()` accepts only the four supported values, NCO `-0.5` and `+0.5` are accepted, and invalid JSON values clamp to the nearest valid decimation and NCO endpoint.

- [ ] **Step 3: Add persistence and legacy JSON tests**

Add a round-trip test that sets `Fs=1e9`, NSD `-150`, decimation `8`, and NCO `-0.125`, serializes, deserializes into a second engine, and asserts all four values. Add a legacy test using only:

```cpp
nlohmann::json legacy = {
    {"sample_rate_Hz", 1e9},
    {"nsd_dBm_per_Hz", -155.0},
};
```

Assert that the new engine values are `2` and `0.25`. Add a malformed-value test with `decimation=6` and `nco_fs_fraction=0.7`; assert the documented normalized values (`4` and `0.5`).

- [ ] **Step 4: Register the standalone target**

Append this to `tests/CMakeLists.txt` after the other standalone pure-DSP tests:

```cmake
add_standalone_test(test_adc_configuration
    SOURCES test_adc_configuration.cpp
    LIBS simulator::adc_engine simulator::node_graph_engine
)
```

- [ ] **Step 5: Run the new test target to confirm the expected failure**

Run:

```bash
cmake --build build --target test_adc_configuration
ctest --test-dir build -R '^test_adc_configuration$' --output-on-failure
```

Expected: compilation fails because the new accessors do not yet exist, or the test fails on the first unimplemented behavior. Do not proceed with implementation if the test is accidentally passing without the new code.

- [ ] **Step 6: Commit the test scaffold**

```bash
git add tests/test_adc_configuration.cpp tests/CMakeLists.txt
git commit -m "test: define configurable ADC DDC behavior"
```

---

### Task 2: Implement configurable ADC DDC semantics

**Files:**
- Modify: `adc/include/adc_engine.h`
- Modify: `adc/src/adc_engine.cpp`

**Interfaces:**
- Consumes: failing tests from Task 1 and existing `alias_frequency()`, `map_tones_to_complex()`, `beginUpdate()`, and `Spectrum` contracts.
- Produces: validated setters, generalized DDC mapping, derived output rate/grid, and JSON compatibility behavior.

- [ ] **Step 1: Add parameters and setters**

Add private members with compatibility defaults:

```cpp
double m_nco_fs_fraction = 0.25;
int m_decimation = 2;
```

Add the four public methods from the interface contract. `setNcoFsFraction()` clamps to `[-0.5, 0.5]` and marks `m_dirty` when changed. `setDecimation()` maps the requested integer to the nearest of `{1,2,4,8}`, resolving equal-distance ties downward, and marks `m_dirty` when changed.

- [ ] **Step 2: Generalize tone mapping**

Change `map_tones_to_complex()` to accept `Fs`, `decimation`, and `nco_fs_fraction`. Compute:

```cpp
const double nco_Hz = nco_fs_fraction * Fs;
const double output_edge = Fs / (2.0 * decimation);
const double f_complex = alias_frequency(tone.freq_Hz, Fs) - nco_Hz;
```

Retain only `[-output_edge, output_edge)`. Preserve real-tone conjugate expansion, complex-input non-expansion, coherent accumulation, phase, and the current physical half-power behavior.

- [ ] **Step 3: Derive output grid and metadata**

In `update()` replace the fixed `/2` and `/4` values with `decimation` and `output_edge`:

```cpp
const double output_fs = m_fs_Hz / static_cast<double>(m_decimation);
const double output_edge = output_fs / 2.0;

N = std::max(2, static_cast<int>(std::ceil(output_fs / df_in)));
df_out = output_fs / N;
out.frequencies[i] = -output_edge + i * df_out;
out.fs_Hz = output_fs;
out.is_complex_baseband = true;
```

The empty-input path must set `out.fs_Hz = output_fs` and `out.is_complex_baseband = true` before bumping generation.

- [ ] **Step 4: Generalize noise-bin lookup without changing PSD semantics**

For each output frequency, derive the sampled input lookup from the output frequency plus the NCO frequency, wrap it with `alias_frequency()`, and use the existing nearest-input-bin search. Keep `noise_W`, `noise_added_W`, and `noise_total_W` in W/Hz. Do not multiply the PSD by `10*log10(D)` or any other process-gain factor.

- [ ] **Step 5: Extend JSON persistence with validation**

Serialize:

```cpp
return {
    {"sample_rate_Hz", m_fs_Hz},
    {"nsd_dBm_per_Hz", m_nsd_dBm_per_Hz},
    {"decimation", m_decimation},
    {"nco_fs_fraction", m_nco_fs_fraction},
};
```

In `deserialize()`, preserve the existing sample-rate and NSD reads. Read missing new fields as `2` and `0.25`, then route values through the setters so clamping and dirty tracking are identical to UI edits.

- [ ] **Step 6: Run the focused ADC test until it passes**

Run:

```bash
cmake --build build --target test_adc_configuration
ctest --test-dir build -R '^test_adc_configuration$' --output-on-failure
```

Expected: all new ADC configuration tests pass.

- [ ] **Step 7: Commit the engine implementation**

```bash
git add adc/include/adc_engine.h adc/src/adc_engine.cpp
git commit -m "feat: configure ADC DDC tuning and decimation"
```

---

### Task 3: Wire the inspector and component authoring metadata

**Files:**
- Modify: `app/src/inspector_panel.cpp`
- Modify: `app/src/component_type_registry.cpp`
- Modify: `tests/test_component_authoring.cpp`

**Interfaces:**
- Consumes: the four `AdcEngine` accessors/setters from Task 2 and existing `utils::inputDouble()`/ImGui patterns.
- Produces: editable ADC controls and registry metadata used by component authoring/validation.

- [ ] **Step 1: Add registry fields**

Extend the ADC descriptor after the existing NSD field with optional numeric fields:

```cpp
{"decimation", "DDC Decimation", "", FieldKind::Number, false, 1.0, 8.0, {}, {}, ""},
{"nco_fs_fraction", "NCO (×Fs)", "", FieldKind::Number, false, -0.5, 0.5, {}, {}, ""},
```

Keep them optional so old library definitions remain valid; the engine setter enforces the discrete decimation choices.

- [ ] **Step 2: Add the inspector controls**

In `drawAdcProperties()` after the sample-rate input, add a combo with labels `1`, `2`, `4`, `8`. On selection, call `setDecimation()` and set `m_param_edited = true`. Add a normalized numeric input using the existing `utils::inputDouble()` helper with step `0.01`, format `%.3f`, and bounds `-0.5`/`0.5`; call `setNcoFsFraction()` and set `m_param_edited = true` when changed.

Do not add a second absolute-Hz NCO field: the displayed value is always the normalized factor requested by the spec.

- [ ] **Step 3: Add descriptor coverage**

Add a test in `tests/test_component_authoring.cpp` that finds the `adc` descriptor and asserts fields named `fs_Hz`, `nsd_dBm_per_Hz`, `decimation`, and `nco_fs_fraction`, with the two new fields reporting numeric bounds `1..8` and `-0.5..0.5`.

- [ ] **Step 4: Run focused UI/registry tests**

Run:

```bash
cmake --build build --target test_component_authoring
ctest --test-dir build -R '^test_component_authoring$' --output-on-failure
```

Expected: the standalone authoring test passes and the ADC drawer compiles with the new controls.

- [ ] **Step 5: Commit the UI and metadata slice**

```bash
git add app/src/inspector_panel.cpp app/src/component_type_registry.cpp tests/test_component_authoring.cpp
git commit -m "feat: expose ADC DDC controls in inspector"
```

---

### Task 4: Update component definitions and downstream propagation coverage

**Files:**
- Modify: `component_data/library/adcs/analog-devices/ad9226.json`
- Modify: `component_data/library/adcs/ti/adc12d1600.json`
- Modify: `tests/test_signal_domain.cpp`
- Modify: `AGENTS.md`

**Interfaces:**
- Consumes: serialized ADC keys and output-rate semantics from Task 2.
- Produces: complete built-in ADC definitions, regression coverage for ADC → PFB sample-rate flow, and durable project guidance.

- [ ] **Step 1: Add explicit compatibility parameters to both built-ins**

In each ADC definition's `parameters` object, add:

```json
"decimation": 2,
"nco_fs_fraction": 0.25
```

Keep `schema_version` at `1`; these are optional additive parameters and no schema migration is required.

- [ ] **Step 2: Add configurable ADC → PFB propagation coverage**

Extend the existing standalone signal-domain tests with a case that creates an ADC and PFB, sets ADC `Fs=1e9` and `decimation=4`, links ADC output to PFB input, runs the chain, and asserts `adc.node().outputs[0].fs_Hz == Approx(250e6)`. Assert that the PFB input sees the same `fs_Hz`, that the PFB output remains complex baseband, and that its configured/input-derived processing uses the propagated `250e6` rate. Use the existing graph/test fixture and do not change the ADC-only link policy.

- [ ] **Step 3: Record the durable contract in root DOX guidance**

Add a concise user-preference bullet under `AGENTS.md`:

```text
- ADC DDC decimation is configurable as 1/2/4/8 and NCO tuning is stored as a normalized factor of the ADC input sample rate; legacy ADC state defaults to decimation 2 and NCO +0.25×Fs.
```

- [ ] **Step 4: Run focused propagation and library checks**

Run:

```bash
cmake --build build --target test_signal_domain tests
ctest --test-dir build -R '^(test_signal_domain|tests)$' --output-on-failure
```

Expected: signal-domain propagation and existing ADC/PFB coverage pass.

- [ ] **Step 5: Commit definitions, propagation coverage, and DOX update**

```bash
git add component_data/library/adcs/analog-devices/ad9226.json component_data/library/adcs/ti/adc12d1600.json tests/test_signal_domain.cpp AGENTS.md
git commit -m "test: cover configurable ADC rate propagation"
```

---

### Task 5: Final focused verification and formatting

**Files:**
- No additional source files; inspect all files changed by Tasks 1–4.

- [ ] **Step 1: Run the focused feature tests**

```bash
cmake --build build --target test_adc_configuration test_component_authoring test_signal_domain tests
ctest --test-dir build -R '^(test_adc_configuration|test_component_authoring|test_signal_domain|tests)$' --output-on-failure
```

Expected: all selected CTest entries pass with zero failures.

- [ ] **Step 2: Check formatting only on changed C++ files**

```bash
scripts/format.sh --check
```

Expected: the formatter reports no changed-file violations. If it reports a violation, run `scripts/format.sh` and review only the resulting ADC/app/test diff before committing the formatting fix.

- [ ] **Step 3: Inspect the final diff and working tree**

```bash
git diff HEAD~4..HEAD --stat
git diff HEAD~4..HEAD -- adc app/src/inspector_panel.cpp app/src/component_type_registry.cpp tests component_data/library/adcs AGENTS.md
git status --short
```

Confirm that only the requested ADC controls, tests, component definitions, and DOX guidance changed; no PFB link-policy or unrelated DSP refactor is present.

- [ ] **Step 4: Commit any formatting-only correction separately**

```bash
git add <only-files-reported-by-format-check>
git commit -m "style: format ADC DDC changes"
```

Do not create this commit when no formatting correction is needed.

## Plan Self-Review

- **Spec coverage:** Engine parameters, NCO input-rate semantics, output rate/grid, ideal filtering, PSD behavior, UI, persistence, library fields, legacy defaults, invalid-value handling, downstream propagation, tests, and DOX documentation each have a task.
- **Placeholder scan:** No task depends on a TBD, TODO, unspecified helper, or future abstraction.
- **Type consistency:** The plan uses `int decimation()`/`setDecimation(int)` and `double ncoFsFraction()`/`setNcoFsFraction(double)` consistently; JSON keys are `decimation` and `nco_fs_fraction` throughout.
- **Scope check:** The work stays in the ADC engine, existing app registry/inspector, built-in ADC definitions, focused tests, and required DOX guidance. FIR modeling, PFB internals, and `IQStream` remain explicitly excluded.
- **Verification check:** The plan runs the new standalone test, existing authoring/signal-domain tests, and the current ADC tests through the existing CTest targets before completion.
