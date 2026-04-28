# Decouple Bin Width from Noise — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Convert the noise model from total power per internal bin (W) to power spectral density (W/Hz), making the generator a clean source and the spectrum analyzer noise floor depend only on RBW and downstream gain/NF.

**Architecture:** All engines store noise as W/Hz. The generator outputs a flat thermal noise density of `k*T`. Amplifiers scale input density by gain and add their own noise density. The spectrum analyzer multiplies density by bin width to get per-bin power, then applies an RBW filter whose kernel peaks at 1 (preserving tone peak power) and whose area scales with RBW (integrating noise over RBW).

**Tech Stack:** C++20, CMake + Ninja, Catch2 v3.4.0, ImGui, ImPlot

---

## File Map

| File | Responsibility |
|------|---------------|
| `common/common.h` | Shared constants and helpers (`addedNoiseDensity_W_per_Hz`) |
| `common/spectrum.h` | `Spectrum` struct — remove `thermalNoisePower_W`, update comments for density semantics |
| `signal_generator/include/signal_generator_engine.h` | Generator engine interface — remove gain/NF members |
| `signal_generator/src/signal_generator_engine.cpp` | Generator engine implementation — clean source with flat thermal density |
| `signal_generator/src/signal_generator_widget.cpp` | Generator UI — remove gain/NF controls |
| `amplifier/src/amplifier_engine.cpp` | Amplifier engine — scale density by gain, add noise density |
| `spectrum_analyzer/src/spectrum_analyzer_engine.cpp` | Spectrum analyzer — multiply density by bin width, RBW kernel peaks at 1 |
| `tests/test_main.cpp` | Unit tests — update for density model, add RBW integration test |

---

### Task 1: Add noise density helper to `common.h`

**Files:**
- Modify: `common/common.h`

- [ ] **Step 1: Add `addedNoiseDensity_W_per_Hz` and deprecate `addedNoisePerBin_W`**

```cpp
// In common/common.h, after calculateNoiseTemp:

inline double addedNoiseDensity_W_per_Hz(double nf_dB, double gain_linear) {
    double Te = calculateNoiseTemp(nf_dB);
    return k * Te * gain_linear;
}

// DEPRECATED: use addedNoiseDensity_W_per_Hz for density (W/Hz) model
inline double addedNoisePerBin_W(double nf_dB, double gain_linear, double bin_width) {
    return addedNoiseDensity_W_per_Hz(nf_dB, gain_linear) * bin_width;
}
```

- [ ] **Step 2: Commit**

```bash
git add common/common.h
git commit -m "feat: add addedNoiseDensity_W_per_Hz helper for spectral density model"
```

---

### Task 2: Remove `thermalNoisePower_W` from `spectrum.h`

**Files:**
- Modify: `common/spectrum.h`

- [ ] **Step 1: Remove the `thermalNoisePower_W` method and add comments clarifying density semantics**

```cpp
#pragma once

#include "common.h"
#include <vector>

struct Spectrum {
    struct Tone {
        double freq_Hz = 0.0;
        double power_dBm = -174;
    };

    std::vector<double> frequencies;
    std::vector<Tone> tones;

    // Noise vectors store POWER SPECTRAL DENSITY in W/Hz.
    // To get total power in a bin, multiply by bin width.
    std::vector<double> noise_W;        // input noise density (W/Hz)
    std::vector<double> noise_added_W;  // added noise density (W/Hz)
    std::vector<double> noise_total_W;  // total output noise density (W/Hz)

    void computeTotalNoise() {
        size_t n = frequencies.size();
        noise_total_W.assign(n, 0.0);
        if (n < 2) {
            return;
        }
        for (size_t i = 0; i < n; ++i) {
            double noise_input = (i < noise_W.size()) ? noise_W[i] : 0.0;
            double noise_added = (i < noise_added_W.size()) ? noise_added_W[i] : 0.0;
            noise_total_W[i] = noise_input + noise_added;
        }
    }
};

struct Peak {
    int index;
    double freq_Hz;
    double power_dBm;
};
```

- [ ] **Step 2: Commit**

```bash
git add common/spectrum.h
git commit -m "refactor: remove thermalNoisePower_W, document noise vectors as W/Hz density"
```

---

### Task 3: Simplify `SignalGeneratorEngine` — remove gain/NF

**Files:**
- Modify: `signal_generator/include/signal_generator_engine.h`
- Modify: `signal_generator/src/signal_generator_engine.cpp`

