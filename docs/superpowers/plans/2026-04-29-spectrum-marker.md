# Spectrum Marker Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an interactive spectrum marker with peak detection to the spectrum analyzer widget

**Architecture:** Pure DSP peak detection lives in `SpectrumAnalyzerEngine`. Marker UI state and rendering live in `SpectrumAnalyzerWidget`. Follows existing engine/widget separation pattern.

**Tech Stack:** C++20, ImGui, ImPlot, Catch2 v3

---

## Files

| File | Action | Responsibility |
|------|--------|--------------|
| `spectrum_analyzer/include/spectrum_analyzer_engine.h` | Modify | Add `findPeaks` declaration |
| `spectrum_analyzer/src/spectrum_analyzer_engine.cpp` | Modify | Implement `findPeaks` |
| `spectrum_analyzer/include/spectrum_analyzer_widget.h` | Modify | Add `MarkerState` struct and member |
| `spectrum_analyzer/src/spectrum_analyzer_widget.cpp` | Modify | Add marker UI controls and plot rendering |
| `tests/test_main.cpp` | Modify | Add `findPeaks` unit tests |

---

### Task 1: Add `findPeaks` declaration to engine header

**Files:**
- Modify: `spectrum_analyzer/include/spectrum_analyzer_engine.h`

- [ ] **Step 1: Add `findPeaks` method declaration**

Insert after `computeAverageNoiseLevel` declaration (line ~32):

```cpp
    std::vector<Peak> findPeaks(const std::vector<double> &power_dBm,
                                const std::vector<double> &freq_axis,
                                int max_count = 8) const;
```

- [ ] **Step 2: Build to check compilation**

Run: `cmake --build build`
Expected: Pass (declaration only, no definition yet)

---

### Task 2: Implement `findPeaks` in engine

**Files:**
- Modify: `spectrum_analyzer/src/spectrum_analyzer_engine.cpp`

- [ ] **Step 1: Add `findPeaks` implementation**

Insert after `computeAverageNoiseLevel` implementation and before `applyRBW`:

```cpp
std::vector<Peak> SpectrumAnalyzerEngine::findPeaks(const std::vector<double> &power_dBm,
                                                    const std::vector<double> &freq_axis,
                                                    int max_count) const {
    std::vector<Peak> peaks;
    size_t n = power_dBm.size();
    if (n < 3 || freq_axis.size() != n) {
        return peaks;
    }

    for (size_t i = 1; i + 1 < n; ++i) {
        if (power_dBm[i] > power_dBm[i - 1] && power_dBm[i] > power_dBm[i + 1]) {
            peaks.push_back(Peak{static_cast<int>(i), freq_axis[i], power_dBm[i]});
        }
    }

    std::sort(peaks.begin(), peaks.end(),
              [](const Peak &a, const Peak &b) { return a.power_dBm > b.power_dBm; });

    if (static_cast<int>(peaks.size()) > max_count) {
        peaks.resize(max_count);
    }
    return peaks;
}
```

- [ ] **Step 2: Add `#include <algorithm>` to top of file if missing**

Check if `algorithm` is already included. The file currently includes `<cmath>` and `<random>`. Add `#include <algorithm>` after line 3 if not present.

- [ ] **Step 3: Build**

Run: `cmake --build build`
Expected: Pass

---

### Task 3: Add `MarkerState` to widget header

**Files:**
- Modify: `spectrum_analyzer/include/spectrum_analyzer_widget.h`

- [ ] **Step 1: Add `MarkerState` struct and member**

Replace the entire file content with:

```cpp
#pragma once
#include "spectrum_analyzer_engine.h"
#include "spectrum.h"
#include "view_manager.h"
#include <string>
#include <vector>

struct MarkerState {
    bool enabled = false;
    int selected_peak_idx = -1;   // -1 = manual, >=0 = snapped to peak
    int manual_bin = 0;
    std::vector<Peak> peaks;
};

class SpectrumAnalyzerWidget {
  public:
    SpectrumAnalyzerWidget(SpectrumAnalyzerEngine &engine, ViewManager &vm);

    void draw(const char *title, bool *p_open = nullptr);
    void setProbeLabel(const std::string& label) { m_probe_label = label; }

  private:
    SpectrumAnalyzerEngine &m_engine;
    ViewManager &m_view_manager;
    std::string m_probe_label;
    MarkerState m_marker;
};
```

- [ ] **Step 2: Build**

Run: `cmake --build build`
Expected: Pass

---

### Task 4: Add marker UI and rendering to widget

**Files:**
- Modify: `spectrum_analyzer/src/spectrum_analyzer_widget.cpp`

- [ ] **Step 1: Add marker UI controls after average noise text**

Replace lines 101-104 (the end of `draw()`):

