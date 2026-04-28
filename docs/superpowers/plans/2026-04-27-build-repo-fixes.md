# Build & Repo Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix 10 identified issues: 1 failing test, 2 functional bugs, 3 hygiene issues, 1 architectural fix, 3 minor polish items.

**Architecture:** Fix each issue in isolation. Each task is independently testable. TDD for the test fix; minimal changes for everything else. Frequent commits per task.

**Tech Stack:** C++20, CMake, Catch2 v3.4.0, ImGui, ImPlot

---

### Task 1: Fix failing "Added noise per bin" unit test

**Files:**
- Modify: `tests/test_main.cpp:22-32`

- [ ] **Step 1: Fix the test expectation**

The test at line 22-26 expects `addedNoisePerBin_W(0.0, 1.0, 1.0)` to equal `k * T`. But with NF = 0 dB, `Te = T * (10^(0/10) - 1) = T * 0 = 0`, so added noise is correctly `0.0`. The test should expect `0.0`.

Replace the entire `TEST_CASE("Added noise per bin", "[common]")` block in `tests/test_main.cpp`:

```cpp
TEST_CASE("Added noise per bin", "[common]") {
    // With noise figure 0 dB => Te = 0 => added noise = 0 regardless of gain
    double added = addedNoisePerBin_W(0.0, 1.0, 1.0);
    REQUIRE(added == Approx(0.0));

    // With gain 10 (10 dB), noise figure 10 dB, bin width 1e6 Hz
    double added2 = addedNoisePerBin_W(10.0, dbToLinear(10.0), 1e6);
    double expected2 = k * calculateNoiseTemp(10.0) * dbToLinear(10.0) * 1e6;
    REQUIRE(added2 == Approx(expected2).epsilon(0.001));
}
```

- [ ] **Step 2: Run tests to verify all pass**

```bash
ctest --test-dir build --output-on-failure
```

Expected: 4/4 tests pass.

- [ ] **Step 3: Commit**

```bash
git add tests/test_main.cpp
git commit -m "fix: correct added noise per bin test expectation for NF=0"
```

---

### Task 2: Fix ImGui ID collision in amplifier window titles

**Files:**
- Modify: `app/src/app.cpp:78`

- [ ] **Step 1: Fix the ID suffix**

Line 78 in `app/src/app.cpp` uses `"Amplifier %d##gen%zu"` — the `##gen` suffix collides with generator windows at the same index. Change to `##amp`:

```cpp
std::snprintf(title_buffer, sizeof(title_buffer), "Amplifier %d##amp%zu",
              m_amplifiers[i]->id(), i);
```

- [ ] **Step 2: Commit**

```bash
git add app/src/app.cpp
git commit -m "fix: use unique ImGui ID suffix for amplifier windows"
```

---

### Task 3: Fix signal generator frequency grid not rebuilding on bin width change

**Files:**
- Modify: `signal_generator/include/signal_generator_engine.h`
- Modify: `signal_generator/src/signal_generator_engine.cpp`

- [ ] **Step 1: Add a private `rebuildFrequencyGrid()` method**

In `signal_generator/include/signal_generator_engine.h`, add a private method declaration:

```cpp
void rebuildFrequencyGrid();
```

Add it in the `private:` section after `m_f_step_Hz`.

- [ ] **Step 2: Implement `rebuildFrequencyGrid()`**

In `signal_generator/src/signal_generator_engine.cpp`, extract the grid-building logic from the constructor into a reusable method:

```cpp
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
    m_node.input.noise_total_W.assign(n, k * T * m_f_step_Hz);
    m_node.output.computeTotalNoise();
}
```

- [ ] **Step 3: Update constructor to call `rebuildFrequencyGrid()`**

Replace the constructor body (lines 6-21) with:

```cpp
SignalGeneratorEngine::SignalGeneratorEngine(int id)
    : m_id(id), m_active_tone(std::make_pair<int, double>(0, -60.0)) {
    rebuildFrequencyGrid();
}
```

- [ ] **Step 4: Make `setFreqStep()` rebuild the grid**

Change `setFreqStep` in the header from:
```cpp
void setFreqStep(double Hz) { m_f_step_Hz = Hz; }
```
to:
```cpp
void setFreqStep(double Hz) { m_f_step_Hz = Hz; rebuildFrequencyGrid(); }
```

- [ ] **Step 5: Build and verify**