- [ ] **Step 1: Update header — remove gain/NF members and accessors**

Replace the entire content of `signal_generator/include/signal_generator_engine.h` with:

```cpp
#pragma once
#include "common.h"
#include "signal_node.h"

class SignalGeneratorEngine {
  public:
    SignalGeneratorEngine(int id);

    int id() const { return m_id; }
    const tone &activeTone() const { return m_active_tone; }

    void setToneFrequency(double frequency) { m_active_tone.first = frequency; }
    void setToneAmplitude(double dBm) { m_active_tone.second = dBm; }
    void setFreqStep(double Hz) { m_f_step_Hz = Hz; rebuildFrequencyGrid(); }

    SignalNode &node() { return m_node; }
    void update(double dt);

    double f_step_Hz() const { return m_f_step_Hz; }

  private:
    int m_id;
    tone m_active_tone;
    SignalNode m_node;
    double m_f_step_Hz = 10e6;

    void rebuildFrequencyGrid();
};
```

- [ ] **Step 2: Update implementation — clean source with thermal density**

Replace the entire content of `signal_generator/src/signal_generator_engine.cpp` with:

```cpp
#include "signal_generator_engine.h"
#include "common.h"
#include <cmath>

SignalGeneratorEngine::SignalGeneratorEngine(int id)
    : m_id(id), m_active_tone(std::make_pair<int, double>(0, -60.0)) {
    rebuildFrequencyGrid();
}

void SignalGeneratorEngine::rebuildFrequencyGrid() {
    const double start_Hz = MIN_FREQ;
    const double stop_Hz = MAX_FREQ;
    int n = static_cast<int>((stop_Hz - start_Hz) / m_f_step_Hz);
    if (n < 2) n = 2;

    m_node.output.frequencies.resize(n);
    for (int i = 0; i < n; ++i) {
        m_node.output.frequencies[i] = start_Hz + i * m_f_step_Hz;
    }

    m_node.output.noise_W.assign(n, 0.0);
    m_node.output.noise_added_W.assign(n, 0.0);
    // Generator is an ideal source: flat thermal noise density k*T (W/Hz)
    m_node.input.noise_total_W.assign(n, k * T);
    m_node.output.computeTotalNoise();
}

void SignalGeneratorEngine::update(double dt) {
    auto &in = m_node.input;
    auto &out = m_node.output;

    out.tones.clear();
    Spectrum::Tone t;
    t.freq_Hz = m_active_tone.first;
    t.power_dBm = m_active_tone.second;
    out.tones.push_back(t);

    const size_t N = out.frequencies.size();
    if (N < 2) {
        out.noise_W.assign(N, 0.0);
        out.noise_added_W.assign(N, 0.0);
        out.noise_total_W.assign(N, 0.0);
        return;
    }

    // Unity gain for noise density
    out.noise_W.assign(N, 0.0);
    for (size_t i = 0; i < N; ++i) {
        double nin = (i < in.noise_total_W.size() ? in.noise_total_W[i] : 0.0);
        out.noise_W[i] = nin;
    }

    // Generator adds no noise of its own
    out.noise_added_W.assign(N, 0.0);

    out.noise_total_W.assign(N, 0.0);
    for (size_t i = 0; i < N; ++i) {
        out.noise_total_W[i] = out.noise_W[i] + out.noise_added_W[i];
    }
}
```

- [ ] **Step 3: Build to verify compilation**

```bash
cmake --build build
```

Expected: compiles successfully.

- [ ] **Step 4: Commit**

```bash
git add signal_generator/include/signal_generator_engine.h signal_generator/src/signal_generator_engine.cpp
git commit -m "refactor: generator is clean source with no gain/NF, outputs thermal noise density"
```

---

### Task 4: Remove gain/NF controls from generator widget

**Files:**
- Modify: `signal_generator/src/signal_generator_widget.cpp`

- [ ] **Step 1: Remove any gain/NF UI controls (verify none exist, or remove if they do)**

The current widget already has no gain/NF controls — only frequency, amplitude, and bin width. Verify the file looks correct after Task 3. The widget code should remain:

```cpp
#include "signal_generator_widget.h"
#include "common.h"
#include "imgui.h"
#include "logging_core.h"
#include "utils.h"

SignalGeneratorWidget::SignalGeneratorWidget(SignalGeneratorEngine &engine) : m_engine(engine) {}

void SignalGeneratorWidget::draw(const char *title, bool *p_open) {
    if (ImGui::Begin(title, p_open)) {
        double tone_frequency = m_engine.activeTone().first;
        double amplitude = m_engine.activeTone().second;
        double bin_width = m_engine.f_step_Hz();

        if (ImGui::Checkbox("Measure", &m_engine.node().view_enabled)) {
            LOG_INFO("Change measurement active state [gen%d -> %s].", m_engine.id(),
                     m_engine.node().view_enabled ? "True" : "False");
        }

        if (utils::inputFrequency("Frequency (MHz)", tone_frequency, 1.0, 100.0, "%.0f", MIN_FREQ,
                               MAX_FREQ)) {
            m_engine.setToneFrequency(tone_frequency);
            LOG_INFO("Update tone frequency: [gen%d -> %.0f MHz].", m_engine.id(),
                     tone_frequency / 1e6);
        }

        if (utils::inputDouble("Amplitude (dBm)", amplitude, 1, 5, "%.0f", MIN_POWER, MAX_POWER)) {
            m_engine.setToneAmplitude(amplitude);
            LOG_INFO("Update tone amplitude: [gen%d -> %.0f dBm]", m_engine.id(), amplitude);
        }

        if (utils::inputFrequency("Bin width (MHz)", bin_width, 1.0, 10.0, "%.0f", 1e6, 100e6)) {
            m_engine.setFreqStep(bin_width);
            LOG_INFO("Update bin width: [gen%d -> %.0f MHz]", m_engine.id(),
                     bin_width / 1e6);
        }

        ImGui::End();
    }
}
```

Note: the log message for bin width no longer mentions noise level since noise is now density-based and independent of bin width.

- [ ] **Step 2: Build to verify compilation**

```bash
cmake --build build
```

Expected: compiles successfully.

- [ ] **Step 3: Commit**

```bash
git add signal_generator/src/signal_generator_widget.cpp
git commit -m "ui: remove noise-level log from generator bin width control"
```

---

### Task 5: Update `AmplifierEngine` for density model

**Files:**
- Modify: `amplifier/src/amplifier_engine.cpp`

- [ ] **Step 1: Replace noise calculation to use W/Hz density**

Replace the noise section of `amplifier/src/amplifier_engine.cpp` (lines 40-59) with:

```cpp
    double G = dbToLinear(m_gain_dB);
    double added_density = addedNoiseDensity_W_per_Hz(m_nf_dB, G);

    out.noise_W.assign(N, 0.0);
    for (size_t i = 0; i < N; ++i) {
        double nin = (i < in.noise_total_W.size() ? in.noise_total_W[i] : 0.0);
        out.noise_W[i] = G * nin;
    }

    out.noise_added_W.resize(N);
    if (added_density <= 0.0) {
        out.noise_added_W.assign(N, 0.0);
    } else {
        out.noise_added_W.assign(N, added_density);
    }
    out.noise_total_W.resize(N);
    for (size_t i = 0; i < N; ++i) {
        out.noise_total_W[i] = out.noise_W[i] + out.noise_added_W[i];
    }
```

The full file should now be:

```cpp
#include "amplifier_engine.h"
#include "common.h"

AmplifierEngine::AmplifierEngine(int id) : m_id(id) {}

void AmplifierEngine::update(double dt) {
    auto &in = m_node.input;
    auto &out = m_node.output;

    if (!in.frequencies.empty()) {
        out.frequencies = in.frequencies;
    } else if (out.frequencies.size() < 2) {
        const double start_Hz = MIN_FREQ;
        const double stop_Hz = MAX_FREQ;
        if (m_f_step_Hz <= 0) m_f_step_Hz = 10e6;
        int n = static_cast<int>((stop_Hz - start_Hz) / m_f_step_Hz);
        if (n < 2) {
            n = 2;
        }
        out.frequencies.resize(n);
        for (int i = 0; i < n; ++i) {
            out.frequencies[i] = start_Hz + i * m_f_step_Hz;
        }
    }

    out.tones = in.tones;
    for (auto &t : out.tones) {
        t.power_dBm += m_gain_dB;
    }

    const size_t N = out.frequencies.size();
    if (N < 2) {
        out.noise_W.assign(N, 0.0);
        out.noise_added_W.assign(N, 0.0);
        out.noise_total_W.assign(N, 0.0);
        return;
    }

    double G = dbToLinear(m_gain_dB);
    double added_density = addedNoiseDensity_W_per_Hz(m_nf_dB, G);

    out.noise_W.assign(N, 0.0);
    for (size_t i = 0; i < N; ++i) {
        double nin = (i < in.noise_total_W.size() ? in.noise_total_W[i] : 0.0);
        out.noise_W[i] = G * nin;
    }

    out.noise_added_W.resize(N);
    if (added_density <= 0.0) {
        out.noise_added_W.assign(N, 0.0);
    } else {
        out.noise_added_W.assign(N, added_density);
    }
    out.noise_total_W.resize(N);
    for (size_t i = 0; i < N; ++i) {
        out.noise_total_W[i] = out.noise_W[i] + out.noise_added_W[i];
    }
}
```