```cpp
    double avg_noise = m_engine.computeAverageNoiseLevel(specs);
    ImGui::Text("Average noise level: %.2f dBm", avg_noise);

    // --- Marker controls ---
    if (ImGui::Checkbox("Enable Marker", &m_marker.enabled)) {
        if (m_marker.enabled) {
            m_marker.manual_bin = static_cast<int>(display_dBm.size() / 2);
            m_marker.selected_peak_idx = -1;
        }
    }

    if (m_marker.enabled) {
        if (ImGui::Button("Snap to Peak")) {
            m_marker.peaks = m_engine.findPeaks(display_dBm, *freq_axis, 8);
            m_marker.selected_peak_idx = m_marker.peaks.empty() ? -1 : 0;
        }
        ImGui::SameLine();
        if (ImGui::Button("Next Peak")) {
            if (!m_marker.peaks.empty()) {
                m_marker.selected_peak_idx =
                    (m_marker.selected_peak_idx + 1) % static_cast<int>(m_marker.peaks.size());
            }
        }

        if (ImGui::Button("<")) {
            if (m_marker.manual_bin > 0) {
                --m_marker.manual_bin;
            }
            m_marker.selected_peak_idx = -1;
        }
        ImGui::SameLine();
        if (ImGui::Button(">")) {
            if (m_marker.manual_bin < static_cast<int>(display_dBm.size()) - 1) {
                ++m_marker.manual_bin;
            }
            m_marker.selected_peak_idx = -1;
        }

        double marker_freq = 0.0;
        double marker_power = -174.0;
        if (m_marker.selected_peak_idx >= 0 &&
            m_marker.selected_peak_idx < static_cast<int>(m_marker.peaks.size())) {
            const auto &pk = m_marker.peaks[m_marker.selected_peak_idx];
            marker_freq = pk.freq_Hz;
            marker_power = pk.power_dBm;
        } else if (!display_dBm.empty()) {
            int idx = std::clamp(m_marker.manual_bin, 0, static_cast<int>(display_dBm.size()) - 1);
            marker_freq = (*freq_axis)[static_cast<size_t>(idx)];
            marker_power = display_dBm[static_cast<size_t>(idx)];
        }

        ImGui::Text("Marker: %.2f MHz, %.2f dBm", marker_freq / 1e6, marker_power);

        // Render marker on plot
        if (!display_dBm.empty()) {
            int idx = 0;
            if (m_marker.selected_peak_idx >= 0 &&
                m_marker.selected_peak_idx < static_cast<int>(m_marker.peaks.size())) {
                idx = m_marker.peaks[m_marker.selected_peak_idx].index;
            } else {
                idx = std::clamp(m_marker.manual_bin, 0, static_cast<int>(display_dBm.size()) - 1);
            }

            double mf = (*freq_axis)[static_cast<size_t>(idx)];
            double mp = display_dBm[static_cast<size_t>(idx)];

            ImPlot::SetNextMarkerStyle(ImPlotMarker_Down, 8.0f,
                                       ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
                                       1.0f, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
            ImPlot::PlotScatter("Marker", &mf, &mp, 1);
        }
    }

    ImGui::End();
}
```

Note: The marker scatter call must be placed **inside** the `ImPlot::BeginPlot` / `EndPlot` block, before `EndPlot`. Move the marker rendering logic inside the plot block (around line 96).

The final plot block should look like:

```cpp
    if (ImPlot::BeginPlot("Spectrum")) {
        ImPlot::PlotLine("Combined Spectrum", freq_axis->data(), display_dBm.data(),
                         (int)display_dBm.size());

        // Render marker on plot
        if (m_marker.enabled && !display_dBm.empty()) {
            int idx = 0;
            if (m_marker.selected_peak_idx >= 0 &&
                m_marker.selected_peak_idx < static_cast<int>(m_marker.peaks.size())) {
                idx = m_marker.peaks[m_marker.selected_peak_idx].index;
            } else {
                idx = std::clamp(m_marker.manual_bin, 0, static_cast<int>(display_dBm.size()) - 1);
            }

            double mf = (*freq_axis)[static_cast<size_t>(idx)];
            double mp = display_dBm[static_cast<size_t>(idx)];

            ImPlot::SetNextMarkerStyle(ImPlotMarker_Down, 8.0f,
                                       ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
                                       1.0f, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
            ImPlot::PlotScatter("Marker", &mf, &mp, 1);
        }

        ImPlot::EndPlot();
    }
```

- [ ] **Step 2: Add `#include <algorithm>` if missing**

Check top of file; add `#include <algorithm>` if not present.

- [ ] **Step 3: Build**

Run: `cmake --build build`
Expected: Pass

---

### Task 5: Add unit tests for `findPeaks`

**Files:**
- Modify: `tests/test_main.cpp`

- [ ] **Step 1: Append tests at end of file**