```bash
cmake --build build
```

Expected: clean build, no warnings.

- [ ] **Step 6: Commit**

```bash
git add signal_generator/include/signal_generator_engine.h signal_generator/src/signal_generator_engine.cpp
git commit -m "fix: rebuild frequency grid when bin width changes"
```

---

### Task 4: Remove stale private method declarations in spectrum_analyzer_engine.h

**Files:**
- Modify: `spectrum_analyzer/include/spectrum_analyzer_engine.h:41-42`

- [ ] **Step 1: Remove dead code**

Delete lines 41-42 from `spectrum_analyzer/include/spectrum_analyzer_engine.h`:

```cpp
// DELETE THESE TWO LINES:
std::vector<double> applyRbw(const std::vector<double> &power_W, double bin_width) const;
std::vector<double> applyVbw(const std::vector<double> &power_dBm, double bin_width) const;
```

The actual implementations use uppercase `applyRBW` and `applyVBW` (declared at lines 27-28).

- [ ] **Step 2: Build and verify**

```bash
cmake --build build
```

Expected: clean build.

- [ ] **Step 3: Commit**

```bash
git add spectrum_analyzer/include/spectrum_analyzer_engine.h
git commit -m "chore: remove stale private method declarations from spectrum engine"
```

---

### Task 5: Add imgui.ini and out/ to .gitignore

**Files:**
- Modify: `.gitignore`

- [ ] **Step 1: Add entries to .gitignore**

Append these lines to `.gitignore`:

```
# Runtime-generated ImGui settings
imgui.ini

# Visual Studio CMake build output
out/
```

- [ ] **Step 2: Remove imgui.ini from git tracking**

```bash
git rm --cached imgui.ini
```

- [ ] **Step 3: Verify git status**

```bash
git status --short
```

Expected: `imgui.ini` no longer appears, `out/` not tracked.

- [ ] **Step 4: Commit**

```bash
git add .gitignore imgui.ini
git commit -m "chore: ignore imgui.ini and out/ directory in git"
```

---

### Task 6: Move utils.h from common/ to core/ (architectural fix)

**Files:**
- Create: `core/include/utils.h`
- Delete: `common/utils.h`
- Modify: `amplifier/src/amplifier_widget.cpp:4`
- Modify: `spectrum_analyzer/src/spectrum_analyzer_widget.cpp:6`
- Modify: `signal_generator/src/signal_generator_widget.cpp:5`

