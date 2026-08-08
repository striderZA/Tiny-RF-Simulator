# Spectrum Euler Conjugate-Symmetry Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the real-domain (pre-ADC) spectrum model and its display honor Euler's formula
(`cos(2*pi*fc*t) = 0.5*(exp(j*2*pi*fc*t) + exp(-j*2*pi*fc*t))`) by showing real tones as ±fc
half-power conjugate pairs, while leaving the ADC's DDC output and all interior DSP math
byte-identical to today.

**Architecture:** Add a `Spectrum::is_complex_baseband` flag (propagated like `fs_Hz` already is)
and a `conjugateSymmetricExpand()` helper in `common/spectrum.h`. Interior DSP (generator,
nonlinear model, gain/filter/S-param stages, mixer) stays untouched. The helper is applied only at
the spectrum-analyzer render path for real-domain spectra. The ADC gets the domain flag set to
`true` on its output but needs **no power-math change** — its existing unmodified pass-through of
`power_dBm` already is the physically-compensated result (see design doc §5).

**Tech Stack:** C++20, Catch2, CMake/Ninja, MinGW-w64 (Windows).

**Design doc:** `docs/superpowers/specs/2026-08-03-spectrum-euler-conjugate-symmetry-design.md`

## Global Constraints

- Interior DSP components (`signal_generator`, `nonlinear_model.h`, `mixer`, and every gain/filter
  S-parameter pass-through stage) MUST NOT split tones into conjugate pairs — they keep operating
  on the existing single collapsed-power-per-tone representation. Splitting there would corrupt
  `nonlinear_model.h`'s harmonic/IM math, which is calibrated against full real-tone power.
- `AdcEngine`'s existing `alias_frequency`, NCO-shift/windowing math, and tone `power_dBm`
  pass-through MUST NOT change. Only `is_complex_baseband = true` is new there.
- The existing "ADC DDC preserves tone power and phase" test in `tests/test_adc.cpp` MUST continue
  to pass with its current expected values, unmodified.
- **MinGW-w64 test-registration ceiling:** `tests/CMakeLists.txt` documents that this toolchain
  silently drops any `TEST_CASE` registered beyond the ~217 already linked into the main `tests`
  executable (confirmed via a prior from-scratch rebuild investigation — see the comment above
  `test_component_authoring` in `tests/CMakeLists.txt`). The current baseline is exactly 217 test
  cases. **Every new test in this plan goes into a new standalone executable
  (`tests/test_signal_domain.cpp`), never appended to `test_main.cpp`, `test_adc.cpp`, or any other
  file already compiled into the `tests` target.** This mirrors the existing
  `test_attenuator`/`test_combiner`/`test_component_authoring`/`test_extensions` pattern.
- Every `out.is_complex_baseband = ...` propagation line mirrors the codebase's existing
  `out.fs_Hz = in_ptr ? in_ptr->fs_Hz : 0.0;` pattern in shape, but is inserted next to each
  component's `out.tones = ...` assignment (present in every branch, unlike `fs_Hz` which several
  components — `coax`, `splitter`, `mixer`, `pfb_channelizer` — don't currently set at all).
- Build: `cmake --build build` (Ninja). Full suite: `ctest --test-dir build --output-on-failure`
  reports one misleadingly-combined-name failure for the multi-case `tests` binary on this Windows
  environment (a pre-existing ctest/Windows command-line-length quirk, unrelated to test content) —
  always cross-check with `build/bin/tests.exe` directly, and run new standalone test binaries
  (`build/bin/test_signal_domain.exe`) directly too.

---

### Task 1: Data model — `is_complex_baseband` flag + `conjugateSymmetricExpand` helper

**Files:**
- Modify: `common/spectrum.h`
- Create: `tests/test_signal_domain.cpp`
- Modify: `tests/CMakeLists.txt:60-98` (register new standalone executable, following the
  `test_attenuator` pattern at lines 61-68)

**Interfaces:**
- Produces: `bool Spectrum::is_complex_baseband` (default `false`), and
  `std::vector<Spectrum::Tone> conjugateSymmetricExpand(const std::vector<Spectrum::Tone> &tones)`
  — both declared in `common/spectrum.h`, consumed by Task 6 (spectrum analyzer) and referenced
  (flag only) by Tasks 2-5 and 7.

- [ ] **Step 1: Create the new standalone test file with failing tests**

Create `tests/test_signal_domain.cpp`:

```cpp
#include "spectrum.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>

using Catch::Matchers::WithinAbs;

TEST_CASE("Spectrum: is_complex_baseband defaults to false", "[spectrum][domain]") {
    Spectrum s;
    REQUIRE(s.is_complex_baseband == false);
}

TEST_CASE("conjugateSymmetricExpand: splits a real tone into +-fc half-power pair",
          "[spectrum][euler]") {
    std::vector<Spectrum::Tone> in = {{250e6, -20.0, 45.0}};
    auto out = conjugateSymmetricExpand(in);

    REQUIRE(out.size() == 2);

    const double expected_power = -20.0 - 10.0 * std::log10(2.0); // -23.0103 dBm

    bool found_pos = false, found_neg = false;
    for (const auto &t : out) {
        if (t.freq_Hz > 0.0) {
            found_pos = true;
            REQUIRE_THAT(t.freq_Hz, WithinAbs(250e6, 1e-6));
            REQUIRE_THAT(t.power_dBm, WithinAbs(expected_power, 1e-9));
            REQUIRE_THAT(t.phase_deg, WithinAbs(45.0, 1e-9));
        } else {
            found_neg = true;
            REQUIRE_THAT(t.freq_Hz, WithinAbs(-250e6, 1e-6));
            REQUIRE_THAT(t.power_dBm, WithinAbs(expected_power, 1e-9));
            REQUIRE_THAT(t.phase_deg, WithinAbs(45.0, 1e-9));
        }
    }
    REQUIRE(found_pos);
    REQUIRE(found_neg);
}

TEST_CASE("conjugateSymmetricExpand: DC tone is not mirrored or split", "[spectrum][euler]") {
    std::vector<Spectrum::Tone> in = {{0.0, -10.0, 0.0}};
    auto out = conjugateSymmetricExpand(in);

    REQUIRE(out.size() == 1);
    REQUIRE(out[0].freq_Hz == 0.0);
    REQUIRE_THAT(out[0].power_dBm, WithinAbs(-10.0, 1e-9));
}

TEST_CASE("conjugateSymmetricExpand: multiple tones each expand independently",
          "[spectrum][euler]") {
    std::vector<Spectrum::Tone> in = {
        {100e6, -10.0, 0.0}, {0.0, -5.0, 0.0}, {200e6, -30.0, 90.0}};
    auto out = conjugateSymmetricExpand(in);

    // Two real tones (2 mirrors each) + one DC tone (unmirrored) = 5 entries
    REQUIRE(out.size() == 5);
}

TEST_CASE("conjugateSymmetricExpand: empty input produces empty output", "[spectrum][euler]") {
    std::vector<Spectrum::Tone> in;
    auto out = conjugateSymmetricExpand(in);
    REQUIRE(out.empty());
}
```

- [ ] **Step 2: Register the new executable in `tests/CMakeLists.txt`**

Read the current end of `tests/CMakeLists.txt` first (it ends after the `test_extensions` block
added at lines 92-98) and append, following the exact `test_attenuator` pattern at lines 61-68:

```cmake
add_executable(test_signal_domain test_signal_domain.cpp)
target_link_libraries(test_signal_domain PRIVATE
    common
    Catch2::Catch2WithMain
)
target_compile_definitions(test_signal_domain PRIVATE PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME test_signal_domain COMMAND test_signal_domain WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
```

(This links only `common` for now — Task 2-5 and 7 will add the component engine link libraries
as they append tests that need them. Reconfigure CMake after each such addition:
`cmake -B build -G Ninja`.)

- [ ] **Step 3: Configure, build, and verify the new test fails to compile**

Run:
```bash
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc
cmake --build build --target test_signal_domain
```
Expected: FAIL — compile error, `Spectrum` has no member `is_complex_baseband` and
`conjugateSymmetricExpand` is not declared.

- [ ] **Step 4: Implement the field and helper in `common/spectrum.h`**

Add the field right after the existing `fs_Hz` member (currently `common/spectrum.h` line 29,
`double fs_Hz = 0.0;`), and add the free function after the closing `};` of `struct Spectrum`
(currently line 49) but before `struct Peak`:

```cpp
    // Sample rate of the signal this spectrum represents (Hz).
    // Set by ADCs, propagated through most components, read by PFB channelizer.
    double fs_Hz = 0.0;

    // True for spectra downstream of an ADC's DDC (complex baseband/IQ). False (default) for
    // real-domain (analog) spectra. Set only by AdcEngine's output; propagated downstream from
    // there exactly like fs_Hz already is. Determines whether the spectrum-analyzer render path
    // needs to apply conjugateSymmetricExpand() before binning tone power (see that function).
    bool is_complex_baseband = false;
```

```cpp
// Expands real-domain tones into their conjugate-symmetric (+-fc) representation per Euler's
// formula: cos(2*pi*fc*t) = 0.5*(exp(j*2*pi*fc*t) + exp(-j*2*pi*fc*t)). Each non-DC tone becomes
// two entries at +freq_Hz and -freq_Hz, each at half the linear power (-3.0103 dB = 10*log10(2)
// below the input). DC tones (freq_Hz == 0) are self-conjugate and pass through unchanged (no
// mirror, no power split).
//
// Used only where +-fc content is physically meaningful: rendering a real-domain (pre-ADC)
// spectrum. NOT used by interior DSP (generator, nonlinear_model.h, gain/filter/S-param stages,
// mixer), which must stay on the collapsed single-entry-per-tone representation — splitting there
// would corrupt nonlinear_model.h's harmonic/IM math (calibrated against full real-tone power).
inline std::vector<Spectrum::Tone>
conjugateSymmetricExpand(const std::vector<Spectrum::Tone> &tones) {
    std::vector<Spectrum::Tone> out;
    out.reserve(tones.size() * 2);
    for (const auto &t : tones) {
        if (t.freq_Hz == 0.0) {
            out.push_back(t);
            continue;
        }
        Spectrum::Tone half = t;
        half.power_dBm = t.power_dBm - 10.0 * std::log10(2.0);
        Spectrum::Tone at_f = half;
        at_f.freq_Hz = t.freq_Hz;
        Spectrum::Tone at_negf = half;
        at_negf.freq_Hz = -t.freq_Hz;
        out.push_back(at_f);
        out.push_back(at_negf);
    }
    return out;
}
```

`common/spectrum.h` already includes `<cmath>` (line 4), so `std::log10` is available with no new
include.

- [ ] **Step 5: Build and verify the new test passes**

Run:
```bash
cmake --build build --target test_signal_domain
./build/bin/test_signal_domain.exe
```
Expected: PASS — `All tests passed (N assertions in 5 test cases)`.

- [ ] **Step 6: Commit**

```bash
git add common/spectrum.h tests/test_signal_domain.cpp tests/CMakeLists.txt
git commit -m "feat: add Spectrum::is_complex_baseband flag and conjugateSymmetricExpand helper"
```

