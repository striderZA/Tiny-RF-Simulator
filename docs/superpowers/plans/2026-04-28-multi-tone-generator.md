# Multi-Tone Signal Generator Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Collapse 4 single-tone generators into 1 multi-tone generator with a shared thermal noise floor.

**Architecture:** Replace `tone m_active_tone` with `std::vector<Spectrum::Tone> m_tones` in `SignalGeneratorEngine`. Widget shows a scrollable tone table with add/delete. App holds one generator instance instead of a vector. Signal chain becomes `single gen → amp[0]`.

**Tech Stack:** C++20, CMake/Ninja, ImGui, Catch2 v3.4.0

---

### Task 1: Update engine header

**Files:**
- Modify: `signal_generator/include/signal_generator_engine.h`

- [ ] **Step 1: Replace single tone with vector in header**

Replace the entire class body:

```cpp
#pragma once
#include "common.h"
#include "signal_node.h"

class SignalGeneratorEngine {
  public:
    SignalGeneratorEngine(int id);

    int id() const { return m_id; }

    void addTone(double freq_Hz, double power_dBm);
    void removeTone(size_t index);
    void updateTone(size_t index, double freq_Hz, double power_dBm);
    const std::vector<Spectrum::Tone> &tones() const { return m_tones; }
    size_t toneCount() const { return m_tones.size(); }

    SignalNode &node() { return m_node; }
    void update(double dt);

  private:
    int m_id;
    std::vector<Spectrum::Tone> m_tones;
    SignalNode m_node;

    void rebuildFrequencyGrid();
};
```

- [ ] **Step 2: Commit**

```bash
git add signal_generator/include/signal_generator_engine.h
git commit -m "refactor: replace single tone with multi-tone vector in engine header"
```

---

### Task 2: Update engine implementation

**Files:**
- Modify: `signal_generator/src/signal_generator_engine.cpp`

- [ ] **Step 1: Rewrite constructor and add multi-tone API**

Full file content:

```cpp
#include "signal_generator_engine.h"
#include "common.h"
#include <cmath>

SignalGeneratorEngine::SignalGeneratorEngine(int id)
    : m_id(id) {
    rebuildFrequencyGrid();
}

void SignalGeneratorEngine::rebuildFrequencyGrid() {
    const double start_Hz = MIN_FREQ;
    const double stop_Hz = MAX_FREQ;
    constexpr double fixed_step = 10e6;
    int n = static_cast<int>((stop_Hz - start_Hz) / fixed_step);
    if (n < 2) n = 2;

    m_node.output.frequencies.resize(n);
    for (int i = 0; i < n; ++i) {
        m_node.output.frequencies[i] = start_Hz + i * fixed_step;
    }

    m_node.output.noise_W.assign(n, 0.0);
    m_node.output.noise_added_W.assign(n, 0.0);
    m_node.input.noise_total_W.assign(n, k * T);
    m_node.output.computeTotalNoise();
}

void SignalGeneratorEngine::addTone(double freq_Hz, double power_dBm) {
    m_tones.push_back({freq_Hz, power_dBm});
}

void SignalGeneratorEngine::removeTone(size_t index) {
    if (index < m_tones.size()) {
        m_tones.erase(m_tones.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

void SignalGeneratorEngine::updateTone(size_t index, double freq_Hz, double power_dBm) {
    if (index < m_tones.size()) {
        m_tones[index].freq_Hz = freq_Hz;
        m_tones[index].power_dBm = power_dBm;
    }
}

void SignalGeneratorEngine::update(double) {
    auto &in = m_node.input;
    auto &out = m_node.output;

    out.tones.clear();
    for (const auto &t : m_tones) {
        out.tones.push_back(t);
    }

    const size_t N = out.frequencies.size();
    if (N < 2) {
        out.noise_W.assign(N, 0.0);
        out.noise_added_W.assign(N, 0.0);
        out.noise_total_W.assign(N, 0.0);
        return;
    }

    out.noise_W.assign(N, 0.0);
    for (size_t i = 0; i < N; ++i) {
        double nin = (i < in.noise_total_W.size() ? in.noise_total_W[i] : 0.0);
        out.noise_W[i] = nin;
    }

    out.noise_added_W.assign(N, 0.0);

    out.noise_total_W.assign(N, 0.0);
    for (size_t i = 0; i < N; ++i) {
        out.noise_total_W[i] = out.noise_W[i] + out.noise_added_W[i];
    }
}
```

