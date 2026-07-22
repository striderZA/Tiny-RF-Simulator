# Spectrum Analyzer Trace Modes — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add ClearWrite, MaxHold, MinHold, and VideoAverage trace modes to the spectrum analyzer engine with per-trace history buffers and widget UI controls.

**Architecture:** Extend `SpectrumAnalyzerEngine` with a `TraceMode` enum, per-trace history buffers (`unordered_map<const Spectrum*, vector<double>>`), and a new `applyTraceMode` method in the render pipeline after VBW. Widget adds a combo box, conditional slider, conditional reset button, and peak status line.

**Tech Stack:** C++20, ImGui, ImPlot, Catch2

## Global Constraints

- C++20 (GCC 11+ / MinGW-w64 on Windows, Clang 14+)
- CMake >= 3.20, Ninja
- Catch2 for tests (already linked via `simulator::spectrum_analyzer_engine`)
- Build: `cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc && cmake --build build`
- Test: `ctest --test-dir build --output-on-failure`
- Follow existing engine patterns (mutable state for caches, `const Spectrum*` keys)
- `renderCombinedSpectrum` is NOT modified — trace mode applies only to per-trace `renderSpectrum`
- No new intermediate processing classes (no TraceProcessor)

---

## File Structure

| File | Responsibility | Action |
|---|---|---|
| `spectrum_analyzer/include/spectrum_analyzer_engine.h` | Engine header: enum, API, state | Modify |
| `spectrum_analyzer/src/spectrum_analyzer_engine.cpp` | Engine impl: applyTraceMode, pruneHistory, resetTraceHistory, pipeline wiring | Modify |
| `spectrum_analyzer/src/spectrum_analyzer_widget.cpp` | Widget UI: combo, slider, button, status line | Modify |
| `tests/test_main.cpp` | Unit tests for all trace modes | Modify |

No new files.

---

### Task 1: TraceMode Enum, State, Setters/Getters + ClearWrite Baseline Test

**Files:**
- Modify: `spectrum_analyzer/include/spectrum_analyzer_engine.h`
- Modify: `tests/test_main.cpp`

**Interfaces:**
- Consumes: Nothing (first task)
- Produces: `TraceMode` enum, `setTraceMode()`, `traceMode()`, `setVideoAvgCount()`, `videoAvgCount()`, `m_trace_mode`, `m_video_avg_count` members. Default: `ClearWrite`, count `10`.

- [ ] **Step 1: Write the ClearWrite baseline test**

Add after the last `[spectrum]` test in `tests/test_main.cpp` (after line ~461):

```cpp
TEST_CASE("TraceMode default is ClearWrite", "[spectrum][trace]") {
    SpectrumAnalyzerEngine sa;
    REQUIRE(sa.traceMode() == TraceMode::ClearWrite);
    REQUIRE(sa.videoAvgCount() == 10);
}

TEST_CASE("ClearWrite returns frame unchanged (no history)", "[spectrum][trace]") {
    SpectrumAnalyzerEngine sa;
    sa.setNoiseJitterEnabled(false);
    sa.setResBw(1e6);
    sa.setVideoBw(1e6);

    Spectrum spec;
    spec.frequencies = {0, 1e6, 2e6, 3e6, 4e6};
    spec.noise_total_W = {1e-18, 1e-18, 1e-18, 1e-18, 1e-18};
    spec.generation = 1;

    auto out1 = sa.renderSpectrum(spec);
    spec.generation = 2;
    auto out2 = sa.renderSpectrum(spec);

    REQUIRE(out1.size() == out2.size());
    for (size_t i = 0; i < out1.size(); ++i) {
        REQUIRE(out1[i] == Approx(out2[i]).epsilon(1e-9));
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build && ctest --test-dir build --output-on-failure -R "TraceMode"`
Expected: FAIL — `TraceMode` not defined, `traceMode()` not a member.

- [ ] **Step 3: Add TraceMode enum and API to engine header**

In `spectrum_analyzer/include/spectrum_analyzer_engine.h`, add after line 5 (`#include <vector>`):

```cpp
#include <unordered_map>
```

Add the enum before the class (after line 6):

