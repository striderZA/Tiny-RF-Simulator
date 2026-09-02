# PFB Filter Calculator + Real-Prototype Engine Model — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the PFB engine's narrow analytic bin response with a real shared windowed-sinc Kaiser prototype and add a dockable Filter Calculator tool that reports achieved metrics against a rejection target and applies M/K/beta to a selected PFB.

**Architecture:** A pure, ImGui-free prototype core (`PfbFilterDesign` + metric functions) lives in the `pfb_channelizer` module and is the single source of truth for both the engine's channel weights and the calculator's plots/metrics. The engine deletes its old `prototypeResponse`/`kaiserWindow` and delegates to the core. A thin dockable widget in `app/` renders controls, plot, metrics, and Apply-to-selected-PFB.

**Tech Stack:** C++20 (project standard; uses `std::cyl_bessel_i`, `std::clamp`, `std::hypot`), CMake ≥ 3.20, Catch2 v3 (standalone test executables only — see Global Constraints), ImGui/ImNodes (widget only).

**Spec:** `docs/superpowers/specs/2026-09-02-pfb-filter-calculator-design.md` (committed `4c0d184`).

## Global Constraints

- New `TEST_CASE`s MUST NOT be added to any file compiled into the main `tests` executable (MinGW-w64 silently drops registrations past ~223; release.yml enforces the floor). New coverage goes in a new standalone executable via the `add_standalone_test(...)` helper in `tests/CMakeLists.txt`.
- Floating-point comparisons use `Catch::Approx` from `<catch2/catch_approx.hpp>`.
- Build/test: `cmake -S . -B build` (once) then `cmake --build build --target <target>` and `ctest --test-dir build -R '<pattern>' --output-on-failure`.
- Keep engine serialization unchanged: only M/K/beta are persisted; project files must round-trip unchanged.
- Engine param clamps stay: M in [2, 2048], K in [1, 64], beta in [0, 20].
- Response is normalized so H(0) = 1 (0 dB). x = offset / (Fs/M) in channel-width units, matching the engine's existing slice semantics (|offset| ≤ channel width).
- No solver, no spec-in mode, no tap serialization, no equiripple windows (spec: "Explicitly Not Included").
- `.clang-format` must be satisfied (`scripts/format.sh --check` clean at the end).

---
### Task 1: Prototype core — taps synthesis

**Files:**
- Create: `pfb_channelizer/include/pfb_filter_design.h`
- Create: `pfb_channelizer/src/pfb_filter_design.cpp`
- Modify: `pfb_channelizer/CMakeLists.txt` (add source to the engine library)
- Create: `tests/test_pfb_filter_design.cpp` (first TEST_CASEs only)
- Modify: `tests/CMakeLists.txt` (register standalone `test_pfb_filter_design`)

**Interfaces:**
- Produces: `class PfbFilterDesign` with constructor `PfbFilterDesign(int M = 32, int K = 8, double beta = 8.0)`, `const std::vector<double>& taps() const`, `int tapCount() const`, `int channelCount() const`, `int tapsPerBranch() const`, `double beta() const`, `double responseAt(double x) const` (responseAt body added in Task 2; declared now).

- [ ] **Step 1: Write the header**

`pfb_channelizer/include/pfb_filter_design.h`:

```cpp
#pragma once

#include <string>
#include <vector>

// Real polyphase prototype for the PFB channelizer, shared by the engine
// (channel weights) and the Filter Calculator tool (metrics/plots) so the two
// can never drift. Design: windowed-sinc lowpass with cutoff at the
// half-channel point Fs/(2*M), Kaiser window of length N = K*M, DC gain
// normalized so H(0) = 1 exactly.
class PfbFilterDesign {
  public:
    PfbFilterDesign(int M = 32, int K = 8, double beta = 8.0);
    // |H(x)| at normalized offset x in channel-width units (x = offset/(Fs/M)).
    // Negative x is allowed; the response is symmetric about 0. O(K*M) per call.
    double responseAt(double x) const;
    const std::vector<double> &taps() const { return m_taps; }
    int tapCount() const { return static_cast<int>(m_taps.size()); }
    int channelCount() const { return m_M; }
    int tapsPerBranch() const { return m_K; }
    double beta() const { return m_beta; }

  private:
    void synthesize();
    int m_M;
    int m_K;
    double m_beta;
    std::vector<double> m_taps;
};

// Achieved filter metrics, computed from the same core the engine uses.
struct PfbFilterMetrics {
    double passband_halfwidth_ch = 0.0; // -3 dB half width, in channel units
    double edge_loss_db = 0.0;          // |H(0.5)| in dB (~ -6 for all designs)
    double adjacent_rejection_db = 0.0; // |H(1.0)| in dB; compared to the target
    double far_floor_db = 0.0;          // max |H(x)| over x in [1.0, 1.5], dB
    int total_taps = 0;                 // N = M*K
    double flat_noise_tilt_db = 0.0;    // 10*log10( int_{-1..1} H(x)^2 dx )
};

PfbFilterMetrics computePfbMetrics(const PfbFilterDesign &design);

// rejection_db is a negative dB magnitude (e.g. -83.2). target_db is the
// positive required suppression (e.g. 80). Meets => |rejection| >= target_db;
// Within10Db => |rejection| >= target_db - 10; Misses otherwise.
enum class RejectionStatus { Meets, Within10Db, Misses };
RejectionStatus compareRejection(double rejection_db, double target_db);

// Short manual-guidance string. Empty when the rejection target is met.
std::string pfbGuidanceText(const PfbFilterDesign &design,
                            const PfbFilterMetrics &metrics, double target_db);
```

