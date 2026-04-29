# Amplifier Widget Refactor — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor the amplifier widget to match the signal generator widget pattern: one widget manages multiple amplifier engines via an add/remove table, with no bin width parameter.

**Architecture:** Remove `f_step_Hz` from `AmplifierEngine` so it only applies gain/NF to its input spectrum. Convert `AmplifierWidget` from a single-engine view to a multi-engine manager with an ImGui table. Update `RfSimulatorApp` to hold a single `AmplifierWidget` and provide add/remove callbacks.

**Tech Stack:** C++20, ImGui, ImPlot, CMake/Ninja

---

### Task 1: Remove bin width from AmplifierEngine

**Files:**
- Modify: `amplifier/include/amplifier_engine.h`
- Modify: `amplifier/src/amplifier_engine.cpp`

- [ ] **Step 1: Remove bin width members and methods from header**

In `amplifier/include/amplifier_engine.h`:
- Remove `setFreqStep(double Hz)`
- Remove `f_step_Hz() const`
- Remove `m_f_step_Hz` member

- [ ] **Step 2: Update update() to remove bin width dependency**

In `amplifier/src/amplifier_engine.cpp`:
- In `update()`, the fallback branch that builds `out.frequencies` from `m_f_step_Hz` should use a hard-coded default step (e.g. 10 MHz) instead of reading `m_f_step_Hz`.

- [ ] **Step 3: Build and verify**

Run: `cmake --build build`
Expected: Compiles cleanly

---

### Task 2: Convert AmplifierWidget to multi-engine table UI

**Files:**
- Modify: `amplifier/include/amplifier_widget.h`
- Modify: `amplifier/src/amplifier_widget.cpp`

- [ ] **Step 1: Rewrite AmplifierWidget header**

`amplifier/include/amplifier_widget.h` should:
- Accept a `std::vector<std::unique_ptr<AmplifierEngine>>&` instead of a single `AmplifierEngine&`
- Provide `std::function<void()>` for `onAddAmplifier`
- Provide `std::function<void(size_t)>` for `onRemoveAmplifier`

- [ ] **Step 2: Rewrite AmplifierWidget draw() to show a table**

`amplifier/src/amplifier_widget.cpp` should:
- Draw an ImGui table with columns: `#`, `Gain (dB)`, `NF (dB)`, `X`
- Each row reads/writes `engine->gain_dB()` and `engine->nf_dB()` via `utils::inputDouble`
- `X` button calls `onRemoveAmplifier(index)`
- `+ Add Amplifier` button calls `onAddAmplifier()`
- Remove the "Measure" checkbox (probing is handled by the node graph)
- Remove the "Bin width" input entirely

- [ ] **Step 3: Build and verify**

Run: `cmake --build build`
Expected: Compiles cleanly

---

### Task 3: Update RfSimulatorApp to use single AmplifierWidget

**Files:**
- Modify: `app/include/app.h`
- Modify: `app/src/app.cpp`

- [ ] **Step 1: Update app.h**

- Change `m_amplifier_widgets` from `std::vector<std::unique_ptr<AmplifierWidget>>` to `std::unique_ptr<AmplifierWidget>`
- No other members change

- [ ] **Step 2: Update app.cpp**

- In constructor: create the single `AmplifierWidget` after first amp is added, passing `m_amplifiers` and wiring callbacks
- In `addAmplifier()`: no longer push a widget; just create the engine
- In `removeComponent()`: after removing an amp, if `m_amplifiers` is empty, optionally clear or recreate widget (not strictly needed since widget references the vector)
- In `draw_ui()`: call `m_amplifier_widget->draw("Amplifiers")` once instead of looping over widgets

- [ ] **Step 3: Build and run tests**

Run: `cmake --build build && ctest --test-dir build`
Expected: Builds and tests pass

---

### Task 4: Manual verification

- [ ] **Step 1: Run the app**

Run: `build/bin/main.exe`

- [ ] **Step 2: Verify UI**

Checklist:
- One "Amplifiers" window appears
- Can add multiple amplifiers via `+ Add Amplifier`
- Can edit gain and NF per row
- Can remove amplifiers via `X`
- No "Bin width" parameter visible
- No "Measure" checkbox in Amplifiers window
- Spectrum analyzer still shows correct gain + noise

---

## Self-Review

**Spec coverage:**
- Remove bin width from engine → Task 1
- Multi-engine table widget → Task 2
- Single widget in app with callbacks → Task 3
- Manual verification → Task 4

**Placeholder scan:** No TBD/TODO placeholders.

**Type consistency:** All references to `AmplifierEngine` and `AmplifierWidget` match existing codebase patterns.