---

### Task 2: Flag propagation — signal_generator, amplifier, attenuator

**Files:**
- Modify: `signal_generator/src/signal_generator_engine.cpp:72`
- Modify: `amplifier/src/amplifier_engine.cpp:55` (S-param branch), `:138` (ideal branch)
- Modify: `attenuator/src/attenuator_engine.cpp:71` (S-param branch), `:134` (manual branch)
- Modify: `tests/CMakeLists.txt` (add link libraries for `test_signal_domain`)
- Test: `tests/test_signal_domain.cpp` (append)

**Interfaces:**
- Consumes: `Spectrum::is_complex_baseband` (Task 1).
- Produces: nothing new — this task and Tasks 3-5 together make the flag propagate through every
  pass-through component in the graph, which Task 7's end-to-end test relies on.

- [ ] **Step 1: Append failing propagation tests**

Append to `tests/test_signal_domain.cpp`:

```cpp
#include "amplifier_engine.h"
#include "attenuator_engine.h"
#include "node_graph_engine.h"
#include "signal_generator_engine.h"

namespace {
std::string amplifierS2pPath() {
    return std::string(PROJECT_SOURCE_DIR) + "/component_data/amplifiers/adm-3844psm/"
                                              "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
}
std::string attenuatorS2pPath() {
    return std::string(PROJECT_SOURCE_DIR) +
           "/component_data/fixed_attenuators/atn01-0040psm/ATN01-0040PSM_SM_25C_De.s2p";
}
} // namespace

TEST_CASE("SignalGenerator: output is_complex_baseband is always false", "[domain][generator]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.update(0.0);
    REQUIRE(gen.node().outputs[0].is_complex_baseband == false);
}

TEST_CASE("Amplifier: propagates is_complex_baseband (ideal mode)", "[domain][amplifier]") {
    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);

    Spectrum in;
    in.frequencies = {1e9, 2e9};
    in.tones = {{1e9, -10.0, 0.0}};
    in.noise_W.assign(2, 1e-21);
    in.noise_added_W.assign(2, 0.0);
    in.noise_total_W.assign(2, 1e-21);
    in.is_complex_baseband = true;

    amp.node().inputs[0] = &in;
    amp.update(0.0);

    REQUIRE(amp.node().outputs[0].is_complex_baseband == true);
}

TEST_CASE("Amplifier: propagates is_complex_baseband (S-param mode)", "[domain][amplifier]") {
    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setSParamFilepath(amplifierS2pPath());
    REQUIRE(amp.sparamMode());

    Spectrum in;
    in.frequencies = {1e9, 2e9};
    in.tones = {{1e9, -10.0, 0.0}};
    in.noise_W.assign(2, 1e-21);
    in.noise_added_W.assign(2, 0.0);
    in.noise_total_W.assign(2, 1e-21);
    in.is_complex_baseband = true;

    amp.node().inputs[0] = &in;
    amp.update(0.0);

    REQUIRE(amp.node().outputs[0].is_complex_baseband == true);
}

TEST_CASE("Attenuator: propagates is_complex_baseband (manual mode)", "[domain][attenuator]") {
    NodeGraphEngine graph;
    AttenuatorEngine atten(0, graph);

    Spectrum in;
    in.frequencies = {1e9, 2e9};
    in.tones = {{1e9, -10.0, 0.0}};
    in.noise_W.assign(2, 1e-21);
    in.noise_added_W.assign(2, 0.0);
    in.noise_total_W.assign(2, 1e-21);
    in.is_complex_baseband = true;

    atten.node().inputs[0] = &in;
    atten.update(0.0);

    REQUIRE(atten.node().outputs[0].is_complex_baseband == true);
}

TEST_CASE("Attenuator: propagates is_complex_baseband (S-param mode)", "[domain][attenuator]") {
    NodeGraphEngine graph;
    AttenuatorEngine atten(0, graph);
    atten.setSParamFile(attenuatorS2pPath());

    Spectrum in;
    in.frequencies = {1e9, 2e9};
    in.tones = {{1e9, -10.0, 0.0}};
    in.noise_W.assign(2, 1e-21);
    in.noise_added_W.assign(2, 0.0);
    in.noise_total_W.assign(2, 1e-21);
    in.is_complex_baseband = true;

    atten.node().inputs[0] = &in;
    atten.update(0.0);

    REQUIRE(atten.node().outputs[0].is_complex_baseband == true);
}

TEST_CASE("SignalGenerator+Amplifier: false flag also propagates", "[domain][amplifier]") {
    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);

    Spectrum in; // is_complex_baseband defaults false
    in.frequencies = {1e9, 2e9};
    in.noise_W.assign(2, 1e-21);
    in.noise_added_W.assign(2, 0.0);
    in.noise_total_W.assign(2, 1e-21);

    amp.node().inputs[0] = &in;
    amp.update(0.0);

    REQUIRE(amp.node().outputs[0].is_complex_baseband == false);
}
```

Add the new link libraries to `tests/CMakeLists.txt`'s `test_signal_domain` target (from Task 1
Step 2):

```cmake
target_link_libraries(test_signal_domain PRIVATE
    common
    simulator::signal_generator_engine
    simulator::amplifier_engine
    simulator::attenuator_engine
    simulator::node_graph_engine
    Catch2::Catch2WithMain
)
```

- [ ] **Step 2: Reconfigure, build, verify failure**