- [ ] **Step 2: Write the failing taps test**

`tests/test_pfb_filter_design.cpp`:

```cpp
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "pfb_filter_design.h"

using Catch::Approx;

TEST_CASE("PfbFilterDesign taps length and symmetry", "[pfb_filter_design]") {
    PfbFilterDesign d(32, 8, 8.0);
    REQUIRE(d.tapCount() == 256);
    REQUIRE(d.channelCount() == 32);
    REQUIRE(d.tapsPerBranch() == 8);

    const auto &taps = d.taps();
    // Kaiser windowed-sinc is symmetric about (N-1)/2 -> linear phase.
    for (int i = 0; i < 256; ++i)
        REQUIRE(taps[i] == Approx(taps[255 - i]).margin(1e-12));

    // DC normalization: h[n] sums to exactly 1 (H(0) = 1).
    double sum = 0.0;
    for (double v : taps) {
        sum += v;
        REQUIRE(std::isfinite(v));
    }
    REQUIRE(sum == Approx(1.0).margin(1e-9));
}

TEST_CASE("PfbFilterDesign constructor clamps parameters", "[pfb_filter_design]") {
    PfbFilterDesign d(1, 0, -5.0);   // below engine minima
    REQUIRE(d.channelCount() == 2);  // M clamp floor
    REQUIRE(d.tapsPerBranch() == 1); // K clamp floor
    REQUIRE(d.beta() == 0.0);
    PfbFilterDesign e(9999, 99, 99.0);
    REQUIRE(e.channelCount() == 2048);
    REQUIRE(e.tapsPerBranch() == 64);
    REQUIRE(e.beta() == 20.0);
}
```

- [ ] **Step 3: Run it to verify it fails**

Run: `cmake -S . -B build` (first time only) then `cmake --build build --target test_pfb_filter_design` and `ctest --test-dir build -R 'test_pfb_filter_design' --output-on-failure`
Expected: FAIL — `pfb_filter_design.h` does not exist / target not defined.

- [ ] **Step 4: Implement taps synthesis**

`pfb_channelizer/src/pfb_filter_design.cpp`:

```cpp
#define _USE_MATH_DEFINES
#include "pfb_filter_design.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
double kaiserWindowAt(double index, int N, double beta) {
    // Symmetric Kaiser window over taps 0..N-1. index in [0, N-1].
    const double x = 2.0 * index / (N - 1) - 1.0; // -1..1
    if (std::abs(x) > 1.0)
        return 0.0;
    const double arg = beta * std::sqrt(std::max(0.0, 1.0 - x * x));
    return std::cyl_bessel_i(0, arg) / std::cyl_bessel_i(0, beta);
}
double sincP(double v) {
    return std::abs(v) < 1e-12 ? 1.0 : std::sin(M_PI * v) / (M_PI * v);
}
double toDb(double v) { return 20.0 * std::log10(std::max(v, 1e-300)); }
} // namespace

PfbFilterDesign::PfbFilterDesign(int M, int K, double beta) {
    m_M = std::clamp(M, 2, 2048);
    m_K = std::clamp(K, 1, 64);
    m_beta = std::clamp(beta, 0.0, 20.0);
    synthesize();
}

void PfbFilterDesign::synthesize() {
    const int N = m_M * m_K;
    const double center = (N - 1) / 2.0;
    m_taps.assign(N, 0.0);
    double sum = 0.0;
    for (int n = 0; n < N; ++n) {
        // Ideal lowpass cutoff at Fs/(2*M): 2*fc = 1/M normalized to Fs.
        const double u = (n - center) / m_M;
        m_taps[n] = kaiserWindowAt(n, N, m_beta) * (1.0 / m_M) * sincP(u);
        sum += m_taps[n];
    }
    // Normalize DC gain to exactly 1 (H(0) = 1).
    if (sum != 0.0) {
        for (double &v : m_taps)
            v /= sum;
    }
}

// responseAt is declared now and completed in Task 2.
double PfbFilterDesign::responseAt(double) const { return 1.0; }
```

Register the source in `pfb_channelizer/CMakeLists.txt` — replace:

```cmake
add_library(pfb_channelizer_engine STATIC src/pfb_channelizer_engine.cpp)
```

with:

```cmake
add_library(pfb_channelizer_engine STATIC
    src/pfb_channelizer_engine.cpp
    src/pfb_filter_design.cpp
)
```

Register the standalone test in `tests/CMakeLists.txt` — append after the `test_adc_configuration` block at the end of the file:

```cmake
# Standalone for the same MinGW-w64 registration-ceiling reason as
# test_component_authoring: new TEST_CASEs must not join the main `tests` binary.
add_standalone_test(test_pfb_filter_design
    SOURCES test_pfb_filter_design.cpp
    LIBS simulator::pfb_channelizer_engine simulator::node_graph_engine
)
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build --target test_pfb_filter_design` then `ctest --test-dir build -R 'test_pfb_filter_design' --output-on-failure`
Expected: PASS (2 test cases). The Task 2 `responseAt` placeholder does not affect these assertions.

- [ ] **Step 6: Commit**

```bash
git add pfb_channelizer/include/pfb_filter_design.h pfb_channelizer/src/pfb_filter_design.cpp
git add pfb_channelizer/CMakeLists.txt tests/test_pfb_filter_design.cpp tests/CMakeLists.txt
git commit -m "feat(pfb): shared real-prototype core, taps synthesis"
```

---
### Task 2: Prototype core — response evaluation

**Files:**
- Modify: `pfb_channelizer/src/pfb_filter_design.cpp` (replace the `responseAt` placeholder)
- Modify: `tests/test_pfb_filter_design.cpp` (add response TEST_CASEs)

