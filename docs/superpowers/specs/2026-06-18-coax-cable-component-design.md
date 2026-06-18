# Coax Cable Component — Design Spec

**Date:** 2026-06-18
**Status:** Approved (pending review of this document)
**Branch:** `docs/coax-cable-component-design`
**Datasheet source:** [Times Microwave MilTech 340 (Rev. 6, 04/2024)](https://timesmicrowave.com/wp-content/uploads/2024/04/miltech-340-coax-cables-datasheet.pdf)

---

## 1. Goal

Add a new signal-chain component — a **coax cable** — to RF Simulator, modelled after the MilTech family of hermetically-sealed flexible RF/microwave transmission-line assemblies. The cable applies frequency-dependent insertion loss and phase shift to a `Spectrum`, parameterised by a preset (cable type), a physical length, and an optional connector loss. This unblocks realistic receive/transmit chain simulations where cabling between components is non-negligible.

## 2. Scope

### In scope (v1)

- A new `coax/` module with `CoaxCableEngine` and `CoaxCableWidget`.
- A preset table for the **MilTech family** (MT 210, 230, 265, 300, 340, 480). MT 340 fully populated from the datasheet; the other five are stubs (K1=K2=delay=0) to be filled in from each cable's own datasheet in a follow-up.
- Per-tone insertion loss `IL(f) = (K1·√f + K2·f)·L + connectors_loss` with `f` in MHz and `L` in metres.
- Per-tone and per-bin phase shift `Δφ = −360°·f_GHz·L·delay_ns_per_m·1e−3`.
- User-editable **length (m)** and **connector loss (dB)**.
- Cable-aware UI: preset dropdown, length input, connector loss input, computed readouts of loss/phase at the spectrum centre frequency.
- Node-graph integration: new "Coax Cable" entry in the add-component menu.
- ~12 unit tests including datasheet spot-checks.

### Out of scope (deferred)

- **Thermal noise from the cable** (fidelity C from brainstorming). A passive lossy component at ambient T₀ = 290 K adds `k·T₀·(1 − 1/L_linear)` per Hz; modelling this is a follow-up that requires a cable-temperature parameter. With fidelity B, the chain's noise analysis is slightly conservative (or optimistic, depending on which side of the cable the LNA sits), and the existing `noise_added_W` field already supports this addition later.
- **VSWR / S11 / S22 mismatch**. The cable is treated as a perfect 50 Ω matched two-port. A real cable has a finite VSWR spec; modelling return loss would require extending the engine to track reflected power, which is not in this design.
- **Cable groups / series segments**. A long run is one engine with one length, not multiple concatenated engines. Concatenation is possible today by chaining two cable components in the node graph if needed.
- **Non-MilTech cables** (LMR-400, RG-58, etc.). The data structure (`CableSpec`) accepts any K1/K2/delay/max_freq, but v1 ships with only MilTech presets.
- **Length units other than metres**. Feet/inches can be added by extending the widget later.

## 3. Datasheet parameters (MT 340)

| Parameter | Value |
|---|---|
| Impedance | 50 Ω |
| K1 (dB/m) | 0.004710 |
| K2 (dB/m) | 0.000004 |
| Delay (ns/m) | 0.4 (equivalently 1.25 ns/ft) |
| Max operating frequency | 18.5 GHz |
| Outer diameter | 8.6 mm |
| Capacitance | 83.3 pF/m |
| Insertion loss formula | `IL [dB] = (K1·√f + K2·f)·L + connectors_loss`, f in MHz |

## 4. Architecture

### Module layout

```
coax/
├── CMakeLists.txt
├── include/
│   ├── coax_cable_engine.h     # IComponentEngine impl
│   ├── coax_cable_widget.h     # ImGui panel
│   └── coax_presets.h          # CableSpec + kCoaxCablePresets
└── src/
    ├── coax_cable_engine.cpp
    └── coax_cable_widget.cpp
```

Follows the existing engine+widget pattern (see `amplifier/`, `splitter/`, `mixer/`, `signal_generator/`). Only widget TUs include ImGui headers.

### CMake targets

- `coax_cable_engine` static library → PUBLIC `common`, `node_graph_engine`; exposed as `simulator::coax_cable_engine`.
- `coax_cable_widget` static library → PUBLIC `core`, `coax_cable_engine`, `logging_widget`; exposed as `simulator::coax_cable_widget`.
- Both added to the top-level `CMakeLists.txt` and linked into the `app` target.
- No new third-party dependencies (ImGui, ImPlot, GLFW, Catch2 already in the project).

## 5. Data model

### `CableSpec` (header-only value type)

```cpp
// coax_presets.h
#pragma once
#include <array>

struct CableSpec {
    const char* name;          // e.g. "MT 340"
    double K1_dB_per_m;        // sqrt(f) coefficient, dB/m with f in MHz
    double K2_dB_per_m;        // f coefficient, dB/m with f in MHz
    double delay_ns_per_m;     // signal propagation delay
    double max_freq_GHz;       // upper limit of the datasheet model
    double diameter_mm;        // informational; for widget display
};

inline const std::array<CableSpec, 6> kCoaxCablePresets = {{
    // name,      K1,        K2,        delay, max_f,  diam
    {"MT 210",  0.0,       0.0,       0.0,   0.0,    0.0},   // TODO: populate from MT 210 datasheet
    {"MT 230",  0.0,       0.0,       0.0,   0.0,    0.0},   // TODO: populate from MT 230 datasheet
    {"MT 265",  0.0,       0.0,       0.0,   0.0,    0.0},   // TODO: populate from MT 265 datasheet
    {"MT 300",  0.0,       0.0,       0.0,   0.0,    0.0},   // TODO: populate from MT 300 datasheet
    {"MT 340",  0.004710,  0.000004,  0.4,   18.5,   8.6},
    {"MT 480",  0.0,       0.0,       0.0,   0.0,    0.0},   // TODO: populate from MT 480 datasheet
}};
```

Zero-K entries produce zero cable loss — selecting a stub preset is harmless until the constants are filled in from each cable's individual datasheet.

### `CoaxCableEngine` members

```cpp
class CoaxCableEngine : public IComponentEngine {
    int m_id;
    int m_graph_node_id = -1;
    NodeGraphEngine* m_graph = nullptr;

    SignalNode m_node;             // 1 input, 1 output
    int m_preset_index = 4;        // defaults to MT 340 (index 4 in kCoaxCablePresets)
    double m_length_m = 1.0;       // defaults to 1 m
    double m_connectors_loss_dB = 0.0;
    bool m_dirty = true;
    const Spectrum* m_cached_input_ptr = nullptr;
    uint64_t m_cached_input_generation = 0;
};
```

Setters (`setPresetIndex`, `setLengthM`, `setConnectorsLossDB`) flip `m_dirty` on actual change. `setLengthM` clamps to `[0, 1000]` to prevent negative or runaway lengths. The other setters do not clamp — negative connector loss is permitted (the same rationale as amplifier's negative gain: a user can model a "cable + booster" combination).

## 6. Processing model — `update()`

Same gating as `AmplifierEngine::update`: skip recomputation if not dirty **and** the input spectrum pointer and generation are unchanged.

1. **Output frequency grid**: copy from input. If input is null or has fewer than two frequency entries, build the default 10 MHz grid via `buildDefaultFrequencyGrid`.

2. **Per-tone (for each `t` in input tones)**:
   ```
   f_Hz       = std::clamp(std::abs(t.freq_Hz), 1.0, preset.max_freq_GHz * 1e9)
   f_MHz      = f_Hz / 1e6
   loss_dB    = (preset.K1 * std::sqrt(f_MHz) + preset.K2 * f_MHz) * m_length_m
                + m_connectors_loss_dB
   phase_shift = -360.0 * (f_Hz / 1e9) * m_length_m * preset.delay_ns_per_m * 1e-3
   out_tones[i].power_dBm = t.power_dBm - loss_dB
   out_tones[i].phase_deg = t.phase_deg + phase_shift
   ```
   If `std::abs(t.freq_Hz)` exceeded `preset.max_freq_GHz`, emit a single `LOG_WARN`. The engine keeps a `bool m_warned_above_max` that is set to `true` when a tone first exceeds the cap during an `update()` call, and is cleared to `false` on the next `setPresetIndex()`. This guarantees at most one warning per "preset selection + at least one out-of-band tone" event.

3. **Per-bin phase (for each `i` in frequencies)**:
   ```
   f_Hz  = std::clamp(std::abs(freqs[i]), 1.0, preset.max_freq_GHz * 1e9)
   shift = -360.0 * (f_Hz / 1e9) * m_length_m * preset.delay_ns_per_m * 1e-3
   out.phase_deg[i] = in.phase_deg[i] + shift
   ```
   (Initialise `out.phase_deg` from `in.phase_deg` first; if input phase is empty, fill with zeros.)

4. **Per-bin noise (for each `i` in frequencies)**:
   ```
   f_Hz       = std::clamp(std::abs(freqs[i]), 1.0, preset.max_freq_GHz * 1e9)
   f_MHz      = f_Hz / 1e6
   loss_dB    = (preset.K1 * std::sqrt(f_MHz) + preset.K2 * f_MHz) * m_length_m
                + m_connectors_loss_dB
   L_linear   = dbToLinear(loss_dB)
   out.noise_W[i]       = in.noise_total_W[i] / L_linear
   out.noise_added_W[i] = 0.0
   out.noise_total_W[i] = out.noise_W[i]
   ```
   `noise_added_W` is zero because fidelity B does not model cable thermal noise (see §2).

5. `bumpGeneration()` on the output spectrum.

### Edge cases

- `length_m == 0`: cable term vanishes; only `m_connectors_loss_dB` contributes.
- `m_preset_index` points to a zero-K stub: loss is identically zero for all tones/bins.
- `m_node.inputs[0] == nullptr`: produce an empty-tone Spectrum on the default grid, with `bumpGeneration()` called.
- `t.freq_Hz` outside `[1 Hz, preset.max_freq_GHz]`: clamp, log warn, continue.

## 7. Widget

`CoaxCableWidget::draw("Coax Cable N")` renders a single ImGui panel per cable component:

| Control | Widget | Range / behaviour |
|---|---|---|
| Preset | `ImGui::Combo` | Iterates `kCoaxCablePresets`; tooltip lists K1, K2, max freq, delay. |
| Length (m) | `utils::inputDouble` | `[0, 1000]` |
| Connector loss (dB) | `utils::inputDouble` | unbounded |
| Loss @ fc | text | computed read-out at the input spectrum's centre frequency `(freqs.front() + freqs.back()) / 2`, or `—` if input frequencies is empty |
| Phase shift @ fc | text | same source; `—` if input frequencies is empty |
| Max freq, Delay | text | read-only summary of the selected preset |

The widget writes only via the engine's setters, so dirty-flag propagation is automatic.

## 8. App integration

`RfSimulatorApp`:

- `addCoaxCable()` — constructs a new `CoaxCableEngine` (auto-increment id), constructs a `CoaxCableWidget` bound to it, stores both in the app's component vectors. The engine's constructor calls `graph.addNode("Coax Cable N", &m_node, 1, 1)`.
- `removeComponent(id)` is already generic; no change needed.
- `update_dsp()` — extend the existing per-component loop to also iterate the cable engine vector. The loop is currently component-type-agnostic in the section that bumps `view_enabled` and updates the spectrum analyzer; only the section that calls `engine->update(dt)` needs an extra `for` over the cable vector (mirroring the existing amplifier loop).
- `draw_ui()` — extra `for` over the cable widget vector, after amplifiers, before splitters, to keep the on-screen order consistent with the add-component menu.

`NodeGraphWidget`'s add-component menu gains one entry, **"Coax Cable"**, between **"Amplifier"** and **"Splitter"**. Clicking it calls `app->addCoaxCable()`.

The engine's `hoverSummary()` returns:

```
"Cable: <preset.name> | L=<m_length_m> m | Loss@fc=<computed dB> dB"
```

This automatically surfaces in the existing per-pin tooltip of `NodeGraphWidget`.

## 9. Testing

New file `tests/coax_cable_engine_tests.cpp`, registered in `tests/CMakeLists.txt`. Catch2 v3, mirroring the existing amplifier tests.

### Datasheet spot-checks (MT 340, L = 100 m, connectors = 0)

| f | Expected IL | Tolerance |
|---|---|---|
| 500 MHz | 10.7 dB | ±0.1 dB |
| 2 GHz | 21.9 dB | ±0.1 dB |
| 6 GHz | 39.6 dB | ±0.1 dB |
| 10 GHz | 51.4 dB | ±0.1 dB |
| 18 GHz | 70.9 dB | ±0.1 dB |

### Behavioural coverage

- **Length scaling** — doubling `m_length_m` doubles per-tone loss.
- **Connector loss additive** — `m_connectors_loss_dB = 0.5` adds 0.5 dB independent of frequency.
- **Per-tone power reduction** — input −50 dBm through 10 dB cable → output −60 dBm.
- **Per-bin noise scaling** — `out.noise_W[i] = in.noise_total_W[i] / 10^(loss/10)`.
- **Phase shift** — at 1 GHz, 1 m, MT 340 (delay 0.4 ns/m) → `phase_deg` decreases by `0.144°` (exact arithmetic; `Catch::Approx` with 1e-9 tolerance).
- **Length = 0** — cable term zero, only connector loss remains.
- **Above max_freq** — a tone at 25 GHz with MT 340 (max 18.5 GHz) is clamped to 18.5 GHz value; no crash.
- **Empty input** — produces a fresh default-grid Spectrum, empty tones, zero noise, `bumpGeneration()` called.
- **Zero-K preset** — switching `m_preset_index` to a stub preset produces zero cable loss.
- **Dirty flag** — two consecutive `update()` calls with no setter call and unchanged input → second call is a no-op (output `generation` unchanged).
- **Generation bump** — after a setter call, `update()` increments output `generation`.

### Integration smoke test

One optional test: feed a single tone at 2 GHz, −40 dBm through a 2 m MT 340 cable and a 10 dB amplifier, assert output tone power matches `−30.44 dBm` (within ±0.05 dB). The math: at 2 GHz, MT 340 loss density is `K1·√2000 + K2·2000 = 0.2187 dB/m`; at 2 m that is `0.437 dB`; total `−40 − 0.437 + 10 = −30.437 dBm`. This exercises `SignalNode` and `NodeGraphEngine` wiring.

## 10. Risks & follow-ups

- **Stub presets**: the MT 210/230/265/300/480 entries ship with zero K-values. Selecting one in the UI will silently pass the signal through unchanged. A tooltip note in the widget makes this clear; filling the values in requires fetching each cable's individual datasheet (out of scope for this design).
- **Fidelity C (thermal noise)**: deferred. The `noise_added_W` field is already wired through the engine; the addition is one extra line in `update()` and one new engine member `m_cable_temp_K` (default 290).
- **Fidelity D (VSWR / S11 / S22)**: out of scope; would require modelling return loss and reflected power, which the current `SignalNode` model doesn't carry.
- **Frequency extrapolation**: above `max_freq_GHz` the loss is clamped to the value at `max_freq_GHz`, which underestimates real-world loss (the √f curve is still rising). A future enhancement could extrapolate with a one-time warn-then-allow toggle.
