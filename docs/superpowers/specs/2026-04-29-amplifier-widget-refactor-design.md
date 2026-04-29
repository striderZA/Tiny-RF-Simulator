# Amplifier Widget Refactor — Design Spec

## Overview

Refactor the amplifier module so its widget behaves like the signal generator widget:
- One `AmplifierWidget` controls all amplifiers (add/remove rows)
- Each row is a single amplifier engine with its own gain and noise figure
- No bin width parameter in widget or engine

## Motivation

Current UX: each amplifier spawns a separate window and exposes an unnecessary bin-width knob. Generator UX is cleaner (one window, table of tones, no frequency-grid knob). Aligning amplifier UX to generator UX reduces UI clutter and cognitive load.

## Architecture

### Engine changes (`AmplifierEngine`)

- Remove `m_f_step_Hz`, `setFreqStep()`, `f_step_Hz()`
- In `update()`:
  - If input has frequencies, use them
  - Else fall back to a hard-coded default grid (e.g. 10 MHz steps over the full span) — no user control needed
- Engine still owns one `SignalNode` (input/output) and has `gain_dB`, `nf_dB` per instance

### Widget changes (`AmplifierWidget`)

- Holds a reference vector to all `AmplifierEngine`s (not a single engine)
- Renders an ImGui table with columns: `#`, `Gain (dB)`, `NF (dB)`, `X`
- Gain and NF edited via `utils::inputDouble` with appropriate min/max clamps
- `+ Add Amplifier` button creates a new engine instance (delegated to `RfSimulatorApp` via callback)
- `X` per row removes that engine
- `Measure` checkbox toggles view visibility on the selected/probed node? No — `view_enabled` is per `SignalNode`; each row's engine has its own node, so each row can have a "Measure" toggle (or we keep the existing pattern where spectrum auto-probes). To keep it simple and consistent with generator, we **omit** a per-row Measure checkbox; spectrum analyzer probing is handled by the node graph.

Actually, re-checking generator widget: it has a "Measure" checkbox per generator window. Since we'll have ONE amplifier window now, we can't have a single checkbox for all amps. The simplest solution: **remove the Measure checkbox from AmplifierWidget entirely** — probing is done via the node graph only. This is cleaner.

### App changes (`RfSimulatorApp`)

- Change `m_amplifier_widgets` from `vector<unique_ptr<AmplifierWidget>>` to a **single** `unique_ptr<AmplifierWidget>`
- Provide callbacks to `AmplifierWidget`:
  - `onAddAmplifier()` → `addAmplifier()`
  - `onRemoveAmplifier(size_t index)` → remove engine at index
- `draw_ui()` calls `m_amplifier_widget->draw("Amplifiers")` once
- `addAmplifier()` logic unchanged (creates engine, registers with view manager)

## Data Flow

Same as before: each `AmplifierEngine::update()` reads `m_node.input` (populated by `RfSimulatorApp::update_dsp()` from the graph source), applies gain and noise, writes to `m_node.output`.

## UI Mock

```
+----------------------------------+
| Amplifiers                    [X]|
+----------------------------------+
| #  | Gain (dB) | NF (dB) |      |
| 1  |    10.0   |   3.0   | [X]  |
| 2  |    20.0   |   1.0   | [X]  |
+----------------------------------+
| [+ Add Amplifier]                |
+----------------------------------+
```

## Testing

- Build and run `build/bin/main.exe`
- Verify one "Amplifiers" window appears
- Add multiple amps, set different gains/NF
- Verify spectrum reflects gain + noise correctly
- Verify no bin width control exists

## Files Modified

- `amplifier/include/amplifier_engine.h`
- `amplifier/src/amplifier_engine.cpp`
- `amplifier/include/amplifier_widget.h`
- `amplifier/src/amplifier_widget.cpp`
- `app/include/app.h`
- `app/src/app.cpp`