**Interfaces:**
- Consumes: `PfbFilterDesign` from Task 1.
- Produces: working `double PfbFilterDesign::responseAt(double x) const` — |H(x)| via direct DTFT over the taps.

- [ ] **Step 1: Write the failing response tests**

Append to `tests/test_pfb_filter_design.cpp`:

```cpp
TEST_CASE("PfbFilterDesign response: normalization and symmetry", "[pfb_filter_design]") {
    PfbFilterDesign d(32, 8, 8.0);
    REQUIRE(d.responseAt(0.0) == Approx(1.0).margin(1e-9));
    REQUIRE(d.responseAt(0.3) == Approx(d.responseAt(-0.3)).margin(1e-9));
    REQUIRE(d.responseAt(1.2) == Approx(d.responseAt(-1.2)).margin(1e-9));
}

TEST_CASE("PfbFilterDesign response: band edge and adjacent center", "[pfb_filter_design]") {
    auto db = [](double v) { return 20.0 * std::log10(v); };

    PfbFilterDesign a(32, 8, 8.0);
    REQUIRE(db(a.responseAt(0.5)) == Approx(-6.02).margin(0.2)); // band edge
    REQUIRE(db(a.responseAt(1.0)) < -40.0);                     // adjacent center

    PfbFilterDesign b(32, 16, 12.0);
    REQUIRE(db(b.responseAt(1.0)) < -100.0); // deeper stopband
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake --build build --target test_pfb_filter_design` then `ctest --test-dir build -R 'test_pfb_filter_design' --output-on-failure`
Expected: FAIL — `responseAt` returns 1.0 for every x (normalization/symmetry and dB assertions fail).

- [ ] **Step 3: Implement the DTFT evaluation**

In `pfb_channelizer/src/pfb_filter_design.cpp`, replace the placeholder:

```cpp
// responseAt is declared now and completed in Task 2.
double PfbFilterDesign::responseAt(double) const { return 1.0; }
```

with:

```cpp
double PfbFilterDesign::responseAt(double x) const {
    // DTFT magnitude of the real taps: H(x) = sum_n h[n] * exp(-j*2*pi*(x/M)*n).
    const double norm = x / m_M; // frequency in cycles/sample
    double re = 0.0;
    double im = 0.0;
    for (int n = 0; n < static_cast<int>(m_taps.size()); ++n) {
        const double ph = 2.0 * M_PI * norm * n;
        re += m_taps[n] * std::cos(ph);
        im += m_taps[n] * std::sin(ph);
    }
    return std::hypot(re, im);
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build build --target test_pfb_filter_design` then `ctest --test-dir build -R 'test_pfb_filter_design' --output-on-failure`
Expected: PASS (4 test cases). Band-edge loss ≈ −6.02 dB; adjacent rejection < −40 dB @ (32,8,8) and < −100 dB @ (32,16,12).

- [ ] **Step 5: Commit**

```bash
git add pfb_channelizer/src/pfb_filter_design.cpp tests/test_pfb_filter_design.cpp
git commit -m "feat(pfb): prototype response evaluation (DTFT over taps)"
```

---
### Task 3: Metrics, target comparison, guidance

**Files:**
- Modify: `pfb_channelizer/src/pfb_filter_design.cpp` (implement `computePfbMetrics`, `compareRejection`, `pfbGuidanceText`)
- Modify: `tests/test_pfb_filter_design.cpp` (add metric/guidance TEST_CASEs)

**Interfaces:**
- Consumes: `PfbFilterMetrics`, `RejectionStatus`, `pfbGuidanceText` declared in Task 1; `PfbFilterDesign` from Tasks 1–2.
- Produces: working metric functions used by the widget (Task 5).

- [ ] **Step 1: Write the failing metric tests**

Append to `tests/test_pfb_filter_design.cpp`:

```cpp
TEST_CASE("PfbFilterMetrics reference configs", "[pfb_filter_design]") {
    auto m = computePfbMetrics(PfbFilterDesign(32, 8, 8.0));
    REQUIRE(m.passband_halfwidth_ch > 0.30);
    REQUIRE(m.passband_halfwidth_ch < 0.49);
    REQUIRE(m.edge_loss_db == Approx(-6.0).margin(1.0));
    REQUIRE(m.adjacent_rejection_db < -40.0);
    REQUIRE(m.far_floor_db < -40.0);
    REQUIRE(m.total_taps == 256);
    REQUIRE(m.flat_noise_tilt_db > -1.5);
    REQUIRE(m.flat_noise_tilt_db < -0.1);

    // Deeper stopband with more taps/branch and a stronger window.
    auto m2 = computePfbMetrics(PfbFilterDesign(32, 16, 12.0));
    REQUIRE(m2.adjacent_rejection_db < -100.0);
}

TEST_CASE("PfbFilterMetrics rejection comparison and guidance", "[pfb_filter_design]") {
    PfbFilterDesign d(32, 8, 8.0);
    auto m = computePfbMetrics(d);
    const double achieved = -m.adjacent_rejection_db; // positive magnitude

    REQUIRE(compareRejection(m.adjacent_rejection_db, achieved) == RejectionStatus::Meets);
    REQUIRE(pfbGuidanceText(d, m, achieved).empty());

    // A much harder target must produce a non-empty, K/beta-referencing hint.
    REQUIRE(compareRejection(m.adjacent_rejection_db, 140.0) == RejectionStatus::Misses);
    std::string hint = pfbGuidanceText(d, m, 140.0);
    REQUIRE(!hint.empty());
    REQUIRE(hint.find("K") != std::string::npos);

    // A target 6 dB above achieved is Within10Db.
    REQUIRE(compareRejection(m.adjacent_rejection_db, achieved + 6.0) ==
            RejectionStatus::Within10Db);
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake --build build --target test_pfb_filter_design` then `ctest --test-dir build -R 'test_pfb_filter_design' --output-on-failure`
Expected: FAIL — metric functions are declared but not defined (link error) / guidance empty.