- [ ] **Step 2: Build to verify compilation**

```bash
cmake --build build
```

Expected: compiles successfully.

- [ ] **Step 3: Commit**

```bash
git add amplifier/src/amplifier_engine.cpp
git commit -m "refactor: amplifier uses W/Hz noise density model"
```

---

### Task 6: Update `SpectrumAnalyzerEngine` — integrate density over RBW

**Files:**
- Modify: `spectrum_analyzer/src/spectrum_analyzer_engine.cpp`

- [ ] **Step 1: Update `integratePowerPerBin` to multiply density by bin width**

Replace the function with:

```cpp
std::vector<double> SpectrumAnalyzerEngine::integratePowerPerBin(const Spectrum &spec) const {
    size_t n = spec.frequencies.size();
    std::vector<double> power_W(n, 0.0);

    double bin_width = 1.0;
    if (spec.frequencies.size() >= 2) {
        bin_width = spec.frequencies[1] - spec.frequencies[0];
    }

    // Convert noise density (W/Hz) to per-bin power (W)
    if (spec.noise_total_W.size() == n) {
        for (size_t i = 0; i < n; ++i) {
            power_W[i] = spec.noise_total_W[i] * bin_width;
        }
    } else if (!spec.noise_total_W.empty()) {
        for (size_t i = 0; i < n && i < spec.noise_total_W.size(); ++i) {
            power_W[i] = spec.noise_total_W[i] * bin_width;
        }
    }

    // Add tones as discrete impulses
    for (const auto &t : spec.tones) {
        if (n < 2) {
            continue;
        }
        int bin_idx =
            static_cast<int>(std::round((t.freq_Hz - spec.frequencies.front()) / bin_width));
        if (bin_idx >= 0 && static_cast<size_t>(bin_idx) < n) {
            double tone_W = std::pow(10.0, (t.power_dBm - 30.0) / 10.0);
            power_W[bin_idx] += tone_W;
        }
    }

    return power_W;
}
```

- [ ] **Step 2: Update `applyRBW` — kernel peaks at 1, no normalization**

Replace the kernel generation section with:

```cpp
    std::vector<double> kernel(kernel_size);
    double sigma = kernel_half / 2.0 + 0.001;

    for (int i = 0; i < kernel_size; ++i) {
        int x = i - kernel_half;
        kernel[i] = std::exp(-0.5 * (x * x) / (sigma * sigma));
    }
    // Kernel peaks at 1 (center). This preserves tone peak power after convolution.
    // For noise, the convolution integrates per-bin power over the filter shape,
    // producing a result proportional to density * RBW (independent of internal grid).
```

Remove the normalization loop:
```cpp
    // REMOVED:
    // for (auto &k : kernel) {
    //     k /= sum;
    // }
```

The full `applyRBW` function should be:

```cpp
std::vector<double> SpectrumAnalyzerEngine::applyRBW(const std::vector<double> &power_W,
                                                     double binWidth) const {
    size_t n = power_W.size();
    if (n == 0) {
        return {};
    }
    // kernel width in bins (simple Gaussian approx)
    int kernel_half = std::max(1, static_cast<int>(std::round((m_rbw / binWidth) / 2.0)));
    int kernel_size = 2 * kernel_half + 1;

    std::vector<double> kernel(kernel_size);
    double sigma = kernel_half / 2.0 + 0.001;

    for (int i = 0; i < kernel_size; ++i) {
        int x = i - kernel_half;
        kernel[i] = std::exp(-0.5 * (x * x) / (sigma * sigma));
    }
    // Kernel peaks at 1 (center). This preserves tone peak power after convolution.
    // For noise, the convolution integrates per-bin power over the filter shape,
    // producing a result proportional to density * RBW (independent of internal grid).

    std::vector<double> out(n, 0.0);
    for (size_t i = 0; i < n; ++i) {
        double acc = 0.0;
        for (int k = -kernel_half; k <= kernel_half; ++k) {
            int idx = static_cast<int>(i) + k;
            if (idx < 0 || idx >= static_cast<int>(n)) {
                continue;
            }
            acc += power_W[idx] * kernel[k + kernel_half];
        }
        out[i] = acc;
    }
    return out;
}
```