- [ ] **Step 2: Commit**

```bash
git add signal_generator/src/signal_generator_engine.cpp
git commit -m "refactor: implement multi-tone engine API and update()"
```

---

### Task 3: Write multi-tone engine tests

**Files:**
- Modify: `tests/test_main.cpp`

- [ ] **Step 1: Add multi-tone test section**

Add after the existing `Generator outputs flat thermal noise density` test:

```cpp
TEST_CASE("Generator with no tones produces empty tone list", "[generator]") {
    SignalGeneratorEngine gen(0);
    gen.update(0.0);
    REQUIRE(gen.node().output.tones.empty());
    REQUIRE(gen.toneCount() == 0);
}

TEST_CASE("Generator with multiple tones outputs all tones", "[generator]") {
    SignalGeneratorEngine gen(0);
    gen.addTone(100e6, -20.0);
    gen.addTone(200e6, -10.0);
    gen.addTone(50e6, 0.0);
    gen.update(0.0);

    REQUIRE(gen.toneCount() == 3);
    const auto &tones = gen.node().output.tones;
    REQUIRE(tones.size() == 3);
    REQUIRE(tones[0].freq_Hz == 100e6);
    REQUIRE(tones[0].power_dBm == -20.0);
    REQUIRE(tones[1].freq_Hz == 200e6);
    REQUIRE(tones[1].power_dBm == -10.0);
    REQUIRE(tones[2].freq_Hz == 50e6);
    REQUIRE(tones[2].power_dBm == 0.0);
}

TEST_CASE("Generator removeTone works correctly", "[generator]") {
    SignalGeneratorEngine gen(0);
    gen.addTone(100e6, -20.0);
    gen.addTone(200e6, -10.0);
    gen.removeTone(0);
    gen.update(0.0);

    REQUIRE(gen.toneCount() == 1);
    REQUIRE(gen.node().output.tones.size() == 1);
    REQUIRE(gen.node().output.tones[0].freq_Hz == 200e6);
}

TEST_CASE("Generator updateTone modifies existing tone", "[generator]") {
    SignalGeneratorEngine gen(0);
    gen.addTone(100e6, -20.0);
    gen.updateTone(0, 150e6, -5.0);
    gen.update(0.0);

    REQUIRE(gen.toneCount() == 1);
    REQUIRE(gen.node().output.tones[0].freq_Hz == 150e6);
    REQUIRE(gen.node().output.tones[0].power_dBm == -5.0);
}

TEST_CASE("Noise floor remains k*T regardless of tone count", "[generator]") {
    SignalGeneratorEngine gen(0);
    // No tones - just noise
    gen.update(0.0);
    for (double density : gen.node().output.noise_total_W) {
        REQUIRE(density == Catch::Approx(k * T).epsilon(1e-30));
    }

    // Add multiple tones - noise should still be k*T
    gen.addTone(100e6, -20.0);
    gen.addTone(200e6, -10.0);
    gen.addTone(300e6, 0.0);
    gen.update(0.0);
    for (double density : gen.node().output.noise_total_W) {
        REQUIRE(density == Catch::Approx(k * T).epsilon(1e-30));
    }
}
```

- [ ] **Step 2: Build and run tests to verify they pass**

```bash
cmake --build build && build/bin/tests.exe
```

Expected: All tests pass including the 4 new ones.

- [ ] **Step 3: Commit**

```bash
git add tests/test_main.cpp
git commit -m "test: add multi-tone generator tests"
```

---

### Task 4: Update generator widget with tone table

**Files:**
- Modify: `signal_generator/src/signal_generator_widget.cpp`

- [ ] **Step 1: Rewrite widget draw() with tone table**

Replace full file content:

```cpp
#include "signal_generator_widget.h"
#include "common.h"
#include "imgui.h"
#include "logging_core.h"
#include "utils.h"
#include <string>

SignalGeneratorWidget::SignalGeneratorWidget(SignalGeneratorEngine &engine) : m_engine(engine) {}

void SignalGeneratorWidget::draw(const char *title, bool *p_open) {
    if (ImGui::Begin(title, p_open)) {
        if (ImGui::Checkbox("Measure", &m_engine.node().view_enabled)) {
            LOG_INFO("Change measurement active state [gen%d -> %s].", m_engine.id(),
                     m_engine.node().view_enabled ? "True" : "False");
        }

        ImGui::SeparatorText("Tones");

        if (ImGui::BeginTable("tones", 4, ImGuiTableFlags_Borders)) {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 30.0f);
            ImGui::TableSetupColumn("Frequency (MHz)");
            ImGui::TableSetupColumn("Amplitude (dBm)");
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableHeadersRow();

            int to_delete = -1;

            for (int i = 0; i < static_cast<int>(m_engine.toneCount()); ++i) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%d", i + 1);

                ImGui::TableNextColumn();
                double freq = m_engine.tones()[static_cast<size_t>(i)].freq_Hz;
                ImGui::PushID(("freq" + std::to_string(i)).c_str());
                bool freq_changed = utils::inputFrequency("##freq", freq, 1.0, 100.0, "%.0f",
                                                          MIN_FREQ, MAX_FREQ);
                ImGui::PopID();

                ImGui::TableNextColumn();
                double amp = m_engine.tones()[static_cast<size_t>(i)].power_dBm;
                ImGui::PushID(("amp" + std::to_string(i)).c_str());
                bool amp_changed = utils::inputDouble("##amp", amp, 1, 5, "%.0f",
                                                      MIN_POWER, MAX_POWER);
                ImGui::PopID();

                if (freq_changed || amp_changed) {
                    m_engine.updateTone(static_cast<size_t>(i), freq, amp);
                    if (freq_changed) {
                        LOG_INFO("Update tone frequency: [gen%d tone%d -> %.0f MHz].",
                                 m_engine.id(), i, freq / 1e6);
                    }
                    if (amp_changed) {
                        LOG_INFO("Update tone amplitude: [gen%d tone%d -> %.0f dBm].",
                                 m_engine.id(), i, amp);
                    }
                }

                ImGui::TableNextColumn();
                ImGui::PushID(("del" + std::to_string(i)).c_str());
                if (ImGui::SmallButton("X")) {
                    to_delete = i;
                }
                ImGui::PopID();
            }

            ImGui::EndTable();

            if (to_delete >= 0) {
                m_engine.removeTone(static_cast<size_t>(to_delete));
                LOG_INFO("Remove tone: [gen%d tone%d].", m_engine.id(), to_delete);
            }
        }

        if (ImGui::Button("+ Add Tone")) {
            m_engine.addTone(100e6, -60.0);
            LOG_INFO("Add tone: [gen%d -> 100 MHz, -60 dBm].", m_engine.id());
        }

        ImGui::End();
    }
}
```

- [ ] **Step 2: Build and run to verify it compiles**

```bash
cmake --build build
```

Expected: No compile errors.

- [ ] **Step 3: Commit**

```bash
git add signal_generator/src/signal_generator_widget.cpp
git commit -m "feat: add tone table UI with add/delete"
```

---

### Task 5: Update app for single generator

**Files:**
- Modify: `app/include/app.h`
- Modify: `app/src/app.cpp`

- [ ] **Step 1: Update app.h — replace vectors with unique_ptr, remove InputSignals**

Replace `enum class InputSignals` and member variables:

```cpp
#pragma once

#include "amplifier_engine.h"
#include "amplifier_widget.h"
#include "logging_widget.h"
#include "signal_generator_engine.h"
#include "signal_generator_widget.h"
#include "spectrum_analyzer_engine.h"
#include "spectrum_analyzer_widget.h"
#include "view_manager.h"
#include <memory>
#include <vector>

class RfSimulatorApp {
  public:
    RfSimulatorApp();
    void draw_ui();
    void update_dsp();
    void addAmplifier();
    void removeAmplifier(size_t index);

    LoggingWidget m_log_widget;
    bool m_show_log = true;

  private:
    ViewManager m_view_manager;
    SpectrumAnalyzerEngine m_spectrum_engine;
    std::unique_ptr<SpectrumAnalyzerWidget> m_spectrum_widget;
    std::unique_ptr<SignalGeneratorEngine> m_generator;
    std::unique_ptr<SignalGeneratorWidget> m_generator_widget;
    std::vector<std::unique_ptr<AmplifierEngine>> m_amplifiers;
    std::vector<std::unique_ptr<AmplifierWidget>> m_amplifier_widgets;
    void draw_signal_chain(const char *title);
};
```