- [ ] **Step 3: Implement metrics, comparison, and guidance**

Append to `pfb_channelizer/src/pfb_filter_design.cpp`:

```cpp
PfbFilterMetrics computePfbMetrics(const PfbFilterDesign &design) {
    PfbFilterMetrics m;
    m.total_taps = design.tapCount();
    m.edge_loss_db = toDb(design.responseAt(0.5));
    m.adjacent_rejection_db = toDb(design.responseAt(1.0));

    // Far-adjacent floor: worst response over x in [1.0, 1.5].
    double floor_db = -1e300;
    for (int i = 0; i <= 50; ++i) {
        floor_db = std::max(floor_db, toDb(design.responseAt(1.0 + 0.01 * i)));
    }
    m.far_floor_db = floor_db;

    // -3 dB half width: first crossing scanning outward from DC. The response
    // is monotonically decreasing through the main lobe, so the first sample
    // at or below -3 dB is the crossing.
    m.passband_halfwidth_ch = 1.0; // degenerate fallback (not reached for sane K)
    for (int i = 1; i <= 1000; ++i) {
        const double x = 0.001 * i;
        if (toDb(design.responseAt(x)) <= -3.0) {
            m.passband_halfwidth_ch = x;
            break;
        }
    }

    // Flat-noise tilt: integral of H(x)^2 over the |x| <= 1 slice the engine
    // integrates. ~ -0.6 dB for the corrected model (old narrow model: -9 dB).
    const int steps = 1000;
    double acc = 0.0;
    for (int i = 0; i <= steps; ++i) {
        const double h = design.responseAt(-1.0 + 2.0 * i / steps);
        acc += h * h;
    }
    acc *= 2.0 / steps;
    m.flat_noise_tilt_db = 10.0 * std::log10(std::max(acc, 1e-300));
    return m;
}

RejectionStatus compareRejection(double rejection_db, double target_db) {
    const double achieved = -rejection_db; // positive magnitude
    if (achieved >= target_db)
        return RejectionStatus::Meets;
    if (achieved >= target_db - 10.0)
        return RejectionStatus::Within10Db;
    return RejectionStatus::Misses;
}

std::string pfbGuidanceText(const PfbFilterDesign &design,
                            const PfbFilterMetrics &metrics, double target_db) {
    if (compareRejection(metrics.adjacent_rejection_db, target_db) == RejectionStatus::Meets)
        return {};
    char buf[192];
    std::snprintf(buf, sizeof(buf),
                  "Adjacent rejection %.1f dB is short of the %.0f dB target. "
                  "Raise K (%d -> more) to narrow the transition band so the "
                  "stopband starts closer to the channel edge, or raise beta "
                  "(%.1f -> up to 20) to deepen the stopband floor.",
                  -metrics.adjacent_rejection_db, target_db, design.tapsPerBranch(),
                  design.beta());
    return buf;
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build build --target test_pfb_filter_design` then `ctest --test-dir build -R 'test_pfb_filter_design' --output-on-failure`
Expected: PASS (6 test cases).

- [ ] **Step 5: Commit**

```bash
git add pfb_channelizer/src/pfb_filter_design.cpp tests/test_pfb_filter_design.cpp
git commit -m "feat(pfb): filter metrics, rejection comparison, guidance text"
```

---
### Task 4: Engine delegates to the shared prototype

**Files:**
- Modify: `pfb_channelizer/include/pfb_channelizer_engine.h` (delete `prototypeResponse`/`kaiserWindow`; include + hold `PfbFilterDesign`)
- Modify: `pfb_channelizer/src/pfb_channelizer_engine.cpp` (recompute weights from the design)
- Modify: `tests/test_pfb_filter_design.cpp` (engine regression TEST_CASEs — written first, failing)

**Interfaces:**
- Consumes: `PfbFilterDesign` (Tasks 1–2).
- Produces: engine whose channel weights equal the core's `responseAt(offset / channel_bw)`; identical M/K/beta/serialization semantics.

- [ ] **Step 1: Write the failing engine regression tests**

Append to `tests/test_pfb_filter_design.cpp` (add these includes at the top of the file if not present):

```cpp
#include "node_graph_engine.h"
#include "pfb_channelizer_engine.h"
#include <vector>
```