```cpp
TEST_CASE("findPeaks detects single tone peak", "[spectrum]") {
    SpectrumAnalyzerEngine sa;
    std::vector<double> freq = {0, 1e6, 2e6, 3e6, 4e6};
    std::vector<double> power = {-80.0, -70.0, -60.0, -70.0, -80.0};

    auto peaks = sa.findPeaks(power, freq, 8);
    REQUIRE(peaks.size() == 1);
    REQUIRE(peaks[0].index == 2);
    REQUIRE(peaks[0].freq_Hz == 2e6);
    REQUIRE(peaks[0].power_dBm == -60.0);
}

TEST_CASE("findPeaks sorts multiple peaks by power", "[spectrum]") {
    SpectrumAnalyzerEngine sa;
    std::vector<double> freq = {0, 1e6, 2e6, 3e6, 4e6, 5e6, 6e6};
    // Peaks at index 1 (-50), 3 (-30), 5 (-40)
    std::vector<double> power = {-90.0, -50.0, -60.0, -30.0, -55.0, -40.0, -90.0};

    auto peaks = sa.findPeaks(power, freq, 8);
    REQUIRE(peaks.size() == 3);
    REQUIRE(peaks[0].power_dBm == -30.0);
    REQUIRE(peaks[1].power_dBm == -40.0);
    REQUIRE(peaks[2].power_dBm == -50.0);
}

TEST_CASE("findPeaks limits to max_count", "[spectrum]") {
    SpectrumAnalyzerEngine sa;
    std::vector<double> freq = {0, 1e6, 2e6, 3e6, 4e6, 5e6, 6e6};
    std::vector<double> power = {-90.0, -50.0, -60.0, -30.0, -55.0, -40.0, -90.0};

    auto peaks = sa.findPeaks(power, freq, 2);
    REQUIRE(peaks.size() == 2);
    REQUIRE(peaks[0].power_dBm == -30.0);
    REQUIRE(peaks[1].power_dBm == -40.0);
}

TEST_CASE("findPeaks returns empty for flat spectrum", "[spectrum]") {
    SpectrumAnalyzerEngine sa;
    std::vector<double> freq = {0, 1e6, 2e6, 3e6};
    std::vector<double> power = {-80.0, -80.0, -80.0, -80.0};

    auto peaks = sa.findPeaks(power, freq, 8);
    REQUIRE(peaks.empty());
}

TEST_CASE("findPeaks skips endpoints", "[spectrum]") {
    SpectrumAnalyzerEngine sa;
    std::vector<double> freq = {0, 1e6, 2e6};
    std::vector<double> power = {-30.0, -80.0, -30.0};

    auto peaks = sa.findPeaks(power, freq, 8);
    // Endpoints are not considered peaks even though they are higher than neighbour
    REQUIRE(peaks.empty());
}

TEST_CASE("findPeaks returns empty for too few points", "[spectrum]") {
    SpectrumAnalyzerEngine sa;
    std::vector<double> freq = {0, 1e6};
    std::vector<double> power = {-30.0, -80.0};

    auto peaks = sa.findPeaks(power, freq, 8);
    REQUIRE(peaks.empty());
}
```

- [ ] **Step 2: Run tests**

Run: `cmake --build build && ctest --test-dir build`
Expected: All 25 tests pass (19 existing + 6 new)

---

### Task 6: Commit

- [ ] **Step 1: Stage and commit**

```bash
git add spectrum_analyzer/include/spectrum_analyzer_engine.h \
        spectrum_analyzer/src/spectrum_analyzer_engine.cpp \
        spectrum_analyzer/include/spectrum_analyzer_widget.h \
        spectrum_analyzer/src/spectrum_analyzer_widget.cpp \
        tests/test_main.cpp
git commit -m "feat(spectrum): add interactive marker with peak detection"
```

---

## Self-Review

**Spec coverage check:**
- ✅ Enable/disable marker — `ImGui::Checkbox("Enable Marker", &m_marker.enabled)`
- ✅ Snap to peak button — `Snap to Peak` calls `findPeaks`, sets `selected_peak_idx = 0`
- ✅ Cycle between up to 8 peaks — `Next Peak` cycles modulo; `findPeaks` takes `max_count = 8`
- ✅ Text display of frequency and power — `ImGui::Text("Marker: %.2f MHz, %.2f dBm", ...)`
- ✅ Move left/right, snap to data — `<` / `>` buttons adjust `manual_bin`, set `selected_peak_idx = -1`
- ✅ Upside-down triangle on plot — `ImPlot::PlotScatter` with `ImPlotMarker_Down`
- ✅ Detected maxima (not tones) — `findPeaks` operates on `display_dBm` rendered trace

**Placeholder scan:** No TBD, TODO, or vague steps. All code provided.

**Type consistency:** `findPeaks` signature matches declaration. `MarkerState` fields used consistently.