- [ ] **Step 2: Update app.cpp — replace multi-generator logic**

Full file content:

```cpp
#include "app.h"
#include "imgui.h"
#include "logging_widget.h"
#include <algorithm>

RfSimulatorApp::RfSimulatorApp() {
    m_generator = std::make_unique<SignalGeneratorEngine>(0);
    m_generator_widget = std::make_unique<SignalGeneratorWidget>(*m_generator);
    m_generator->addTone(100e6, -20.0);
    m_view_manager.registerNode(&m_generator->node());

    static const int defaultAmplifierCount = 1;
    for (int i = 0; i < defaultAmplifierCount; ++i) {
        addAmplifier();
    }
    m_spectrum_widget = std::make_unique<SpectrumAnalyzerWidget>(m_spectrum_engine, m_view_manager);
}

void RfSimulatorApp::update_dsp() {
    m_generator->update(0.0);

    if (!m_amplifiers.empty()) {
        m_amplifiers[0]->node().input = m_generator->node().output;
        m_amplifiers[0]->update(0.0);
    }
}

void RfSimulatorApp::addAmplifier() {
    int id = static_cast<int>(m_amplifiers.size());
    m_amplifiers.push_back(std::make_unique<AmplifierEngine>(id));
    m_amplifier_widgets.push_back(std::make_unique<AmplifierWidget>(*m_amplifiers.back()));
    m_view_manager.registerNode(&m_amplifiers.back()->node());
}

void RfSimulatorApp::removeAmplifier(size_t index) {
    if (index >= m_amplifiers.size()) return;
    m_view_manager.unregisterNode(&m_amplifiers[index]->node());
    m_amplifiers.erase(m_amplifiers.begin() + static_cast<std::ptrdiff_t>(index));
    m_amplifier_widgets.erase(m_amplifier_widgets.begin() + static_cast<std::ptrdiff_t>(index));
}

void RfSimulatorApp::draw_ui() {
    ImGuiIO &io = ImGui::GetIO();
    (void)io;
    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate,
                io.Framerate);

    draw_signal_chain("Signal Chain");

    m_spectrum_widget->draw("Spectrum Analyzer");

    m_generator_widget->draw("Generator 0##gen0");

    for (size_t i = 0; i < m_amplifier_widgets.size(); ++i) {
        char title_buffer[64];
        std::snprintf(title_buffer, sizeof(title_buffer), "Amplifier %d##amp%zu",
                      m_amplifiers[i]->id(), i);
        m_amplifier_widgets[i]->draw(title_buffer);
    }

    if (m_show_log)
        m_log_widget.draw("Log", &m_show_log);
}

void RfSimulatorApp::draw_signal_chain(const char *title) {
    if (ImGui::Begin(title)) {
        ImGui::Text("Generator: 1");
        ImGui::Separator();
        ImGui::Text("Amplifiers: %zu", m_amplifiers.size());
        ImGui::SameLine();
        if (ImGui::Button("Add Amplifier")) {
            addAmplifier();
        }
        for (size_t i = 0; i < m_amplifiers.size(); ++i) {
            ImGui::PushID(static_cast<int>(i + 1000));
            ImGui::Text("Amplifier %d", m_amplifiers[i]->id());
            ImGui::SameLine();
            if (ImGui::Button("Remove")) {
                removeAmplifier(i);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
    }
    ImGui::End();
}
```

- [ ] **Step 3: Build and verify**

```bash
cmake --build build
```

Expected: No compile errors.

- [ ] **Step 4: Run tests**

```bash
build/bin/tests.exe
```

Expected: All tests pass.

- [ ] **Step 5: Commit**

```bash
git add app/include/app.h app/src/app.cpp
git commit -m "refactor: collapse to single multi-tone generator in app layer"
```

---

### Task 6: Final verification

- [ ] **Step 1: Full clean build**

```bash
cmake -B build -G Ninja && cmake --build build
```

- [ ] **Step 2: Run all tests**

```bash
build/bin/tests.exe
```

Expected: 12 tests pass (8 original + 4 new multi-tone tests).

- [ ] **Step 3: Commit any remaining changes**

```bash
git add -A
git commit -m "chore: finalize multi-tone generator implementation"
```