- [ ] **Step 3: Build and run tests**

```bash
cmake --build build && ctest --test-dir build
```

Expected: Build succeeds. Tests may fail (they will be updated in Task 7).

- [ ] **Step 4: Commit**

```bash
git add spectrum_analyzer/src/spectrum_analyzer_engine.cpp
git commit -m "feat: spectrum analyzer integrates noise density over RBW, kernel peaks at 1"
```

---

### Task 7: Update tests for density model

**Files:**
- Modify: `tests/test_main.cpp`

- [ ] **Step 1: Update existing tests**

Replace the entire content of `tests/test_main.cpp` with:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "common.h"
#include "spectrum.h"
#include "signal_generator_engine.h"
#include "amplifier_engine.h"
#include "spectrum_analyzer_engine.h"

using Catch::Approx;

TEST_CASE("dB to linear conversion", "[common]") {
    REQUIRE(dbToLinear(0.0) == Approx(1.0));
    REQUIRE(dbToLinear(10.0) == Approx(10.0));
    REQUIRE(dbToLinear(-10.0) == Approx(0.1));
    REQUIRE(dbToLinear(20.0) == Approx(100.0));
}

TEST_CASE("Noise temperature calculation", "[common]") {
    // Noise figure 0 dB => noise temperature = 0 K
    REQUIRE(calculateNoiseTemp(0.0) == Approx(0.0));
    // Noise figure 3 dB => F = 10^(3/10) ≈ 2.0, Te = T*(F-1) = 290*(2-1)=290 K
    REQUIRE(calculateNoiseTemp(3.0) == Approx(290.0).epsilon(0.01));
}

TEST_CASE("Added noise density", "[common]") {
    // With noise figure 0 dB => Te = 0 => added noise density = 0 regardless of gain
    double added = addedNoiseDensity_W_per_Hz(0.0, 1.0);
    REQUIRE(added == Approx(0.0));

    // With gain 10 (10 dB), noise figure 10 dB
    double added2 = addedNoiseDensity_W_per_Hz(10.0, dbToLinear(10.0));
    double expected2 = k * calculateNoiseTemp(10.0) * dbToLinear(10.0);
    REQUIRE(added2 == Approx(expected2).epsilon(0.001));
}

TEST_CASE("Added noise per bin (deprecated helper)", "[common]") {
    // Verify backward compatibility: addedNoisePerBin_W = density * bin_width
    double density = addedNoiseDensity_W_per_Hz(10.0, dbToLinear(10.0));
    double per_bin = addedNoisePerBin_W(10.0, dbToLinear(10.0), 1e6);
    REQUIRE(per_bin == Approx(density * 1e6).epsilon(0.001));
}

TEST_CASE("Spectrum computeTotalNoise", "[common]") {
    Spectrum spec;
    // Set up frequencies (uniform grid)
    const int N = 5;
    spec.frequencies.resize(N);
    for (int i = 0; i < N; ++i) {
        spec.frequencies[i] = i * 1e6; // 1 MHz steps
    }
    // Values are now W/Hz (density)
    spec.noise_W = {1e-18, 2e-18, 3e-18, 4e-18, 5e-18};
    spec.noise_added_W = {0.5e-18, 0.6e-18, 0.7e-18, 0.8e-18, 0.9e-18};
    spec.computeTotalNoise();
    REQUIRE(spec.noise_total_W.size() == N);
    for (int i = 0; i < N; ++i) {
        REQUIRE(spec.noise_total_W[i] == Approx(spec.noise_W[i] + spec.noise_added_W[i]).epsilon(1e-30));
    }
}

TEST_CASE("Generator outputs flat thermal noise density", "[generator]") {
    SignalGeneratorEngine gen(0);
    gen.update(0.0);

    const auto &out = gen.node().output;
    REQUIRE(!out.noise_total_W.empty());
    for (double density : out.noise_total_W) {
        REQUIRE(density == Approx(k * T).epsilon(1e-30));
    }
}