Run:
```bash
cmake -B build -G Ninja
cmake --build build --target test_signal_domain
```
Expected: FAIL — compile error (link succeeds structurally, but `REQUIRE(... == true)` assertions
fail at runtime since the flag isn't propagated yet) OR runtime FAIL if it compiles cleanly. Either
way, run `./build/bin/test_signal_domain.exe` and confirm the 6 new `[domain]` test cases FAIL.

- [ ] **Step 3: Implement propagation**

`signal_generator/src/signal_generator_engine.cpp` — insert after line 72
(`out.fs_Hz = m_fs_Hz;`):
```cpp
    out.is_complex_baseband = false;
```

`amplifier/src/amplifier_engine.cpp` — insert after line 55 (S-param branch,
`out.tones = in_ptr ? in_ptr->tones : std::vector<Spectrum::Tone>{};`):
```cpp
        out.is_complex_baseband = in_ptr ? in_ptr->is_complex_baseband : false;
```
— and insert after line 138 (ideal branch, same statement):
```cpp
    out.is_complex_baseband = in_ptr ? in_ptr->is_complex_baseband : false;
```

`attenuator/src/attenuator_engine.cpp` — insert after line 71 (S-param branch,
`out.tones = in_ptr ? in_ptr->tones : std::vector<Spectrum::Tone>{};`):
```cpp
        out.is_complex_baseband = in_ptr ? in_ptr->is_complex_baseband : false;
```
— and insert after line 134 (manual branch, same statement):
```cpp
    out.is_complex_baseband = in_ptr ? in_ptr->is_complex_baseband : false;
```

- [ ] **Step 4: Build and verify all pass**

Run:
```bash
cmake --build build --target test_signal_domain
./build/bin/test_signal_domain.exe
```
Expected: PASS — `All tests passed (N assertions in 11 test cases)`.

- [ ] **Step 5: Commit**

```bash
git add signal_generator/src/signal_generator_engine.cpp amplifier/src/amplifier_engine.cpp \
    attenuator/src/attenuator_engine.cpp tests/test_signal_domain.cpp tests/CMakeLists.txt
git commit -m "feat: propagate is_complex_baseband through generator, amplifier, attenuator"
```

---

### Task 3: Flag propagation — combiner, equalizer, ideal_filter

**Files:**
- Modify: `combiner/src/combiner_engine.cpp:142` (S-param branch), `:231` (manual branch)
- Modify: `equalizer/src/equalizer_engine.cpp:53` (S-param branch), `:106` (ideal branch)
- Modify: `ideal_filter/src/ideal_filter_engine.cpp:73` (S-param branch), `:125` (main branch)
- Modify: `tests/CMakeLists.txt` (add link libraries)
- Test: `tests/test_signal_domain.cpp` (append)

**Interfaces:**
- Consumes: `Spectrum::is_complex_baseband` (Task 1).

- [ ] **Step 1: Append failing propagation tests**

Append to `tests/test_signal_domain.cpp`:

```cpp
#include "combiner_engine.h"
#include "equalizer_engine.h"
#include "ideal_filter_engine.h"

TEST_CASE("Combiner: propagates is_complex_baseband (manual mode, either input)",
          "[domain][combiner]") {
    NodeGraphEngine graph;
    CombinerEngine comb(0, graph);

    Spectrum in0, in1;
    in0.frequencies = {1e9, 2e9};
    in0.noise_total_W.assign(2, 1e-21);
    in0.is_complex_baseband = true;
    in1.frequencies = {1e9, 2e9};
    in1.noise_total_W.assign(2, 1e-21);
    in1.is_complex_baseband = false;

    comb.node().inputs[0] = &in0;
    comb.node().inputs[1] = &in1;
    comb.update(0.0);

    REQUIRE(comb.node().outputs[0].is_complex_baseband == true);
}

TEST_CASE("Equalizer: propagates is_complex_baseband (ideal mode)", "[domain][equalizer]") {
    NodeGraphEngine graph;
    EqualizerEngine eq(0, graph);

    Spectrum in;
    in.frequencies = {1e9, 2e9};
    in.tones = {{1e9, -10.0, 0.0}};
    in.noise_total_W.assign(2, 1e-21);
    in.is_complex_baseband = true;

    eq.node().inputs[0] = &in;
    eq.update(0.0);

    REQUIRE(eq.node().outputs[0].is_complex_baseband == true);
}

TEST_CASE("IdealFilter: propagates is_complex_baseband", "[domain][ideal_filter]") {
    NodeGraphEngine graph;
    IdealFilterEngine flt(0, graph);

    Spectrum in;
    in.frequencies = {1e9, 2e9};
    in.tones = {{1e9, -10.0, 0.0}};
    in.noise_total_W.assign(2, 1e-21);
    in.is_complex_baseband = true;

    flt.node().inputs[0] = &in;
    flt.update(0.0);

    REQUIRE(flt.node().outputs[0].is_complex_baseband == true);
}
```

Add link libraries to the `test_signal_domain` target:
```cmake
    simulator::combiner_engine
    simulator::equalizer_engine
    simulator::ideal_filter_engine
```

- [ ] **Step 2: Reconfigure, build, verify failure**

```bash
cmake -B build -G Ninja
cmake --build build --target test_signal_domain
./build/bin/test_signal_domain.exe
```
Expected: the 3 new `[domain]` test cases FAIL.

- [ ] **Step 3: Implement propagation**

`combiner/src/combiner_engine.cpp` — insert after line 142 (S-param branch, `out.tones =
combined_tones;`):
```cpp
        out.is_complex_baseband =
            (in0 && in0->is_complex_baseband) || (in1 && in1->is_complex_baseband);
```
— and insert after line 231 (manual branch, same statement):
```cpp
    out.is_complex_baseband = (in0 && in0->is_complex_baseband) || (in1 && in1->is_complex_baseband);
```

`equalizer/src/equalizer_engine.cpp` — insert after line 53 (S-param branch,
`out.tones = in_ptr ? in_ptr->tones : std::vector<Spectrum::Tone>{};`):
```cpp
        out.is_complex_baseband = in_ptr ? in_ptr->is_complex_baseband : false;
```
— and insert after line 106 (ideal branch, same statement):
```cpp
    out.is_complex_baseband = in_ptr ? in_ptr->is_complex_baseband : false;
```

`ideal_filter/src/ideal_filter_engine.cpp` — insert after line 73 (S-param branch,
`out.tones = in_ptr ? in_ptr->tones : std::vector<Spectrum::Tone>{};`):
```cpp
        out.is_complex_baseband = in_ptr ? in_ptr->is_complex_baseband : false;
```
— and insert after line 125 (main branch, `out.tones.clear();`):
```cpp
    out.is_complex_baseband = in_ptr ? in_ptr->is_complex_baseband : false;
```

- [ ] **Step 4: Build and verify all pass**

```bash
cmake --build build --target test_signal_domain
./build/bin/test_signal_domain.exe
```
Expected: PASS — `All tests passed (N assertions in 14 test cases)`.

- [ ] **Step 5: Commit**

```bash
git add combiner/src/combiner_engine.cpp equalizer/src/equalizer_engine.cpp \
    ideal_filter/src/ideal_filter_engine.cpp tests/test_signal_domain.cpp tests/CMakeLists.txt
git commit -m "feat: propagate is_complex_baseband through combiner, equalizer, ideal_filter"
```

---

### Task 4: Flag propagation — coax, splitter, mixer, pfb_channelizer, touchstone s_parameter_data

**Files:**
- Modify: `coax/src/coax_cable_engine.cpp:69`
- Modify: `splitter/src/splitter_engine.cpp:49`
- Modify: `mixer/src/mixer_engine.cpp:39-40`
- Modify: `pfb_channelizer/src/pfb_channelizer_engine.cpp:138`
- Modify: `touchstone/src/s_parameter_data.cpp:63`
- Modify: `tests/CMakeLists.txt` (add link libraries)
- Test: `tests/test_signal_domain.cpp` (append)

**Interfaces:**
- Consumes: `Spectrum::is_complex_baseband` (Task 1).

- [ ] **Step 1: Append failing propagation tests**

Append to `tests/test_signal_domain.cpp`:

```cpp
#include "coax_cable_engine.h"
#include "mixer_engine.h"
#include "pfb_channelizer_engine.h"
#include "s_parameter_data.h"
#include "splitter_engine.h"

TEST_CASE("CoaxCable: propagates is_complex_baseband", "[domain][coax]") {
    NodeGraphEngine graph;
    CoaxCableEngine coax(0, graph);

    Spectrum in;
    in.frequencies = {1e9, 2e9};
    in.tones = {{1e9, -10.0, 0.0}};
    in.noise_total_W.assign(2, 1e-21);
    in.is_complex_baseband = true;

    coax.node().inputs[0] = &in;
    coax.update(0.0);

    REQUIRE(coax.node().outputs[0].is_complex_baseband == true);
}

TEST_CASE("Splitter: propagates is_complex_baseband to both outputs", "[domain][splitter]") {
    NodeGraphEngine graph;
    SplitterEngine splitter(0, graph);

    Spectrum in;
    in.frequencies = {1e9, 2e9};
    in.tones = {{1e9, -10.0, 0.0}};
    in.noise_total_W.assign(2, 1e-21);
    in.is_complex_baseband = true;

    splitter.node().inputs[0] = &in;
    splitter.update(0.0);

    REQUIRE(splitter.node().outputs[0].is_complex_baseband == true);
    REQUIRE(splitter.node().outputs[1].is_complex_baseband == true);
}

TEST_CASE("Mixer: propagates is_complex_baseband", "[domain][mixer]") {
    NodeGraphEngine graph;
    MixerEngine mixer(0, graph);

    Spectrum in;
    in.frequencies = {1e9, 2e9};
    in.tones = {{1e9, -10.0, 0.0}};
    in.noise_total_W.assign(2, 1e-21);
    in.is_complex_baseband = true;

    mixer.node().inputs[0] = &in;
    mixer.update(0.0);

    REQUIRE(mixer.node().outputs[0].is_complex_baseband == true);
}

TEST_CASE("PFBChannelizer: propagates is_complex_baseband", "[domain][pfb]") {
    NodeGraphEngine graph;
    PFBChannelizerEngine pfb(0, graph);

    Spectrum in;
    in.frequencies.resize(401);
    for (int i = 0; i < 401; ++i)
        in.frequencies[i] = -200e6 + i * 1e6;
    in.noise_total_W.assign(401, 1e-20);
    in.is_complex_baseband = true;

    pfb.setFs_Hz(400e6);
    pfb.node().inputs[0] = &in;
    pfb.update(0.0);

    REQUIRE(pfb.node().outputs[0].is_complex_baseband == true);
}

TEST_CASE("SParameterData::applyToSpectrum propagates is_complex_baseband", "[domain][sparam]") {
    SParameterData sparam;
    bool loaded = sparam.load(std::string(PROJECT_SOURCE_DIR) +
                               "/component_data/amplifiers/adm-3844psm/"
                               "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p");
    REQUIRE(loaded);

    Spectrum in;
    in.frequencies = {1e9, 2e9};
    in.tones = {{1e9, -10.0, 0.0}};
    in.noise_total_W.assign(2, 1e-21);
    in.is_complex_baseband = true;

    Spectrum out;
    sparam.applyToSpectrum(in, out, 0); // loaded() == true -> reaches the propagation line

    REQUIRE(out.is_complex_baseband == true);
}

TEST_CASE("SParameterData::applyToSpectrum defaults is_complex_baseband when not loaded",
          "[domain][sparam]") {
    SParameterData sparam; // not loaded()
    Spectrum in;
    in.is_complex_baseband = true;
    Spectrum out;
    sparam.applyToSpectrum(in, out, 0); // early-return path, never reaches the propagation line
    REQUIRE(out.is_complex_baseband == false);
}
```

Add link libraries to the `test_signal_domain` target:
```cmake
    simulator::coax_cable_engine
    simulator::splitter_engine
    simulator::mixer_engine
    simulator::pfb_channelizer_engine
    simulator::touchstone_parser
```

- [ ] **Step 2: Reconfigure, build, verify failure**

```bash
cmake -B build -G Ninja
cmake --build build --target test_signal_domain
./build/bin/test_signal_domain.exe
```
Expected: 5 of the 6 new `[domain]` test cases FAIL (`CoaxCable`, `Splitter`, `Mixer`, `PFB`, and
the loaded-`SParameterData` case). The not-loaded `SParameterData` case passes trivially since it
only checks the default (documents current behavior on the early-return path, not new logic).

- [ ] **Step 3: Implement propagation**

`coax/src/coax_cable_engine.cpp` — insert after line 69
(`out.tones = in_ptr ? in_ptr->tones : std::vector<Spectrum::Tone>{};`):
```cpp
    out.is_complex_baseband = in_ptr ? in_ptr->is_complex_baseband : false;
```

`splitter/src/splitter_engine.cpp` — insert after line 49 (inside the `for (size_t out_idx ...)`
loop, `out.tones = in_ptr ? in_ptr->tones : std::vector<Spectrum::Tone>{};`):
```cpp
        out.is_complex_baseband = in_ptr ? in_ptr->is_complex_baseband : false;
```

`mixer/src/mixer_engine.cpp` — insert after line 39 (`out.tones.clear();`, before the
`if (in_ptr) {` block that starts at line 40):
```cpp
    out.is_complex_baseband = in_ptr ? in_ptr->is_complex_baseband : false;
```

`pfb_channelizer/src/pfb_channelizer_engine.cpp` — insert after line 138
(`out.tones = active.tones;`):
```cpp
    out.is_complex_baseband = in_ptr ? in_ptr->is_complex_baseband : false;
```

`touchstone/src/s_parameter_data.cpp` — insert after line 63 (`out.tones = in.tones;`):
```cpp
    out.is_complex_baseband = in.is_complex_baseband;
```
(`in` is a `const Spectrum &` reference here, not a pointer — no null check needed, matching the
rest of `applyToSpectrum`'s style which dereferences `in` directly throughout.)

- [ ] **Step 4: Build and verify all pass**

```bash
cmake --build build --target test_signal_domain
./build/bin/test_signal_domain.exe
```
Expected: PASS — `All tests passed (N assertions in 20 test cases)`.

- [ ] **Step 5: Commit**

```bash
git add coax/src/coax_cable_engine.cpp splitter/src/splitter_engine.cpp \
    mixer/src/mixer_engine.cpp pfb_channelizer/src/pfb_channelizer_engine.cpp \
    touchstone/src/s_parameter_data.cpp tests/test_signal_domain.cpp tests/CMakeLists.txt
git commit -m "feat: propagate is_complex_baseband through coax, splitter, mixer, pfb, s-param"
```

---

### Task 5: ADC — set `is_complex_baseband = true`

**Files:**
- Modify: `adc/src/adc_engine.cpp` (lines 46-56 empty-input path, line 69 main path)
- Test: `tests/test_signal_domain.cpp` (append)
- Modify: `tests/CMakeLists.txt` (add `simulator::adc_engine` link library)

**Interfaces:**
- Consumes: `Spectrum::is_complex_baseband` (Task 1).
- Produces: the ADC output now reliably carries `is_complex_baseband == true`, which Task 6's
  spectrum-analyzer render path depends on to skip mirroring post-ADC spectra.

- [ ] **Step 1: Append failing tests**

Append to `tests/test_signal_domain.cpp`:

```cpp
#include "adc_engine.h"

TEST_CASE("AdcEngine: output is_complex_baseband is true (populated input)", "[domain][adc]") {
    NodeGraphEngine graph;
    AdcEngine adc(0, graph);
    adc.setFs_Hz(1e9);

    Spectrum in;
    in.frequencies.resize(101);
    for (int i = 0; i < 101; ++i)
        in.frequencies[i] = i * 5e6;
    in.noise_W.assign(101, 1e-20);
    in.noise_added_W.assign(101, 0.0);
    in.noise_total_W.assign(101, 1e-20);
    in.tones.push_back({250e6, -20.0, 45.0});

    adc.node().inputs[0] = &in;
    adc.update(0.0);

    REQUIRE(adc.node().outputs[0].is_complex_baseband == true);
    // Regression pin (mirrors the existing "ADC DDC preserves tone power and phase" assertion in
    // tests/test_adc.cpp — restated here to prove the flag addition didn't touch power math):
    REQUIRE(adc.node().outputs[0].tones.size() == 1);
    REQUIRE(adc.node().outputs[0].tones[0].power_dBm == Catch::Approx(-20.0));
    REQUIRE(adc.node().outputs[0].tones[0].phase_deg == Catch::Approx(45.0));
}

TEST_CASE("AdcEngine: output is_complex_baseband is true (empty input)", "[domain][adc]") {
    NodeGraphEngine graph;
    AdcEngine adc(1, graph);
    adc.setFs_Hz(1e9);

    adc.node().inputs[0] = nullptr;
    adc.update(0.0);

    REQUIRE(adc.node().outputs[0].is_complex_baseband == true);
    REQUIRE(adc.node().outputs[0].frequencies.empty());
}
```

Note: this file needs `Catch::Approx` — add `#include <catch2/catch_approx.hpp>` and
`using Catch::Approx;` at the top of `tests/test_signal_domain.cpp` if not already present (it
isn't, from Task 1).

Add link library to the `test_signal_domain` target:
```cmake
    simulator::adc_engine
```

- [ ] **Step 2: Reconfigure, build, verify failure**

```bash
cmake -B build -G Ninja
cmake --build build --target test_signal_domain
./build/bin/test_signal_domain.exe
```
Expected: the 2 new `[domain][adc]` test cases FAIL on the `is_complex_baseband == true`
assertions (the power/phase regression-pin assertions already pass, since that part of
`adc_engine.cpp` isn't changing).

- [ ] **Step 3: Implement the flag**

Read `adc/src/adc_engine.cpp` fresh (Tasks 1-4 don't touch this file, so line numbers should match
the plan's earlier full reads, but confirm before editing): in the empty-input early-return block
(currently lines 46-56), insert before the `out.bumpGeneration();` at line 54:
```cpp
        out.is_complex_baseband = true;
```
And in the main path, insert right after line 69 (`out.fs_Hz = m_fs_Hz / 2.0;`):
```cpp
    out.is_complex_baseband = true;
```

Do **not** change `alias_frequency`, the tone-mapping loop (lines 101-111), or any `power_dBm`
math — see design doc §5 for why that math is already correct as-is.

- [ ] **Step 4: Build and verify all pass, then run the full existing ADC suite**

```bash
cmake --build build --target test_signal_domain
./build/bin/test_signal_domain.exe
cmake --build build --target tests
./build/bin/tests.exe "[adc]"
```
Expected: `test_signal_domain.exe` reports `All tests passed (N assertions in 22 test cases)`.
`tests.exe "[adc]"` reports all existing ADC tests (including "ADC DDC preserves tone power and
phase") still passing with unchanged values.

- [ ] **Step 5: Commit**

```bash
git add adc/src/adc_engine.cpp tests/test_signal_domain.cpp tests/CMakeLists.txt
git commit -m "feat: set is_complex_baseband=true on ADC output"
```

---

### Task 6: Spectrum analyzer render-time Euler expansion

**Files:**
- Modify: `spectrum_analyzer/src/spectrum_analyzer_engine.cpp:19-53` (`integratePowerPerBin`)
- Modify: `common/spectrum.h` (no change needed — helper already exists from Task 1)
- Test: `tests/test_signal_domain.cpp` (append)
- Modify: `tests/CMakeLists.txt` (add `simulator::spectrum_analyzer_engine` link library)

**Interfaces:**
- Consumes: `Spectrum::is_complex_baseband` and `conjugateSymmetricExpand()` (Task 1).

- [ ] **Step 1: Append failing tests**

Append to `tests/test_signal_domain.cpp`:

```cpp
#include "spectrum_analyzer_engine.h"

TEST_CASE("SpectrumAnalyzer: real-domain tone renders as +-fc half-power pair",
          "[domain][spectrum_analyzer]") {
    SpectrumAnalyzerEngine sa;

    Spectrum spec;
    spec.frequencies.resize(41);
    for (int i = 0; i < 41; ++i)
        spec.frequencies[i] = -100e6 + i * 5e6; // bin width 5 MHz, spans -100..+100 MHz
    spec.tones = {{50e6, -20.0, 0.0}};
    spec.noise_total_W.assign(41, 1e-21);
    spec.is_complex_baseband = false;
    spec.generation = 1;

    auto trace = sa.renderSpectrum(spec);

    // Bin for +50 MHz: index (50e6 - (-100e6)) / 5e6 = 30. Bin for -50 MHz: index 10.
    REQUIRE(trace.size() == 41);
    const double expected_power = -20.0 - 10.0 * std::log10(2.0); // ~-23.01 dBm
    REQUIRE_THAT(trace[30], WithinAbs(expected_power, 1.0));
    REQUIRE_THAT(trace[10], WithinAbs(expected_power, 1.0));
    // Neither half-power bin should read the full -20 dBm (that would mean no split happened).
    REQUIRE(trace[30] < -20.0 + 0.5);
}

TEST_CASE("SpectrumAnalyzer: complex-baseband tone renders unchanged (no mirroring)",
          "[domain][spectrum_analyzer]") {
    SpectrumAnalyzerEngine sa;

    Spectrum spec;
    spec.frequencies.resize(41);
    for (int i = 0; i < 41; ++i)
        spec.frequencies[i] = -100e6 + i * 5e6;
    spec.tones = {{50e6, -20.0, 0.0}};
    spec.noise_total_W.assign(41, 1e-21);
    spec.is_complex_baseband = true;
    spec.generation = 1;

    auto trace = sa.renderSpectrum(spec);

    REQUIRE(trace.size() == 41);
    REQUIRE_THAT(trace[30], WithinAbs(-20.0, 1.0)); // full power at +50 MHz, unchanged
    // -50 MHz bin should show only the noise floor, not a mirrored tone.
    REQUIRE(trace[10] < -20.0 + 0.5 ? false : true); // sanity: not near the tone's own power
}
```

Add link library to the `test_signal_domain` target:
```cmake
    simulator::spectrum_analyzer_engine
```

- [ ] **Step 2: Reconfigure, build, verify failure**

```bash
cmake -B build -G Ninja
cmake --build build --target test_signal_domain
./build/bin/test_signal_domain.exe
```
Expected: the first new test (`real-domain tone renders as +-fc half-power pair`) FAILS — today
`integratePowerPerBin` puts the full `-20.0 dBm` at bin 30 and nothing at bin 10. The second test
(complex-baseband, unchanged) already PASSES today since no mirroring currently happens anywhere.

- [ ] **Step 3: Implement render-time expansion**

Read `spectrum_analyzer/src/spectrum_analyzer_engine.cpp` fresh to confirm current line numbers
(should match: `integratePowerPerBin` starting at line 19, the tone-binning loop `for (const auto
&t : spec.tones)` starting at line 40). Replace lines 39-40:

```cpp
    // Add tones as discrete impulses
    for (const auto &t : spec.tones) {
```

with:

```cpp
    // Add tones as discrete impulses. Real-domain spectra (pre-ADC) get expanded into their
    // +-fc conjugate-symmetric half-power pair per Euler's formula before binning; post-ADC
    // complex-baseband spectra are already correctly one-sided and render as-is.
    const std::vector<Spectrum::Tone> &display_tones =
        spec.is_complex_baseband ? spec.tones : conjugateSymmetricExpand(spec.tones);
    for (const auto &t : display_tones) {
```

This requires `display_tones` to outlive the loop body only (it does, as a local in
`integratePowerPerBin`) — no other change needed in that function.

- [ ] **Step 4: Build and verify all pass, then run the full existing spectrum-analyzer suite**

```bash
cmake --build build --target test_signal_domain
./build/bin/test_signal_domain.exe
cmake --build build --target tests
./build/bin/tests.exe "[spectrum_analyzer]"
./build/bin/tests.exe "[spectrum]"
```
Expected: `test_signal_domain.exe` reports all test cases passing (24 total). `tests.exe
"[spectrum_analyzer]"` and `tests.exe "[spectrum]"` report all existing spectrum-analyzer and
`findPeaks` tests still passing, with no expected-value edits needed. This was pre-verified while
writing this plan by auditing every `renderSpectrum`/`findPeaks` call site in `tests/test_main.cpp`
and `tests/test_bench_dsp.cpp`: `findPeaks` tests feed raw `power`/`freq` vectors directly (never
through `integratePowerPerBin`, so unaffected); every `renderSpectrum` test either uses relative
comparisons (`out2[0] > out2[1] + 5.0`, well outside the ±3.0103 dB shift), noise-floor thresholds
(`out[i] < -50.0`), frame-to-frame equality (`Approx(prev[i])`), or asymmetric one-sided frequency
grids starting at `0` where the mirrored `-fc` tone falls outside the grid and is silently dropped
by the existing `bin_idx >= 0` bounds check in `integratePowerPerBin` — none asserts an exact
absolute tone power through `renderSpectrum`. If this build step surfaces a failure anyway (e.g.
from a test file not covered by that audit), stop and re-read the failing test before touching its
assertions — do not weaken an assertion to make it pass without understanding why it changed.

- [ ] **Step 5: Commit**

```bash
git add spectrum_analyzer/src/spectrum_analyzer_engine.cpp tests/test_signal_domain.cpp \
    tests/CMakeLists.txt
git commit -m "feat: spectrum analyzer renders real-domain tones as +-fc Euler pairs"
```


---

### Task 7: Full regression pass and wrap-up

**Files:** none (verification only)

**Interfaces:** none.

- [ ] **Step 1: Full clean build**

```bash
cmake --build build
```
Expected: builds with no errors or new warnings.

- [ ] **Step 2: Run every standalone test binary directly**

```bash
./build/bin/tests.exe
./build/bin/test_attenuator.exe
./build/bin/test_combiner.exe
./build/bin/test_signal_domain.exe
```
Expected: every binary reports `All tests passed`. `tests.exe` should report the original 217
test cases (unchanged — nothing was added there, per the Global Constraints ceiling) plus whatever
`65522`-ish assertion count, now potentially adjusted only if Task 6 Step 4a touched an existing
assertion value. `test_signal_domain.exe` reports all newly-added domain/Euler tests.

- [ ] **Step 3: Spot-check via `ctest` (informational only, known name-mangling quirk)**

```bash
ctest --test-dir build --output-on-failure
```
Expected: same pre-existing single misleading-name failure on the combined `tests` binary
documented in the Global Constraints (unrelated to this change — cross-checked in Step 2), all
other registered tests (`test_attenuator`, `test_combiner`, `test_signal_domain`, etc.) pass
individually.

- [ ] **Step 4: Update the GitHub issue**

```bash
gh issue comment 39 --body "Fixed on branch fix/spectrum-euler-conjugate-symmetry: real-domain spectra now render as +-fc conjugate-symmetric half-power pairs (Euler's formula), while the ADC/DDC output and all interior DSP math (generator, nonlinear model, gain/filter/S-param stages, mixer) remain byte-identical to before. See docs/superpowers/specs/2026-08-03-spectrum-euler-conjugate-symmetry-design.md for the design and docs/superpowers/plans/2026-08-03-spectrum-euler-conjugate-symmetry.md for the implementation plan."
```

- [ ] **Step 5: Final commit / ready-for-review checkpoint**

```bash
git log --oneline fix/spectrum-euler-conjugate-symmetry -8
git status
```
Confirm the working tree is clean and all 7 task commits are present, then hand off to whichever
finishing-a-development-branch flow the user prefers (PR, merge, etc.) — not automated by this
plan.
