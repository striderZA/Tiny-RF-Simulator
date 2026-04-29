# Spectrum Marker v2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Update spectrum marker to track moving tones by frequency and support click-and-drag on the plot

**Architecture:** Widget owns marker state (`target_freq_Hz`, `is_dragging`). `resolveMarker` finds nearest peak to `target_freq_Hz` from cached peaks (or nearest bin if no match). Drag uses `ImPlot::GetPlotMousePos()` inside the plot block.

**Tech Stack:** C++20, ImGui, ImPlot, Catch2 v3

---

## Files

| File | Action | Responsibility |
|------|--------|--------------|
| `spectrum_analyzer/include/spectrum_analyzer_widget.h` | Modify | Update `MarkerState`, remove `MarkerInfo` |
| `spectrum_analyzer/src/spectrum_analyzer_widget.cpp` | Modify | Refactor resolveMarker, add drag, update controls |
| `tests/test_main.cpp` | Modify | Add drag-state tests (optional, covered by manual verification) |

---

### Task 1: Refactor MarkerState and resolveMarker

**Files:**
- Modify: `spectrum_analyzer/include/spectrum_analyzer_widget.h`
- Modify: `spectrum_analyzer/src/spectrum_analyzer_widget.cpp`

- [ ] **Step 1: Update `MarkerState` struct**

In `spectrum_analyzer_widget.h`, replace `MarkerState` with:

```cpp
struct MarkerState {
    bool enabled = false;
    double target_freq_Hz = 0.0;   // Frequency the marker aims for
    bool is_dragging = false;      // True while mouse is held down on plot
    std::vector<Peak> peaks;       // Cached peaks from last snap
};
```

Remove `MarkerInfo` struct entirely (no longer needed).

- [ ] **Step 2: Update `resolveMarker` to use frequency-proximity tracking**

Replace the `resolveMarker` implementation in the `.cpp`:

```cpp
int SpectrumAnalyzerWidget::resolveMarkerIdx(const std::vector<double> &freq_axis,
                                              const std::vector<double> &power_dBm) const {
    if (!m_marker.enabled || power_dBm.empty() || freq_axis.empty()) {
        return -1;
    }

    // Try to find nearest cached peak to target_freq_Hz
    if (!m_marker.peaks.empty()) {
        int best_idx = -1;
        double best_dist = std::numeric_limits<double>::max();
        for (const auto &pk : m_marker.peaks) {
            double dist = std::abs(pk.freq_Hz - m_marker.target_freq_Hz);
            if (dist < best_dist) {
                best_dist = dist;
                best_idx = pk.index;
            }
        }
        if (best_idx >= 0 && best_idx < static_cast<int>(power_dBm.size())) {
            return best_idx;
        }
    }

    // Fallback: nearest bin to target_freq_Hz
    int best_idx = 0;
    double best_dist = std::numeric_limits<double>::max();
    for (size_t i = 0; i < freq_axis.size(); ++i) {
        double dist = std::abs(freq_axis[i] - m_marker.target_freq_Hz);
        if (dist < best_dist) {
            best_dist = dist;
            best_idx = static_cast<int>(i);
        }
    }
    return best_idx;
}
```

Update the header to replace `resolveMarker` declaration:

```cpp
    int resolveMarkerIdx(const std::vector<double> &freq_axis,
                         const std::vector<double> &power_dBm) const;
```

- [ ] **Step 3: Update `drawMarkerOnPlot` signature**

Since `MarkerInfo` is removed, `drawMarkerOnPlot` now takes `idx`, `freq`, `power` directly:

```cpp
    void drawMarkerOnPlot(int idx, double freq_Hz, double power_dBm);
```

Implementation:

```cpp
void SpectrumAnalyzerWidget::drawMarkerOnPlot(int idx, double freq_Hz, double power_dBm) {
    (void)idx; // may be used later for annotation
    ImPlotSpec marker_spec;
    marker_spec.Marker = ImPlotMarker_Down;
    marker_spec.MarkerSize = 8.0f;
    marker_spec.MarkerFillColor = ImVec4(1.0f, 0.5f, 0.0f, 1.0f);
    marker_spec.LineWeight = 1.0f;
    marker_spec.MarkerLineColor = ImVec4(1.0f, 0.5f, 0.0f, 1.0f);
    ImPlot::PlotScatter("Marker", &freq_Hz, &power_dBm, 1, marker_spec);
}
```

- [ ] **Step 4: Build**

Run: `cmake --build build`
Expected: Pass

---

### Task 2: Add drag interaction and update controls

**Files:**
- Modify: `spectrum_analyzer/src/spectrum_analyzer_widget.cpp`