```cpp
enum class TraceMode { ClearWrite, MaxHold, MinHold, VideoAverage };
```

Add to the public section (after line 29, the `rbw()` getter):

```cpp
    void setTraceMode(TraceMode m) { m_trace_mode = m; }
    void setVideoAvgCount(int n) { m_video_avg_count = n; }

    TraceMode traceMode() const { return m_trace_mode; }
    int videoAvgCount() const { return m_video_avg_count; }
```

Add to the private section (after line 62, the noise members):

```cpp
    mutable TraceMode m_trace_mode = TraceMode::ClearWrite;
    mutable int m_video_avg_count = 10;
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build && ctest --test-dir build --output-on-failure -R "TraceMode\|ClearWrite"`
Expected: PASS (2 tests)

- [ ] **Step 5: Commit**

```bash
git add spectrum_analyzer/include/spectrum_analyzer_engine.h tests/test_main.cpp
git commit -m "feat(spectrum): add TraceMode enum, state, and ClearWrite baseline test"
```

---

### Task 2: MaxHold Mode

**Files:**
- Modify: `spectrum_analyzer/include/spectrum_analyzer_engine.h`
- Modify: `spectrum_analyzer/src/spectrum_analyzer_engine.cpp`
- Modify: `tests/test_main.cpp`

**Interfaces:**
- Consumes: `TraceMode` enum, `setTraceMode()`, `renderSpectrum()` from Task 1
- Produces: `m_max_hold` buffer, `applyTraceMode()` private method, `renderSpectrum()` pipeline wired through `applyTraceMode`

- [ ] **Step 1: Write failing MaxHold test**

Add after the ClearWrite tests in `tests/test_main.cpp`:

```cpp
TEST_CASE("MaxHold accumulates per-bin maximum", "[spectrum][trace]") {
    SpectrumAnalyzerEngine sa;
    sa.setNoiseJitterEnabled(false);
    sa.setResBw(1e6);
    sa.setVideoBw(1e6);
    sa.setTraceMode(TraceMode::MaxHold);

    Spectrum spec;
    spec.frequencies = {0, 1e6, 2e6, 3e6, 4e6};
    spec.tones = {{2e6, -10.0, 0.0}};  // tone at bin 2
    spec.generation = 1;

    auto out1 = sa.renderSpectrum(spec);

    // Second frame: tone moves to bin 4, bin 2 tone removed
    spec.tones = {{4e6, -10.0, 0.0}};
    spec.generation = 2;
    auto out2 = sa.renderSpectrum(spec);

    // MaxHold: bin 2 should still have the high value from frame 1
    // bin 4 should have the high value from frame 2
    REQUIRE(out2[2] > out2[0] + 10.0);  // bin 2 held high
    REQUIRE(out2[4] > out2[0] + 10.0);  // bin 4 now high
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build && ctest --test-dir build --output-on-failure -R "MaxHold"`
Expected: FAIL — tone at bin 2 not held (ClearWrite behavior)

- [ ] **Step 3: Add m_max_hold buffer and applyTraceMode to engine**

In `spectrum_analyzer/include/spectrum_analyzer_engine.h`, add to private section (after `m_video_avg_count`):

```cpp
    mutable std::unordered_map<const Spectrum*, std::vector<double>> m_max_hold;
```

Add private method declaration (after `integratePowerPerBin` on line 50):

```cpp
    std::vector<double> applyTraceMode(const Spectrum &spec,
                                        const std::vector<double> &after_vbw) const;
```

In `spectrum_analyzer/src/spectrum_analyzer_engine.cpp`, add the implementation (after `applyVBW` function, at end of file):

```cpp
std::vector<double> SpectrumAnalyzerEngine::applyTraceMode(
    const Spectrum &spec, const std::vector<double> &after_vbw) const
{
    if (m_trace_mode == TraceMode::ClearWrite) {
        return after_vbw;
    }

    std::unordered_map<const Spectrum*, std::vector<double>>* history = nullptr;
    if (m_trace_mode == TraceMode::MaxHold) history = &m_max_hold;
    // MinHold and VideoAverage will be added in subsequent tasks
    if (!history) return after_vbw;

    auto& h = (*history)[&spec];

    if (h.size() != after_vbw.size()) {
        h = after_vbw;
        return after_vbw;
    }

    if (m_trace_mode == TraceMode::MaxHold) {
        for (size_t i = 0; i < h.size(); ++i)
            h[i] = std::max(h[i], after_vbw[i]);
    }
    return h;
}
```