```cpp
TEST_CASE("PFB engine tiles flat noise (regression vs narrow model)", "[pfb_filter_design]") {
    NodeGraphEngine graph;
    PFBChannelizerEngine pfb(0, graph);

    Spectrum in;
    in.frequencies.resize(401);
    for (int i = 0; i < 401; ++i)
        in.frequencies[i] = -200e6 + i * 1e6;
    in.noise_total_W.assign(401, 1e-20);

    pfb.setFs_Hz(400e6);
    pfb.node().inputs[0] = &in;
    pfb.update(0.0);

    const auto &out = pfb.node().outputs[1];
    REQUIRE(out.noise_W.size() == in.frequencies.size());
    double sum = 0.0;
    for (double v : out.noise_W)
        sum += v;
    const double mean = sum / out.noise_W.size();
    // Corrected prototype tiles to within ~1 dB of the input PSD.
    // The old narrow model produced ~0.12 * 1e-20 and fails this bound.
    REQUIRE(mean > 0.5e-20);
    REQUIRE(mean < 1.5e-20);
}

TEST_CASE("PFB engine tone weights match the shared prototype", "[pfb_filter_design]") {
    NodeGraphEngine graph;
    PFBChannelizerEngine pfb(0, graph); // defaults M=32, K=8, beta=8

    Spectrum in;
    in.frequencies.resize(401);
    for (int i = 0; i < 401; ++i)
        in.frequencies[i] = -200e6 + i * 1e6;
    in.noise_total_W.assign(401, 1e-20);

    pfb.setFs_Hz(400e6); // channel_bw = 12.5 MHz; ch16 center = 6.25 MHz
    pfb.node().inputs[0] = &in;
    pfb.update(0.0);

    const auto &chs = pfb.channels();
    // Center tone passes at unity gain...
    REQUIRE(chs[16].tones.size() == 1);
    REQUIRE(chs[16].tones[0].power_dBm == Approx(-30.0).margin(0.5));
    // ...and leaks into the adjacent channels at the prototype's |H(1.0)|
    // (~ -83 dB), not at the old model's exact -300 dB null.
    REQUIRE(chs[15].tones.size() == 1);
    REQUIRE(chs[15].tones[0].power_dBm > -88.0);
    REQUIRE(chs[15].tones[0].power_dBm < -78.0);
}
```

`Spectrum` comes from `common/spectrum.h`, already included transitively by `pfb_channelizer_engine.h`; add `#include "spectrum.h"` if the compiler requires it.

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake --build build --target test_pfb_filter_design` then `ctest --test-dir build -R 'test_pfb_filter_design' --output-on-failure`
Expected: FAIL — flat-noise mean ≈ 0.12e-20 (< 0.5e-20 bound); adjacent leak ≈ −300 dB (< −88 fails).

- [ ] **Step 3: Swap the engine to the shared core**

In `pfb_channelizer/include/pfb_channelizer_engine.h`:
- add `#include "pfb_filter_design.h"` to the includes;
- in the private section, delete the two method declarations:

```cpp
    void recomputeChannels(const std::vector<double> &freqs);
    double prototypeResponse(double offset_Hz) const;
    double kaiserWindow(double x) const;
```

→

```cpp
    void recomputeChannels(const std::vector<double> &freqs);
    // Shared real prototype; rebuilt whenever M/K/beta change. The single
    // source of truth for channel weights (also used by the Filter Calculator).
    PfbFilterDesign m_design{32, 8, 8.0};
```

In `pfb_channelizer/src/pfb_channelizer_engine.cpp`:
- delete the two function bodies `prototypeResponse` and `kaiserWindow` entirely (they are the only other users of `M_PI`/`std::cyl_bessel_i` in this file; `_USE_MATH_DEFINES` can stay);
- at the top of `recomputeChannels`, rebuild the design from the current config:

```cpp
void PFBChannelizerEngine::recomputeChannels(const std::vector<double> &freqs) {
    m_design = PfbFilterDesign(m_cfg.M, m_cfg.K, m_cfg.beta);
    double channel_bw = m_cfg.Fs_Hz / m_cfg.M;
```

- replace the weight push inside the bin loop:

```cpp
                ch.bin_weights.push_back(prototypeResponse(offset));
```

→

```cpp
                ch.bin_weights.push_back(m_design.responseAt(offset / channel_bw));
```

- in `update()`, replace the live tone-path call (inside the tone loop over `in_ptr->tones`):

```cpp
                double w = prototypeResponse(offset);
```

→

```cpp
                double w = m_design.responseAt(offset / (m_cfg.Fs_Hz / m_cfg.M));
```

(That tone path only runs after the cache guard has triggered `recomputeChannels` for any M/K/beta/Fs/grid change, so `m_design` is always current there.)

- [ ] **Step 4: Run the regression tests to verify they pass**

Run: `cmake --build build --target test_pfb_filter_design` then `ctest --test-dir build -R 'test_pfb_filter_design' --output-on-failure`
Expected: PASS (8 test cases).

- [ ] **Step 5: Run the existing PFB suite (main tests binary) to check the sweep**

Run: `cmake --build build --target tests` then `ctest --test-dir build -R 'PFB|pfb' --output-on-failure`
Expected: PASS. The old assertions (`tone routing`, `noise distribution`, `flatness ripple < 1%`, `recompute on K/beta change`, issue 37/70 reconnect tests) are behavior-level and survive; if any single expectation fails because it encoded the old numeric shape, update only that expectation to the corrected-model value shown by the failing output — do not weaken bounds into vacuous checks.

- [ ] **Step 6: Commit**

```bash
git add pfb_channelizer/include/pfb_channelizer_engine.h pfb_channelizer/src/pfb_channelizer_engine.cpp
git add tests/test_pfb_filter_design.cpp
git commit -m "refactor(pfb): engine channel weights from shared real prototype"
```

---
### Task 5: Filter Calculator widget

**Files:**
- Create: `app/include/pfb_calculator_widget.h`
- Create: `app/src/pfb_calculator_widget.cpp`
- Modify: `app/CMakeLists.txt` (add the .cpp to the `app` library)

**Interfaces:**
- Consumes: `PfbFilterDesign`, `computePfbMetrics`, `compareRejection`, `RejectionStatus`, `pfbGuidanceText` (Tasks 1–3); `PFBChannelizerEngine` setters/getters; `ComponentRegistry::byType<PFBChannelizerEngine>()`; ImNodes graph selection (same pattern as `InspectorPanel::findSelected`).
- Produces: `class PfbCalculatorWidget` with `draw(const char* title, bool* p_open)`, `std::function<void()> onParamChange`. Wired into the app in Task 6.