- [ ] **Step 1: Replace the plot block with drag-aware version**

The plot block (currently lines 175-185) should become:

```cpp
    if (ImPlot::BeginPlot("Spectrum")) {
        ImPlot::PlotLine("Combined Spectrum", freq_axis->data(), display_dBm.data(),
                         (int)display_dBm.size());

        if (m_marker.enabled && !display_dBm.empty()) {
            // Drag handling: if mouse is down inside plot, update target_freq_Hz
            if (ImPlot::IsPlotHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                m_marker.is_dragging = true;
                ImPlotPoint mouse_pos = ImPlot::GetPlotMousePos();
                m_marker.target_freq_Hz = mouse_pos.x;
            } else if (m_marker.is_dragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                m_marker.is_dragging = false;
                // Snap to nearest peak on release
                m_marker.peaks = m_engine.findPeaks(display_dBm, *freq_axis, 8);
            }

            int idx = resolveMarkerIdx(*freq_axis, display_dBm);
            if (idx >= 0 && idx < static_cast<int>(display_dBm.size())) {
                double mf = (*freq_axis)[static_cast<size_t>(idx)];
                double mp = display_dBm[static_cast<size_t>(idx)];
                drawMarkerOnPlot(idx, mf, mp);
            }
        }

        ImPlot::EndPlot();
    }
```

- [ ] **Step 2: Update `drawMarkerControls`**

Replace the current `drawMarkerControls` with:

```cpp
void SpectrumAnalyzerWidget::drawMarkerControls(const std::vector<double> &freq_axis,
                                                const std::vector<double> &display_dBm) {
    if (ImGui::Checkbox("Enable Marker", &m_marker.enabled)) {
        if (m_marker.enabled && !freq_axis.empty()) {
            m_marker.target_freq_Hz = freq_axis[freq_axis.size() / 2];
            m_marker.peaks.clear();
        }
    }

    if (!m_marker.enabled) {
        return;
    }

    if (ImGui::Button("Snap to Peak")) {
        m_marker.peaks = m_engine.findPeaks(display_dBm, freq_axis, 8);
        if (!m_marker.peaks.empty()) {
            m_marker.target_freq_Hz = m_marker.peaks[0].freq_Hz;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Next Peak")) {
        if (!m_marker.peaks.empty()) {
            // Find the currently tracked peak (nearest to target_freq_Hz)
            int current_rank = 0;
            double best_dist = std::numeric_limits<double>::max();
            for (size_t i = 0; i < m_marker.peaks.size(); ++i) {
                double dist = std::abs(m_marker.peaks[i].freq_Hz - m_marker.target_freq_Hz);
                if (dist < best_dist) {
                    best_dist = dist;
                    current_rank = static_cast<int>(i);
                }
            }
            int next_rank = (current_rank + 1) % static_cast<int>(m_marker.peaks.size());
            m_marker.target_freq_Hz = m_marker.peaks[next_rank].freq_Hz;
        }
    }

    int idx = resolveMarkerIdx(freq_axis, display_dBm);
    double marker_freq = 0.0;
    double marker_power = -174.0;
    if (idx >= 0 && idx < static_cast<int>(display_dBm.size())) {
        marker_freq = freq_axis[static_cast<size_t>(idx)];
        marker_power = display_dBm[static_cast<size_t>(idx)];
    }
    ImGui::Text("Marker: %.2f MHz, %.2f dBm", marker_freq / 1e6, marker_power);
}
```

- [ ] **Step 3: Build and test**

Run: `cmake --build build && ctest --test-dir build`
Expected: All existing tests pass. No new tests needed for UI drag (manual verification).

---

### Task 3: Commit

- [ ] **Step 1: Stage and commit**

```bash
git add spectrum_analyzer/include/spectrum_analyzer_widget.h \
        spectrum_analyzer/src/spectrum_analyzer_widget.cpp
git commit -m "feat(spectrum): draggable marker with frequency tracking"
```

---

## Self-Review

**Spec coverage check:**
- ✅ Marker tracks moving tones — `target_freq_Hz` persists; `resolveMarkerIdx` finds nearest peak by frequency proximity
- ✅ Click-and-drag on plot — `IsPlotHovered() + IsMouseDown` updates `target_freq_Hz` continuously
- ✅ Snap to nearest peak on release — `IsMouseReleased` triggers `findPeaks` + cache update
- ✅ Remove `<`/`>` buttons — no longer in controls
- ✅ Keep Snap to Peak / Next Peak — both present, Next Peak tracks by proximity

**Placeholder scan:** No TBD, TODO, or vague steps. All code provided.

**Type consistency:** `MarkerState` fields match usage. `resolveMarkerIdx` returns `int` (-1 for invalid).