Wire `applyTraceMode` into `renderSpectrum`. Change the return at line 95 from:

```cpp
    return vbw_out;
```

to:

```cpp
    return this->applyTraceMode(spec, vbw_out);
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build && ctest --test-dir build --output-on-failure -R "MaxHold"`
Expected: PASS

- [ ] **Step 5: Run all spectrum tests to check for regressions**

Run: `ctest --test-dir build --output-on-failure -R "spectrum"`
Expected: All spectrum tests PASS

- [ ] **Step 6: Commit**

```bash
git add spectrum_analyzer/include/spectrum_analyzer_engine.h spectrum_analyzer/src/spectrum_analyzer_engine.cpp tests/test_main.cpp
git commit -m "feat(spectrum): add MaxHold trace mode with per-trace history"
```

---

### Task 3: MinHold Mode

**Files:**
- Modify: `spectrum_analyzer/include/spectrum_analyzer_engine.h`
- Modify: `spectrum_analyzer/src/spectrum_analyzer_engine.cpp`
- Modify: `tests/test_main.cpp`

**Interfaces:**
- Consumes: `applyTraceMode()` from Task 2
- Produces: `m_min_hold` buffer, MinHold branch in `applyTraceMode`

- [ ] **Step 1: Write failing MinHold test**

Add after the MaxHold test:

```cpp
TEST_CASE("MinHold accumulates per-bin minimum", "[spectrum][trace]") {
    SpectrumAnalyzerEngine sa;
    sa.setNoiseJitterEnabled(false);
    sa.setResBw(1e6);
    sa.setVideoBw(1e6);
    sa.setTraceMode(TraceMode::MinHold);

    Spectrum spec;
    spec.frequencies = {0, 1e6, 2e6, 3e6, 4e6};
    spec.tones = {{2e6, -10.0, 0.0}, {4e6, -10.0, 0.0}};
    spec.generation = 1;

    auto out1 = sa.renderSpectrum(spec);

    // Second frame: remove tone at bin 4
    spec.tones = {{2e6, -10.0, 0.0}};
    spec.generation = 2;
    auto out2 = sa.renderSpectrum(spec);

    // MinHold: bin 4 should still have the low value from frame 2
    // bin 2 should still have the high tone from frame 1 (held)
    REQUIRE(out2[4] < out2[2] - 10.0);  // bin 4 held low
    REQUIRE(out2[2] > out2[4] + 10.0);  // bin 2 still high
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build && ctest --test-dir build --output-on-failure -R "MinHold"`
Expected: FAIL — MinHold falls through to `ClearWrite` behavior (no `m_min_hold` branch yet)

- [ ] **Step 3: Add m_min_hold buffer and MinHold branch**

In `spectrum_analyzer/include/spectrum_analyzer_engine.h`, add after `m_max_hold`:

```cpp
    mutable std::unordered_map<const Spectrum*, std::vector<double>> m_min_hold;
```

In `spectrum_analyzer/src/spectrum_analyzer_engine.cpp`, update `applyTraceMode` — change the history selection:

```cpp
    std::unordered_map<const Spectrum*, std::vector<double>>* history = nullptr;
    if (m_trace_mode == TraceMode::MaxHold) history = &m_max_hold;
    else if (m_trace_mode == TraceMode::MinHold) history = &m_min_hold;
    if (!history) return after_vbw;
```

Add the MinHold branch after the MaxHold branch:

```cpp
    else if (m_trace_mode == TraceMode::MinHold) {
        for (size_t i = 0; i < h.size(); ++i)
            h[i] = std::min(h[i], after_vbw[i]);
    }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build && ctest --test-dir build --output-on-failure -R "MinHold"`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add spectrum_analyzer/include/spectrum_analyzer_engine.h spectrum_analyzer/src/spectrum_analyzer_engine.cpp tests/test_main.cpp