- [ ] **Step 1: Move utils.h to core/include/**

Create `core/include/utils.h` with the exact same content as `common/utils.h`. The file includes `imgui.h` and `logging_core.h`, both of which are already accessible through `simulator::core`'s PUBLIC dependencies.

```cpp
#pragma once

#include "imgui.h"
#include "logging_core.h"

namespace utils {
static bool inputDouble(std::string label, double &ref, double minorStep, double majorStep,
                        const char *format, double lowerLimit, double upperLimit) {
    // clamp external writes to ref BEFORE drawing widget
    if (ref > upperLimit) {
        LOG_WARN("Unable to update value: %s! (above upper limit)", label.c_str());
        ref = upperLimit;
    } else if (ref < lowerLimit) {
        LOG_WARN("Unable to update value: %s! (below lower limit)", label.c_str());
        ref = lowerLimit;
    }

    // perform ImGui update
    bool changed = ImGui::InputDouble(label.c_str(), &ref, minorStep, majorStep, format);

    // optionally clamp AFTER user change too
    if (ref > upperLimit) {
        ref = upperLimit;
        changed = true;
    }
    if (ref < lowerLimit) {
        ref = lowerLimit;
        changed = true;
    }

    return changed;
}
static bool inputFrequency(const char* label, double& freq_Hz, double minorStep_MHz, double majorStep_MHz,
                          const char* format, double lowerLimit_Hz, double upperLimit_Hz) {
    // clamp external writes to freq_Hz BEFORE drawing widget
    if (freq_Hz > upperLimit_Hz) {
        LOG_WARN("Unable to update frequency: %s! (above upper limit)", label);
        freq_Hz = upperLimit_Hz;
    } else if (freq_Hz < lowerLimit_Hz) {
        LOG_WARN("Unable to update frequency: %s! (below lower limit)", label);
        freq_Hz = lowerLimit_Hz;
    }

    double freq_MHz = freq_Hz / 1e6;
    double minorStep = minorStep_MHz;
    double majorStep = majorStep_MHz;
    bool changed = ImGui::InputDouble(label, &freq_MHz, minorStep, majorStep, format);

    if (changed) {
        freq_Hz = freq_MHz * 1e6;
        // optionally clamp AFTER user change too
        if (freq_Hz > upperLimit_Hz) {
            LOG_WARN("Unable to update frequency: %s! (above upper limit)", label);
            freq_Hz = upperLimit_Hz;
        } else if (freq_Hz < lowerLimit_Hz) {
            LOG_WARN("Unable to update frequency: %s! (below lower limit)", label);
            freq_Hz = lowerLimit_Hz;
        }
    }
    return changed;
}

} // namespace utils
```

- [ ] **Step 2: Update include paths in all 3 widget files**

In each of these files, change `#include "utils.h"` to `#include "utils.h"` — the include path stays the same because `core/include/` is already in the PUBLIC include directories of `simulator::core`, and all 3 widgets already link `simulator::core`.

No include path changes are needed. The only change is deleting the old file.

- [ ] **Step 3: Delete common/utils.h**

```bash
git rm common/utils.h
```

- [ ] **Step 4: Build and verify**

```bash
cmake --build build
```

Expected: clean build. All widgets resolve `utils.h` through `simulator::core`'s PUBLIC include path.

- [ ] **Step 5: Commit**

```bash
git add core/include/utils.h common/utils.h
git commit -m "refactor: move utils.h from common/ to core/ to remove UI dep from common"
```

---

### Task 7: Fix BOM in spectrum_analyzer_widget.cpp

**Files:**
- Modify: `spectrum_analyzer/src/spectrum_analyzer_widget.cpp`

- [ ] **Step 1: Remove BOM**

The file starts with a UTF-8 BOM (bytes `EF BB BF`). Rewrite the file without BOM. The content is unchanged — only the encoding changes to UTF-8 without BOM.

Read the current file content and write it back without the BOM prefix.

- [ ] **Step 2: Verify no BOM**

```bash
# In PowerShell:
$bytes = [System.IO.File]::ReadAllBytes("spectrum_analyzer/src/spectrum_analyzer_widget.cpp")
if ($bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
    Write-Output "BOM still present"
} else {
    Write-Output "BOM removed"
}
```

Expected: "BOM removed".

- [ ] **Step 3: Commit**

```bash
git add spectrum_analyzer/src/spectrum_analyzer_widget.cpp
git commit -m "chore: remove UTF-8 BOM from spectrum_analyzer_widget.cpp"
```

---

### Task 8: Fix shutdown path in RfSimulatorCore

**Files:**
- Modify: `core/src/core.cpp:18-23`

- [ ] **Step 1: Fix Run() to call Shutdown() on init failure**

Replace the `Run()` method (lines 18-23):

```cpp
void RfSimulatorCore::Run(const std::function<void()> &onGui) {
    if (!Initialize()) {
        Shutdown();
        return;
    }
    MainLoop(onGui);
    Shutdown();
}
```

- [ ] **Step 2: Build and verify**

```bash
cmake --build build
```

Expected: clean build.

- [ ] **Step 3: Commit**

```bash
git add core/src/core.cpp
git commit -m "fix: call Shutdown() when Initialize() fails in RfSimulatorCore"
```

---

## Self-Review

### 1. Spec coverage
| Issue | Task |
|---|---|
| Failing test | Task 1 |
| ImGui ID collision | Task 2 |
| Freq grid not rebuilt | Task 3 |
| Stale declarations | Task 4 |
| imgui.ini in git | Task 5 |
| out/ not ignored | Task 5 |
| utils.h in common/ | Task 6 |
| BOM in file | Task 7 |
| RBW edge effects | Not included (documented as low-priority, needs design decision) |
| Shutdown path | Task 8 |

RBW edge effects excluded — it's a design decision about padding strategy, not a clear bug.

### 2. Placeholder scan
No TBD, TODO, or placeholder patterns found. All steps contain exact code and commands.

### 3. Type consistency
- `SignalGeneratorEngine::rebuildFrequencyGrid()` uses existing member variables (`m_f_step_Hz`, `m_node`, `MIN_FREQ`, `MAX_FREQ`, `k`, `T`) — all consistent with existing code.
- All include paths verified: `core/include/` is PUBLIC in `simulator::core`, all widgets link `simulator::core`.
