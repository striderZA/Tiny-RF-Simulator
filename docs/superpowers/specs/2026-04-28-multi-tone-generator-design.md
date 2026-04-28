# Multi-Tone Signal Generator Design

## Problem

Currently, `RfSimulatorApp` creates 4 `SignalGeneratorEngine` instances, each with 1 tone + a full thermal noise floor (`k*T` W/Hz). When multiple generators are measured simultaneously, the spectrum analyzer sums power per bin across all active nodes, causing the noise floor to scale with the number of generators (4× power = ~6 dB increase). This is incorrect — a real RF source has one thermal noise floor regardless of how many tones it produces.

## Solution

Collapse into a **single signal generator** that produces N independent tones + one shared thermal noise floor. The noise floor stays constant at `k*T` regardless of tone count.

## Design

### 1. Engine: `signal_generator_engine.h/.cpp`

**Data model change:**
```
Before: tone m_active_tone;                    // std::pair<double, double>
After:  std::vector<Spectrum::Tone> m_tones;   // N independent tones
```

`Spectrum::Tone` is already defined in `spectrum.h`:
```cpp
struct Tone {
    double freq_Hz = 0.0;
    double power_dBm = -174;
};
```

**New/removed API:**
- Remove: `setToneFrequency(double)`, `setToneAmplitude(double)`, `activeTone()`
- Add: `addTone(double freq_Hz, double power_dBm)` — appends with defaults
- Add: `removeTone(size_t index)` — removes by index
- Add: `updateTone(size_t index, double freq_Hz, double power_dBm)` — modifies existing
- Add: `const std::vector<Spectrum::Tone>& tones() const`
- Add: `size_t toneCount() const` — convenience

**Default constructor:** No default tone. First tone added via `addTone()` or UI.

**`update(double dt)` changes:**
- Iterate `m_tones`, push each as a `Spectrum::Tone` into `out.tones`
- Noise path unchanged: flat `k*T` density, unity gain, no added noise
- Empty `m_tones` → no tones in output, noise floor still present

### 2. Widget: `signal_generator_widget.h/.cpp`

Replace single freq + amplitude controls with a **tone table**:

```
+------------------------------------------+
| [Measure] ☐                               |
|                                            |
| Tones:                                     |
| # | Frequency (MHz) | Amplitude (dBm) |   |
|---|-----------------|-----------------|---|
| 1 | [  100.0     ]  | [   -20      ]  |[X]|
| 2 | [  200.0     ]  | [    -5      ]  |[X]|
|---|-----------------|-----------------|---|
| [+ Add Tone]                              |
+------------------------------------------+
```

- Each row: `utils::inputFrequency()` + `utils::inputDouble()` + delete button
- `[+ Add Tone]` appends a tone at (100 MHz, -60 dBm) default
- Deleting last tone → empty list (no tones, just noise floor)
- "Measure" checkbox unchanged

### 3. App: `app.h/.cpp`

**Member change:**
```
Before:
  std::vector<std::unique_ptr<SignalGeneratorEngine>> m_generators;  // 4
  std::vector<std::unique_ptr<SignalGeneratorWidget>> m_generator_widgets;
After:
  std::unique_ptr<SignalGeneratorEngine> m_generator;               // 1
  std::unique_ptr<SignalGeneratorWidget> m_generator_widget;
```

**Constructor:** Create 1 generator (was 4). Default tone: 100 MHz, -20 dBm.

**`update_dsp()`:**
```cpp
m_generator->update(0.0);
if (!m_amplifiers.empty()) {
    m_amplifiers[0]->node().input = m_generator->node().output;
    m_amplifiers[0]->update(0.0);
}
```

**Signal chain panel:** Remove "Add Generator"/"Remove Generator" buttons. Show single generator widget inline.

**`InputSignals` enum:** Remove (`G0, G1, G2, G3, COUNT` no longer applies).

### 4. Tests

Update `tests/test_main.cpp`:

- `Generator outputs flat thermal noise density` — already tone-agnostic, no changes needed
- `Amplifier scales noise density correctly` — already tone-agnostic, no changes needed
- `Spectrum analyzer noise floor depends on RBW not grid spacing` — already tone-agnostic, no changes needed

Add new tests for multi-tone behavior:
- `Generator with no tones produces no tones in output`
- `Generator with N tones produces N tones in output`
- `Generator addTone / removeTone / updateTone work correctly`
- `Noise floor remains k*T regardless of tone count`

### 5. Files Changed

| File | Change |
|------|--------|
| `signal_generator/include/signal_generator_engine.h` | Replace single tone with `std::vector<Spectrum::Tone>`, new API |
| `signal_generator/src/signal_generator_engine.cpp` | Rewrite `update()`, remove/replace tone setters |
| `signal_generator/include/signal_generator_widget.h` | No structural change (still holds `Engine&`) |
| `signal_generator/src/signal_generator_widget.cpp` | Rewrite draw() with tone table |
| `app/include/app.h` | Replace `vector<Generator>` with `unique_ptr<Generator>` |
| `app/src/app.cpp` | Update constructor, `update_dsp()`, signal chain UI |
| `CMakeLists.txt` | Possibly update if target structure changes (unlikely) |
| `tests/test_main.cpp` | Add multi-tone tests |