git commit -m "feat(spectrum): add MinHold trace mode"
```

---

### Task 4: VideoAverage Mode

**Files:**
- Modify: `spectrum_analyzer/include/spectrum_analyzer_engine.h`
- Modify: `spectrum_analyzer/src/spectrum_analyzer_engine.cpp`
- Modify: `tests/test_main.cpp`

**Interfaces:**
- Consumes: `applyTraceMode()` from Task 2/3
- Produces: `m_video_avg` buffer, VideoAverage branch in `applyTraceMode`

- [ ] **Step 1: Write failing VideoAverage tests**

Add after the MinHold test:

```cpp
TEST_CASE("VideoAverage converges to constant input", "[spectrum][trace]") {
    SpectrumAnalyzerEngine sa;
    sa.setNoiseJitterEnabled(false);
    sa.setResBw(1e6);
    sa.setVideoBw(1e6);
    sa.setTraceMode(TraceMode::VideoAverage);
    sa.setVideoAvgCount(5);

    Spectrum spec;
    spec.frequencies = {0, 1e6, 2e6, 3e6, 4e6};
    spec.noise_total_W = {1e-18, 1e-18, 1e-18, 1e-18, 1e-18};
    spec.generation = 1;

    // Feed many identical frames — EWMA should converge
    std::vector<double> prev;
    for (int i = 0; i < 50; ++i) {
        spec.generation = i + 1;
        prev = sa.renderSpectrum(spec);
    }

    // One more frame — should be essentially unchanged
    spec.generation = 51;
    auto out = sa.renderSpectrum(spec);
    for (size_t i = 0; i < out.size(); ++i) {
        REQUIRE(out[i] == Approx(prev[i]).epsilon(0.01));
    }
}

TEST_CASE("VideoAverage responds to step change", "[spectrum][trace]") {
    SpectrumAnalyzerEngine sa;
    sa.setNoiseJitterEnabled(false);
    sa.setResBw(1e6);
    sa.setVideoBw(1e6);
    sa.setTraceMode(TraceMode::VideoAverage);
    sa.setVideoAvgCount(3);

    // Two different constant spectra
    Spectrum spec_lo;
    spec_lo.frequencies = {0, 1e6, 2e6};
    spec_lo.noise_total_W = {1e-20, 1e-20, 1e-20};

    Spectrum spec_hi;
    spec_hi.frequencies = {0, 1e6, 2e6};
    spec_hi.noise_total_W = {1e-15, 1e-15, 1e-15};

    // Feed spec_lo for many frames to converge
    for (int i = 0; i < 100; ++i) {
        spec_lo.generation = i + 1;
        sa.renderSpectrum(spec_lo);
    }

    // Now switch to spec_hi (same pointer won't work — use different address)
    // The key is &spec, so we reuse spec_lo but change generation
    // Actually, to test step response we need the SAME spectrum pointer
    // with different noise values, so let's modify spec_lo in place
    spec_lo.noise_total_W = {1e-15, 1e-15, 1e-15};
    spec_lo.generation = 101;
    auto out = sa.renderSpectrum(spec_lo);

    // After one step, output should be between old and new (EWMA blend)
    // The old converged value was ~W_to_dBm(1e-20 * 1e6) = W_to_dBm(1e-14) = -110 dBm
    // The new raw value is ~W_to_dBm(1e-15 * 1e6) = W_to_dBm(1e-9) = -60 dBm
    // With alpha = 2/(3+1) = 0.5, one step: 0.5*(-60) + 0.5*(-110) = -85 dBm
    double expected_approx = -85.0;
    for (double v : out) {
        REQUIRE(v == Approx(expected_approx).margin(5.0));
    }
}
```

- [ ] **Step 2: Run test to verify they fail**

Run: `cmake --build build && ctest --test-dir build --output-on-failure -R "VideoAverage"`
Expected: FAIL — VideoAverage falls through to ClearWrite (no branch)

- [ ] **Step 3: Add m_video_avg buffer and VideoAverage branch**

In `spectrum_analyzer/include/spectrum_analyzer_engine.h`, add after `m_min_hold`:

```cpp
    mutable std::unordered_map<const Spectrum*, std::vector<double>> m_video_avg;