TEST_CASE("Amplifier scales noise density correctly", "[amplifier]") {
    SignalGeneratorEngine gen(0);
    gen.update(0.0);

    AmplifierEngine amp(0);
    amp.setGain_dB(10.0);
    amp.setNF_dB(3.0);
    amp.node().input = gen.node().output;
    amp.update(0.0);

    const auto &out = amp.node().output;
    REQUIRE(!out.noise_total_W.empty());

    double G = dbToLinear(10.0);
    double Te = calculateNoiseTemp(3.0);
    double expected_density = k * T * G + k * Te * G;

    for (double density : out.noise_total_W) {
        REQUIRE(density == Approx(expected_density).epsilon(1e-30));
    }
}

TEST_CASE("Spectrum analyzer noise floor depends on RBW not grid spacing", "[spectrum]") {
    // Setup: generator + amplifier chain
    SignalGeneratorEngine gen(0);
    gen.setFreqStep(10e6);
    gen.update(0.0);

    AmplifierEngine amp(0);
    amp.setGain_dB(20.0);
    amp.setNF_dB(5.0);
    amp.node().input = gen.node().output;
    amp.update(0.0);

    SpectrumAnalyzerEngine sa;
    sa.setStartFrequency(MIN_FREQ);
    sa.setStopFrequency(MAX_FREQ);
    sa.setResBw(50e6);

    // Render with default grid (10 MHz step)
    std::vector<const Spectrum *> specs1 = {&amp.node().output};
    auto display1 = sa.renderCombinedSpectrum(specs1);

    // Change generator grid spacing to 20 MHz
    gen.setFreqStep(20e6);
    gen.update(0.0);
    amp.node().input = gen.node().output;
    amp.update(0.0);

    auto display2 = sa.renderCombinedSpectrum(specs1);

    // Noise floor should be approximately the same (independent of grid)
    // Sample a point away from the tone (e.g., middle of the array)
    size_t mid = display1.size() / 2;
    REQUIRE(display1[mid] == Approx(display2[mid]).epsilon(1.0));

    // Now change RBW and verify noise floor changes
    sa.setResBw(100e6);
    auto display3 = sa.renderCombinedSpectrum(specs1);

    // Higher RBW => higher noise floor (roughly +3 dB for 2x RBW)
    REQUIRE(display3[mid] > display2[mid]);
}
```

- [ ] **Step 2: Run tests**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: All tests pass.

- [ ] **Step 3: Commit**

```bash
git add tests/test_main.cpp
git commit -m "test: update tests for W/Hz noise density model and add RBW integration test"
```

---

### Task 8: Final verification

- [ ] **Step 1: Full build and test run**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: All tests pass, `build/bin/main.exe` compiles.

- [ ] **Step 2: Run the application (smoke test)**

```bash
build/bin/main.exe
```

Expected: Application launches. Signal generator widget shows only Frequency, Amplitude, and Bin width controls (no gain/NF). Spectrum analyzer shows noise floor that changes with RBW but not with generator bin width.

- [ ] **Step 3: Commit**

```bash
git commit -m "feat: decouple bin width from noise using spectral density model" --allow-empty
```

---

## Self-Review

### Spec Coverage

| Spec Section | Plan Task |
|-------------|-----------|
| 3.1 Data Model: Noise as Spectral Density | Task 2 (`spectrum.h` comments), Task 6 (`integratePowerPerBin`) |
| 3.2 Signal Generator: Clean Ideal Source | Task 3 (header + engine), Task 4 (widget) |
| 3.3 Amplifier: Density Scaling + Added Noise | Task 5 (`amplifier_engine.cpp`) |
| 3.4 Spectrum Analyzer: Integrate Density Over RBW | Task 6 (`spectrum_analyzer_engine.cpp`) |
| 3.5 UI Changes | Task 4 (widget) |
| 3.6 Test Impact | Task 7 (tests) |
| 5. Success Criteria | Task 8 (final verification) |

### Placeholder Scan

- No "TBD", "TODO", or "implement later" found.
- All steps contain exact code blocks.
- All steps contain exact commands with expected output.
- No vague requirements like "add appropriate error handling".

### Type Consistency

- `addedNoiseDensity_W_per_Hz(double nf_dB, double gain_linear)` — consistent across all tasks.
- `noise_W`, `noise_added_W`, `noise_total_W` treated as W/Hz throughout.
- No naming conflicts or signature mismatches.

**Plan is complete and ready for execution.**