- [ ] **Step 1: Write the header**

`app/include/pfb_calculator_widget.h`:

```cpp
#pragma once

#include <functional>

class NodeGraphEngine;
class ComponentRegistry;
class PFBChannelizerEngine;
class PfbFilterDesign;
struct ImDrawList;
struct ImVec2;

// Dockable PFB filter calculator: shows the achieved prototype metrics for
// M/K/beta against a rejection target, plots the response, and applies M/K/beta
// to a targeted PFB via the engine's existing setters. Pure DSP lives in the
// pfb_channelizer core (PfbFilterDesign); this widget only binds/renders.
class PfbCalculatorWidget {
  public:
    PfbCalculatorWidget(NodeGraphEngine &graph, ComponentRegistry &components);
    void draw(const char *title, bool *p_open = nullptr);
    std::function<void()> onParamChange; // fired after Apply writes engine params

  private:
    PFBChannelizerEngine *resolveTarget(const std::vector<PFBChannelizerEngine *> &pfbs);
    void pullFrom(PFBChannelizerEngine &pfb);
    void drawPlot(ImDrawList *dl, const ImVec2 &origin, float w, float h,
                  const PfbFilterDesign &design);

    NodeGraphEngine *m_graph;
    ComponentRegistry *m_components;
    int m_M = 32;
    int m_K = 8;
    float m_beta = 8.0f;    // float for ImGui::SliderFloat
    float m_target_db = 80.0f;
    int m_target_index = -1; // -1 = auto: follow a single graph-selected PFB
    int m_bound_pfb_id = -1; // graph node id the controls were pulled from
};
```

- [ ] **Step 2: Write the implementation**

`app/src/pfb_calculator_widget.cpp`:

```cpp
#include "pfb_calculator_widget.h"
#include "component_registry.h"
#include "imgui.h"
#include "imnodes.h"
#include "pfb_channelizer_engine.h"
#include "pfb_filter_design.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {
ImVec4 statusColor(RejectionStatus s) {
    switch (s) {
    case RejectionStatus::Meets:
        return ImVec4(0.35f, 1.0f, 0.35f, 1.0f);
    case RejectionStatus::Within10Db:
        return ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
    case RejectionStatus::Misses:
    default:
        return ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
    }
}
} // namespace

PfbCalculatorWidget::PfbCalculatorWidget(NodeGraphEngine &graph, ComponentRegistry &components)
    : m_graph(&graph), m_components(&components) {}

PFBChannelizerEngine *
PfbCalculatorWidget::resolveTarget(const std::vector<PFBChannelizerEngine *> &pfbs) {
    if (pfbs.empty()) {
        m_target_index = -1;
        return nullptr;
    }
    // Explicit combo choice wins.
    if (m_target_index >= 0 && m_target_index < static_cast<int>(pfbs.size()))
        return pfbs[m_target_index];
    m_target_index = -1;
    // Auto: a single graph-selected node that is a PFB.
    if (ImNodes::NumSelectedNodes() == 1) {
        int selected_id = -1;
        ImNodes::GetSelectedNodes(&selected_id);
        auto *engine = m_components->find(selected_id);
        if (engine && engine->type_name() == "pfb") {
            for (auto *p : pfbs)
                if (p->id() == selected_id)
                    return p;
        }
    }
    return pfbs.front();
}

void PfbCalculatorWidget::pullFrom(PFBChannelizerEngine &pfb) {
    m_M = pfb.channelCount();
    m_K = pfb.tapsPerBranch();
    m_beta = pfb.kaiserBeta();
}

void PfbCalculatorWidget::draw(const char *title, bool *p_open) {
    ImGui::SetNextWindowSize(ImVec2(820, 600), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title, p_open)) {
        ImGui::End();
        return;
    }

    auto pfbs = m_components->byType<PFBChannelizerEngine>();
    PFBChannelizerEngine *target = resolveTarget(pfbs);

    // Pull-on-retarget: whenever the bound target changes, load its current
    // M/K/beta so the panel shows reality, not last-edited values.
    const int target_id = target ? target->id() : -1;
    if (target_id != m_bound_pfb_id) {
        if (target)
            pullFrom(*target);
        m_bound_pfb_id = target_id;
    }
    if (!target)
        m_bound_pfb_id = -1;

    // --- Left column: controls -------------------------------------------
    ImGui::BeginGroup();
    ImGui::BeginChild("##calc_controls", ImVec2(300, 0), false);

    std::string preview = "Auto (graph selection)";
    if (m_target_index >= 0 && m_target_index < static_cast<int>(pfbs.size()))
        preview = "PFB " + std::to_string(pfbs[m_target_index]->id());
    if (ImGui::BeginCombo("Target PFB", preview.c_str())) {
        if (ImGui::Selectable("Auto (graph selection)", m_target_index < 0))
            m_target_index = -1;
        for (int i = 0; i < static_cast<int>(pfbs.size()); ++i) {
            const bool selected = (m_target_index == i);
            const std::string label = "PFB " + std::to_string(pfbs[i]->id());
            if (ImGui::Selectable(label.c_str(), selected))
                m_target_index = i;
        }
        ImGui::EndCombo();
    }
    ImGui::Separator();

    bool edited = false;
    edited |= ImGui::SliderInt("Channels M", &m_M, 2, 2048);
    edited |= ImGui::SliderInt("Taps/branch K", &m_K, 1, 64);
    edited |= ImGui::SliderFloat("Kaiser beta", &m_beta, 0.0f, 20.0f, "%.2f");
    edited |= ImGui::SliderFloat("Target rejection (dB)", &m_target_db, 20.0f, 140.0f, "%.0f");
    (void)edited;

    if (target) {
        if (ImGui::Button("Apply to PFB")) {
            target->setChannelCount(m_M);
            target->setTapsPerBranch(m_K);
            target->setKaiserBeta(m_beta);
            if (onParamChange)
                onParamChange();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("writes M/K/beta to PFB %d", target->id());
        if (target->fs_Hz() > 0.0)
            ImGui::Text("Input Fs: %.3f MHz", target->fs_Hz() / 1e6);
        else
            ImGui::TextDisabled("PFB has no input yet (Fs unknown)");
    } else {
        ImGui::TextDisabled("Design only: add a PFB to the graph to enable Apply.");
    }
    ImGui::EndChild();
    ImGui::EndGroup();

    // --- Right column: plot + metrics -------------------------------------
    ImGui::SameLine();
    ImGui::BeginChild("##calc_analysis", ImVec2(0, 0), true);

    const PfbFilterDesign design(m_M, m_K, m_beta);
    const PfbFilterMetrics metrics = computePfbMetrics(design);

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float plot_h = std::max(avail.y - 190.0f, 80.0f);
    drawPlot(ImGui::GetWindowDrawList(), origin, avail.x, plot_h, design);
    ImGui::Dummy(ImVec2(avail.x, plot_h));

    ImGui::Separator();
    const RejectionStatus st = compareRejection(metrics.adjacent_rejection_db, m_target_db);
    ImGui::Text("Adjacent rejection (H at x=1.0):  ");
    ImGui::SameLine();
    ImGui::TextColored(statusColor(st), "%.1f dB  (target %.0f dB)",
                       -metrics.adjacent_rejection_db, m_target_db);

    ImGui::Text("-3 dB half-width:  %.3f channel", metrics.passband_halfwidth_ch);
    ImGui::Text("Band-edge loss (H at x=0.5):  %.1f dB", metrics.edge_loss_db);
    ImGui::Text("Far-adjacent floor (x in 1.0..1.5):  %.1f dB", metrics.far_floor_db);
    ImGui::Text("Prototype taps N = M*K:  %d", metrics.total_taps);
    ImGui::Text("Flat-noise tilt:  %.2f dB", metrics.flat_noise_tilt_db);

    const std::string hint = pfbGuidanceText(design, metrics, m_target_db);
    if (!hint.empty()) {
        ImGui::TextWrapped("Hint: %s", hint.c_str());
    } else {
        ImGui::TextColored(statusColor(st), "Rejection target met.");
    }
    ImGui::EndChild();

    ImGui::End();
}

void PfbCalculatorWidget::drawPlot(ImDrawList *dl, const ImVec2 &origin, float w, float h,
                                   const PfbFilterDesign &design) {
    const float kTop = 8.0f, kBottom = 20.0f, kLeft = 12.0f, kRight = 8.0f;
    if (w <= kLeft + kRight || h <= kTop + kBottom)
        return;

    // Sample |H| in dB over x in [0, 1.5].
    const int kSamples = 151;
    std::vector<float> db(kSamples);
    double y_max = 5.0, y_min = -160.0;
    for (int i = 0; i < kSamples; ++i) {
        const double x = 1.5 * i / (kSamples - 1);
        const double v = 20.0 * std::log10(std::max(design.responseAt(x), 1e-300));
        db[i] = static_cast<float>(v);
        y_min = std::min(y_min, v);
    }
    // Keep the target line and stopband in view but bound the floor.
    const double target_db = std::max(0.0, m_target_db);
    y_min = std::max(y_min, -std::max(target_db + 12.0, 60.0));

    auto x_px = [&](double x) { return origin.x + kLeft + static_cast<float>((x / 1.5) * (w - kLeft - kRight)); };
    auto y_px = [&](double v) {
        const double span = y_max - y_min;
        const float f = static_cast<float>((v - y_min) / span);
        return origin.y + kTop + (1.0f - f) * (h - kTop - kBottom);
    };

    // Grid + markers: channel edge x=0.5, adjacent center x=1.0.
    const unsigned grid_col = IM_COL32(120, 120, 120, 120);
    const unsigned edge_col = IM_COL32(200, 200, 80, 160);
    const unsigned target_col = IM_COL32(255, 90, 220, 200);
    const unsigned curve_col = IM_COL32(110, 200, 255, 255);
    dl->AddLine(ImVec2(x_px(0.0), y_px(0.0)), ImVec2(x_px(1.5), y_px(0.0)), grid_col);
    dl->AddLine(ImVec2(x_px(0.5), origin.y + kTop), ImVec2(x_px(0.5), origin.y + h - kBottom),
                edge_col);
    dl->AddLine(ImVec2(x_px(1.0), origin.y + kTop), ImVec2(x_px(1.0), origin.y + h - kBottom),
                edge_col);
    dl->AddLine(ImVec2(x_px(0.0), y_px(-m_target_db)),
                ImVec2(x_px(1.5), y_px(-m_target_db)), target_col);
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(x_px(0.5) - 10.0f, origin.y + h - kBottom + 2.0f), edge_col, "0.5 edge");
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(x_px(1.0) - 10.0f, origin.y + h - kBottom + 2.0f), edge_col, "1.0 adj");

    std::vector<ImVec2> pts;
    pts.reserve(kSamples);
    for (int i = 0; i < kSamples; ++i)
        pts.emplace_back(x_px(1.5 * i / (kSamples - 1)), y_px(db[i]));
    dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), curve_col, 0, 1.6f);
}
```