```

In `spectrum_analyzer/src/spectrum_analyzer_engine.cpp`, update `applyTraceMode` history selection:

```cpp
    std::unordered_map<const Spectrum*, std::vector<double>>* history = nullptr;
    if (m_trace_mode == TraceMode::MaxHold) history = &m_max_hold;
    else if (m_trace_mode == TraceMode::MinHold) history = &m_min_hold;
    else if (m_trace_mode == TraceMode::VideoAverage) history = &m_video_avg;
    if (!history) return after_vbw;
```

Add the VideoAverage branch:

```cpp
    else { // VideoAverage
        double alpha = 2.0 / (m_video_avg_count + 1);
        for (size_t i = 0; i < h.size(); ++i)
            h[i] = alpha * after_vbw[i] + (1.0 - alpha) * h[i];
    }
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build && ctest --test-dir build --output-on-failure -R "VideoAverage"`
Expected: PASS (2 tests)

- [ ] **Step 5: Commit**

```bash
git add spectrum_analyzer/include/spectrum_analyzer_engine.h spectrum_analyzer/src/spectrum_analyzer_engine.cpp tests/test_main.cpp
git commit -m "feat(spectrum): add VideoAverage trace mode with EWMA"
```

---

### Task 5: Lifecycle — resetTraceHistory, pruneHistory, Mode-Switch Reset

**Files:**
- Modify: `spectrum_analyzer/include/spectrum_analyzer_engine.h`
- Modify: `spectrum_analyzer/src/spectrum_analyzer_engine.cpp`
- Modify: `tests/test_main.cpp`

**Interfaces:**
- Consumes: All trace modes from Tasks 2-4
- Produces: `resetTraceHistory()`, `pruneHistory()` public methods. Mode switch resets history.

- [ ] **Step 1: Write failing lifecycle tests**

Add after the VideoAverage tests:

```cpp
TEST_CASE("Mode switch resets history", "[spectrum][trace]") {
    SpectrumAnalyzerEngine sa;
    sa.setNoiseJitterEnabled(false);
    sa.setResBw(1e6);
    sa.setVideoBw(1e6);

    Spectrum spec;
    spec.frequencies = {0, 1e6, 2e6, 3e6, 4e6};
    spec.tones = {{2e6, -10.0, 0.0}};
    spec.generation = 1;

    // Build up MaxHold history
    sa.setTraceMode(TraceMode::MaxHold);
    sa.renderSpectrum(spec);

    // Switch to ClearWrite — should not see held values
    sa.setTraceMode(TraceMode::ClearWrite);
    spec.tones = {};  // remove tone
    spec.generation = 2;
    auto out = sa.renderSpectrum(spec);

    // With no tone and ClearWrite, bin 2 should be low (noise floor)
    REQUIRE(out[2] < -50.0);
}

TEST_CASE("resetTraceHistory clears all buffers", "[spectrum][trace]") {
    SpectrumAnalyzerEngine sa;
    sa.setNoiseJitterEnabled(false);
    sa.setResBw(1e6);
    sa.setVideoBw(1e6);

    Spectrum spec;
    spec.frequencies = {0, 1e6, 2e6, 3e6, 4e6};
    spec.tones = {{2e6, -10.0, 0.0}};
    spec.generation = 1;

    sa.setTraceMode(TraceMode::MaxHold);
    sa.renderSpectrum(spec);
    sa.resetTraceHistory();

    // After reset, rendering with no tone should show noise floor (not held tone)
    spec.tones = {};
    spec.generation = 2;
    auto out = sa.renderSpectrum(spec);
    REQUIRE(out[2] < -50.0);
}

TEST_CASE("pruneHistory removes stale entries", "[spectrum][trace]") {
    SpectrumAnalyzerEngine sa;
    sa.setNoiseJitterEnabled(false);
    sa.setResBw(1e6);
    sa.setVideoBw(1e6);
    sa.setTraceMode(TraceMode::MaxHold);

    Spectrum spec;
    spec.frequencies = {0, 1e6, 2e6};
    spec.tones = {{1e6, -10.0, 0.0}};
    spec.generation = 1;
    sa.renderSpectrum(spec);

    // Prune with empty active set — should clear all history
    sa.pruneHistory({});

    // After prune, same spec should start fresh (size mismatch resets, so no held values)
    spec.tones = {};
    spec.generation = 2;
    auto out = sa.renderSpectrum(spec);
    REQUIRE(out[1] < -50.0);
}

TEST_CASE("Size mismatch resets history", "[spectrum][trace]") {
    SpectrumAnalyzerEngine sa;
    sa.setNoiseJitterEnabled(false);
    sa.setResBw(1e6);
    sa.setVideoBw(1e6);
    sa.setTraceMode(TraceMode::MaxHold);

    Spectrum spec;
    spec.frequencies = {0, 1e6, 2e6};
    spec.tones = {{1e6, -10.0, 0.0}};
    spec.generation = 1;
    sa.renderSpectrum(spec);

    // Change spectrum size
    spec.frequencies = {0, 1e6, 2e6, 3e6, 4e6, 5e6};
    spec.tones = {};
    spec.generation = 2;
    auto out = sa.renderSpectrum(spec);

    // All bins should be noise floor (no held values from old-size history)
    for (double v : out) {
        REQUIRE(v < -50.0);
    }
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build && ctest --test-dir build --output-on-failure -R "Mode switch\|resetTraceHistory\|pruneHistory\|Size mismatch"`
Expected: FAIL — `resetTraceHistory()` and `pruneHistory()` not defined

- [ ] **Step 3: Add resetTraceHistory and pruneHistory**

In `spectrum_analyzer/include/spectrum_analyzer_engine.h`, add to public section (after `videoAvgCount()`):

```cpp
    void resetTraceHistory();
    void pruneHistory(const std::vector<const Spectrum *> &active_keys);
```

In `spectrum_analyzer/src/spectrum_analyzer_engine.cpp`, add implementations (after `applyTraceMode`):

```cpp
void SpectrumAnalyzerEngine::resetTraceHistory() const {
    m_max_hold.clear();
    m_min_hold.clear();
    m_video_avg.clear();
}

void SpectrumAnalyzerEngine::pruneHistory(
    const std::vector<const Spectrum *> &active_keys) const
{
    std::unordered_set<const Spectrum*> active(active_keys.begin(), active_keys.end());
    for (auto* map : {&m_max_hold, &m_min_hold, &m_video_avg}) {
        for (auto it = map->begin(); it != map->end(); ) {
            if (active.find(it->first) == active.end()) {
                it = map->erase(it);
            } else {
                ++it;
            }
        }
    }
}
```

Add `#include <unordered_set>` at the top of `spectrum_analyzer_engine.cpp` (after existing includes).

For mode-switch reset: update `setTraceMode` in the header (replace the inline setter):

Change:
```cpp
    void setTraceMode(TraceMode m) { m_trace_mode = m; }
```
To:
```cpp
    void setTraceMode(TraceMode m);
```

Add implementation in `spectrum_analyzer_engine.cpp`:

```cpp
void SpectrumAnalyzerEngine::setTraceMode(TraceMode m) {
    m_trace_mode = m;
    resetTraceHistory();
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build && ctest --test-dir build --output-on-failure -R "trace"`
Expected: All trace tests PASS

- [ ] **Step 5: Run full test suite**

Run: `ctest --test-dir build --output-on-failure`
Expected: All tests PASS

- [ ] **Step 6: Commit**

```bash
git add spectrum_analyzer/include/spectrum_analyzer_engine.h spectrum_analyzer/src/spectrum_analyzer_engine.cpp tests/test_main.cpp
git commit -m "feat(spectrum): add resetTraceHistory, pruneHistory, mode-switch reset"
```

---

### Task 6: Widget UI — Combo, Slider, Button, Status Line

**Files:**
- Modify: `spectrum_analyzer/src/spectrum_analyzer_widget.cpp`

**Interfaces:**
- Consumes: All engine API from Tasks 1-5
- Produces: UI controls for trace mode selection, avg count, reset, and peak readout

- [ ] **Step 1: Add trace mode controls to widget**

In `spectrum_analyzer/src/spectrum_analyzer_widget.cpp`, in the `draw()` method, insert after the RBW control (after line 147):

```cpp
    // Trace mode controls
    static const char* trace_mode_labels[] = { "Clear/Write", "Max Hold", "Min Hold", "Video Avg" };
    static const TraceMode trace_mode_values[] = {
        TraceMode::ClearWrite, TraceMode::MaxHold, TraceMode::MinHold, TraceMode::VideoAverage
    };
    int current_mode_idx = 0;
    for (int i = 0; i < 4; ++i) {
        if (m_engine.traceMode() == trace_mode_values[i]) { current_mode_idx = i; break; }
    }
    if (ImGui::Combo("Trace Mode", &current_mode_idx, trace_mode_labels, 4)) {
        m_engine.setTraceMode(trace_mode_values[current_mode_idx]);
    }

    int avg_count = m_engine.videoAvgCount();
    bool avg_enabled = (m_engine.traceMode() == TraceMode::VideoAverage);
    if (!avg_enabled) ImGui::BeginDisabled();
    if (ImGui::SliderInt("Avg Count", &avg_count, 2, 100)) {
        m_engine.setVideoAvgCount(avg_count);
    }
    if (!avg_enabled) ImGui::EndDisabled();

    bool show_reset = (m_engine.traceMode() == TraceMode::MaxHold ||
                       m_engine.traceMode() == TraceMode::MinHold);
    if (show_reset) {
        if (ImGui::Button("Reset Hold")) {
            m_engine.resetTraceHistory();
        }
    }
```

- [ ] **Step 2: Add peak status line**

After the "Average noise level" text (after line 336), add:

```cpp
    // Trace mode + peak readout
    {
        const char* mode_name = "Clear/Write";
        switch (m_engine.traceMode()) {
            case TraceMode::MaxHold: mode_name = "Max Hold"; break;
            case TraceMode::MinHold: mode_name = "Min Hold"; break;
            case TraceMode::VideoAverage: mode_name = "Video Avg"; break;
            default: break;
        }
        // Find peak in combined display
        double peak_val = -174.0;
        double peak_freq = 0.0;
        if (!combined_dBm.empty() && !freq_axis->empty()) {
            for (size_t i = 0; i < combined_dBm.size() && i < freq_axis->size(); ++i) {
                if (combined_dBm[i] > peak_val) {
                    peak_val = combined_dBm[i];
                    peak_freq = (*freq_axis)[i];
                }
            }
        }
        ImGui::Text("Trace: %s | Peak: %.2f MHz, %.2f dBm",
                    mode_name, peak_freq / 1e6, peak_val);
    }
```

- [ ] **Step 3: Wire pruneHistory call**

In the `draw()` method, after building `visible_specs` (after line 206), add:

```cpp
    // Prune stale trace history for nodes no longer visible
    std::vector<const Spectrum*> all_active;
    for (auto* node : active_nodes) {
        if (!node) continue;
        auto pfbIter = m_pfb_map.find(node);
        all_active.push_back(pfbIter != m_pfb_map.end()
            ? &node->outputs[1] : &node->outputs[0]);
    }
    m_engine.pruneHistory(all_active);
```

- [ ] **Step 4: Build and verify**

Run: `cmake --build build`
Expected: Build succeeds

- [ ] **Step 5: Manual verification**

Run the application, open the spectrum analyzer window. Verify:
- Trace Mode combo appears and switches modes
- Avg Count slider is enabled only in Video Avg mode
- Reset Hold button appears only in Max Hold / Min Hold modes
- Peak status line shows current mode and peak frequency/power
- Max Hold: observe peaks staying visible after signal moves
- Min Hold: observe dips staying visible after signal fills them
- Video Avg: observe smoothing of noisy display

- [ ] **Step 6: Commit**

```bash
git add spectrum_analyzer/src/spectrum_analyzer_widget.cpp
git commit -m "feat(spectrum): add trace mode UI controls and peak status line"
```