- [ ] **Step 3: Register the source in the app library**

In `app/CMakeLists.txt`, add to the `add_library(app STATIC ...)` source list (after `src/pfb_view_manager.cpp`):

```cmake
    src/pfb_calculator_widget.cpp
```

- [ ] **Step 4: Build to verify it compiles**

Run: `cmake --build build --target app`
Expected: compiles clean (no link step needed for the static lib; `simulator::app` consumers rebuild in Task 6).

- [ ] **Step 5: Commit**

```bash
git add app/include/pfb_calculator_widget.h app/src/pfb_calculator_widget.cpp app/CMakeLists.txt
git commit -m "feat(app): PFB filter calculator widget (plot, metrics, apply)"
```

---
### Task 6: Wire the calculator into the app shell

**Files:**
- Modify: `app/include/app.h`
- Modify: `app/src/app.cpp`

**Interfaces:**
- Consumes: `PfbCalculatorWidget` from Task 5.
- Produces: View-menu entry, dockable window draw, window-state persistence, dirty marking on Apply.

- [ ] **Step 1: Declare the member**

In `app/include/app.h`:
- add `#include "pfb_calculator_widget.h"` with the other widget includes;
- near the existing `std::unique_ptr<LibraryBrowserWidget> m_library_browser;` members add:

```cpp
    std::unique_ptr<PfbCalculatorWidget> m_calculator_widget;
    bool m_show_calculator = false;
```

(Placement does not matter for these two; construction happens in the ctor body after all referenced members exist.)

- [ ] **Step 2: Construct it and bind dirty marking**

In `app/src/app.cpp`, in the constructor after the `m_na_widget->onParamChange = ...` block, add:

```cpp
    m_calculator_widget = std::make_unique<PfbCalculatorWidget>(m_graph_engine, m_components);
    m_calculator_widget->onParamChange = [this]() { markDirty(); };
```

- [ ] **Step 3: Window-state persistence**

In `app/src/app.cpp` `load_window_states()`, after the `m_show_help` line, add:

```cpp
    m_show_calculator = m_state.loadBool("WindowState", "FilterCalculator", false);
```

In the destructor, after the `m_show_help` save line, add:

```cpp
    m_state.saveBool("WindowState", "FilterCalculator", m_show_calculator);
```

- [ ] **Step 4: View menu entry**

In `app/src/app.cpp` View menu, after the `Component Library` MenuItem, add:

```cpp
            ImGui::MenuItem("Filter Calculator", nullptr, &m_show_calculator);
```

- [ ] **Step 5: Draw the window**

In `app/src/app.cpp` `draw_ui()`, after the Component Library draw block (before `drawExtensionsPanel();`), add:

```cpp
    if (m_show_calculator && m_calculator_widget) {
        m_calculator_widget->draw("Filter Calculator", &m_show_calculator);
    }
```

- [ ] **Step 6: Build and smoke-check**

Run: `cmake --build build --target tiny-rf-simulator`
Expected: builds clean.

Manual smoke (launch the app): open View → Filter Calculator; with no PFB added the window shows controls and "Design only" and no crash; add a PFB (and wire an ADC/generator input), select it in the node graph, verify the target follows the selection and the M/K/beta controls pull from the engine; drag K/beta and confirm the metrics/plot update; press Apply and confirm the PFB inspector values change and the project becomes dirty (title asterisk).

- [ ] **Step 7: Commit**

```bash
git add app/include/app.h app/src/app.cpp
git commit -m "feat(app): dockable PFB filter calculator window"
```

---
### Task 7: Docs, DOX pass, full verification

**Files:**
- Modify: `ROADMAP.md` (completed row)
- Modify: `tests/AGENTS.md` (mention the new standalone test)
- Modify: `app/AGENTS.md` (mention the new widget file, if it enumerates widget files)

- [ ] **Step 1: ROADMAP row**

In `ROADMAP.md`, append a completed row:

```markdown
| 22 | **PFB filter calculator + real-prototype engine model** — dockable filter calculator tool with prototype response plot, achieved metrics vs rejection target, and Apply-to-selected-PFB; engine channel weights now come from a shared real windowed-sinc Kaiser prototype (single source of truth with the tool) | ✅ Completed | PfbFilterDesign core in pfb_channelizer; old analytic prototypeResponse/kaiserWindow removed; serialization unchanged; standalone tests in test_pfb_filter_design. |
```

- [ ] **Step 2: Tests AGENTS ownership**

In `tests/AGENTS.md`, in the Ownership list after the per-module test-file bullet, add:

```markdown
- **test_pfb_filter_design.cpp** — standalone executable: real-prototype core + metric tests and PFB engine flat-noise/tone-weight regressions (kept out of the main `tests` binary for the MinGW registration ceiling)
```

- [ ] **Step 3: App AGENTS check**

Read `app/AGENTS.md`; if it enumerates widget source files, add `pfb_calculator_widget.{h,cpp}` (dockable PFB filter calculator, no engine, DSP core lives in pfb_channelizer) to the relevant list. If it does not enumerate per-file, leave it unchanged and note that in the commit message.

- [ ] **Step 4: Full build + full test suite**

Run:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
scripts/format.sh --check
```

Expected: all tests pass (including the existing PFB cases in the main `tests` binary and the 8 new standalone cases), format check clean. Fix any format drift with `scripts/format.sh` before committing.

- [ ] **Step 5: Commit**

```bash
git add ROADMAP.md tests/AGENTS.md app/AGENTS.md
git commit -m "docs: PFB filter calculator roadmap + DOX index updates"
```
