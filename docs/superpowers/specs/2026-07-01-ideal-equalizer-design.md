# Ideal Equalizer — Design Spec

**Date:** 2026-07-01
**Status:** Approved (pending review of this document)
**Branch:** `feat/ideal-equalizer`

---

## 1. Goal

Add a new 1-in/1-out signal-chain component — an **idealized equalizer** — that applies a frequency-dependent loss defined by a textbook log-frequency model: a single anchor loss at DC plus a linear dB-per-decade slope. This gives the user a free-form shaping element without coupling to coax-specific (K1·√f + K2·f) semantics.

## 2. Scope

### In scope (v1)

- New `equalizer/` module with `EqualizerEngine` only (no standalone widget; settings live in the existing `InspectorPanel`, mirroring `IdealFilterEngine`).
- Two free parameters: `L_DC_dB` (loss at DC, default 0 dB) and `slope_dB_per_decade` (default 0 dB/decade).
- Closed-form response: `L(f) = L_DC + slope * log10(max(|f|, 1 Hz))`.
- Per-tone and per-bin application; input noise scaled by `dbToLinear(-L(f))`.
- Zero phase shift, zero added noise (passive, magnitude-only, like a lossless-of-noise ideal attenuator).
- Node-graph integration: new "Equalizer" entry in the add-component menu.
- ~8 unit tests in `[equalizer]` tag.

### Out of scope (deferred)

- **Noise figure (NF)**. Adding NF is one engine field + one line in `update()`. Not needed for the "idealized textbook" framing the user asked for.
- **Phase response**. Equalizer is magnitude-only. If a min-phase / linear-phase model is wanted later, it can plug into the existing `phase_deg` channel.
- **Piecewise / multi-segment slopes** (low-band slope, break frequency, high-band slope). Single monotonic slope across the whole spectrum is v1.
- **Cable-matched presets** (auto-compute `L_DC` + `slope` from a `CableSpec` to invert a coax). One follow-up method on the engine; not in v1.
- **Length units, multi-tap, FIR-style equalization, S-parameter de-embedding.**

## 3. Architecture

### Module layout

```
equalizer/
├── CMakeLists.txt
├── include/
│   └── equalizer_engine.h       # IComponentEngine impl
└── src/
    └── equalizer_engine.cpp
```

Mirrors `ideal_filter/` (engine-only, inspector integration). No ImGui dependencies in the engine.

### CMake targets

- `equalizer_engine` static library → PUBLIC `common`, `node_graph_engine`; exposed as `simulator::equalizer_engine`.
- Added to top-level `CMakeLists.txt` and linked into the `app` target.
- No new third-party dependencies.

## 4. Data model

```cpp
// equalizer/include/equalizer_engine.h
#pragma once
#include "common.h"
#include "component_interface.h"
#include "node_graph_engine.h"
#include "signal_node.h"

class EqualizerEngine : public IComponentEngine {
  public:
    EqualizerEngine(int id, NodeGraphEngine& graph);

    int id() const override { return m_id; }
    int graphNodeId() const override { return m_graph_node_id; }
    int inputPinId() const override;
    int outputPinId() const override;
    std::string hoverSummary() const override;
    SignalNode& node() override { return m_node; }
    const SignalNode& node() const override { return m_node; }
    void update(double dt) override;

    void setLossAtDC(double dB);
    void setSlope(double dB_per_decade);
    double lossAtDC() const { return m_loss_at_DC_dB; }
    double slope() const { return m_slope_dB_per_decade; }

  private:
    int m_id;
    int m_graph_node_id = -1;
    NodeGraphEngine* m_graph = nullptr;
    SignalNode m_node;                        // 1 input, 1 output
    double m_loss_at_DC_dB = 0.0;
    double m_slope_dB_per_decade = 0.0;
    bool m_dirty = true;
    const Spectrum* m_cached_input_ptr = nullptr;
    uint64_t m_cached_input_generation = 0;

    double lossAt(double freq_Hz) const;       // L(f) helper
};
```

Setters flip `m_dirty` on actual change. No clamping — `L_DC` may be negative (a gain), slope may be negative (pre-emphasis).

## 5. Processing model — `update()`

Same gating as `IdealFilterEngine::update` and `AmplifierEngine::update`: skip recomputation if not dirty **and** the input spectrum pointer and generation are unchanged.

1. **Output frequency grid**: copy from input. If input is null or has fewer than two frequency entries, build the default 10 MHz grid via `buildDefaultFrequencyGrid`.

2. **Per-tone (for each `t` in input tones)**:
   ```
   loss_dB = lossAt(t.freq_Hz)        // see below
   out_tones[i].power_dBm = t.power_dBm - loss_dB
   out_tones[i].phase_deg  = t.phase_deg     // unchanged (pass-through)
   ```
   `lossAt` uses `std::abs(t.freq_Hz)`. Negative-frequency tones (mixer products) are equalized by their magnitude.

3. **Per-bin phase**: copy `in.phase_deg` into `out.phase_deg`; if input is empty, fill with zeros.

4. **Per-bin noise (for each `i` in frequencies)**:
   ```
   f_Hz     = std::abs(freqs[i])
   loss_dB  = lossAt(f_Hz)
   L_lin    = dbToLinear(loss_dB)
   out.noise_W[i]       = (in.noise_total_W[i]) / L_lin
   out.noise_added_W[i] = 0.0
   out.noise_total_W[i] = out.noise_W[i]
   ```
   `noise_added_W` is zero (passive equalizer, no NF).

5. `out.fs_Hz = in ? in->fs_Hz : 0.0;` and `bumpGeneration()`.

### `lossAt(f)` helper

```cpp
double EqualizerEngine::lossAt(double freq_Hz) const {
    double f = std::abs(freq_Hz);
    if (f < 1.0) f = 1.0;                  // floor to avoid log10(0)
    return m_loss_at_DC_dB + m_slope_dB_per_decade * std::log10(f);
}
```

Behaviour at the boundaries:
- `f = 0` or `|f| < 1 Hz` → `loss = L_DC` (the anchor).
- `f = 1 Hz` → `loss = L_DC` (1 decade of headroom for the floor).
- `f = 10 Hz` → `loss = L_DC + slope`.
- `f = 1 GHz` → `loss = L_DC + 9 * slope`.

### Edge cases

- `slope = 0`, `L_DC = 0` → identity; no change to input.
- `slope = 0`, `L_DC != 0` → uniform loss/gain across all tones and bins.
- `m_node.inputs[0] == nullptr` → empty tones on the default grid, zero noise, `bumpGeneration()` called.
- Tones at `freq_Hz = 0` (theoretical) → loss clamped to `L_DC`.

## 6. UI integration (inspector panel only)

`InspectorPanel::drawEqualizerProperties(EqualizerEngine&, int)` renders:

| Control | Widget | Behaviour |
|---|---|---|
| `Measure` | `ImGui::Checkbox` | toggles `m_node.view_enabled` |
| `L@DC (dB)` | `utils::inputDouble` | free range, default 0.0 |
| `Slope (dB/decade)` | `utils::inputDouble` | free range, default 0.0 |

Optional read-only summary: `Loss at 1 GHz = L_DC + 9 * slope` dB (computed live from current values). No edit; for sanity-checking. One `ImGui::Text` line.

`hoverSummary()` returns:

```
"Equalizer | L@DC=0.0 dB, slope=0.0 dB/dec"
```

This surfaces in the existing per-pin tooltip of `NodeGraphWidget`.

## 7. App integration

`RfSimulatorApp`:

- `addEqualizer()` — constructs a new `EqualizerEngine` (auto-increment id), stores it in the existing `m_components` vector. No separate widget instance (inspector handles UI).
- `removeComponent(id)` is already generic; no change needed.
- `update_dsp()` — extend the existing per-component `update(dt)` loop to also iterate the equalizer vector. (Mirror the existing amplifier / cable / filter loops.)
- `draw_ui()` — no new top-level window; the equalizer is configured via the inspector when its node is selected.

`NodeGraphWidget`'s add-component menu gains one entry, **"Equalizer"**, between **"Ideal Filter"** and **"Coax Cable"**. Clicking it calls `app->addEqualizer()`.

`InspectorPanel::ComponentType` enum gets a new entry, `Equalizer`. The dispatch in `inspectForNode()` and the title in `componentTitle()` get one line each. The draw switch in `drawForNode()` gets one new `case ComponentType::Equalizer:`.

## 8. Testing

New file `tests/test_equalizer_engine.cpp`, registered in `tests/CMakeLists.txt`. Catch2 v3, `[equalizer]` tag.

### Behavioural coverage

- **Identity** — `L_DC=0`, `slope=0` → output tones match input tones (power, frequency, phase).
- **Pure DC loss** — `L_DC=+3 dB`, `slope=0` → all tones and noise reduced by 3 dB (linear factor 0.5). `L_DC` follows the codebase's "positive = attenuation" convention (matches `CoaxCableEngine` / `AmplifierEngine`); `L_DC = −3 dB` is 3 dB of gain, not 3 dB of loss.
- **Pure slope** — `L_DC=0`, `slope=2 dB/decade` → loss at 1 Hz = 0 dB (tone unchanged), loss at 10 Hz = 2 dB, loss at 1 GHz = 18 dB, loss at 10 GHz = 20 dB. Each test asserts the output tone power = input tone power − the computed loss.
- **Negative slope (pre-emphasis)** — `L_DC=0`, `slope=−2 dB/decade` → loss at 1 GHz = −18 dB (i.e. +18 dB gain); output tone power exceeds input by 18 dB. Linear noise factor is the reciprocal of the positive-slope case (gain instead of attenuation).
- **DC floor** — tone at `freq_Hz = 0` (synthesised in test) → loss = `L_DC` exactly.
- **Sub-1 Hz floor** — tone at `freq_Hz = 0.5` → loss = `L_DC` (matches 1 Hz case).
- **Phase passthrough** — `phase_deg` of each output tone equals `phase_deg` of corresponding input tone (no shift added by the equalizer).
- **fs_Hz passthrough** — output `fs_Hz` equals input `fs_Hz`.
- **Noise scaling** — for a tone in noise, `out.noise_W[i] = in.noise_total_W[i] / 10^(L(f)/10)`.
- **Empty input** — produces fresh default-grid Spectrum, empty tones, zero noise, `bumpGeneration()` called.
- **Dirty flag** — two consecutive `update()` calls with no setter call and unchanged input → second call is a no-op (output `generation` unchanged).
- **Generation bump** — after a `setSlope` or `setLossAtDC`, `update()` increments output `generation`.

### Integration smoke test

One test: feed a 2 GHz tone at −40 dBm through an equalizer with `L_DC=0`, `slope=4 dB/decade`. Loss at 2 GHz = `4 * log10(2e9) ≈ 4 * 9.301 = 37.2 dB`. Output tone power ≈ −77.2 dBm (`Catch::Approx`, ±0.1 dB). Verifies end-to-end tone scaling on a realistic magnitude.

## 9. Risks & follow-ups

- **Single-slope limitation**: piecewise slopes are not modelled. If users regularly need "boost 1–100 MHz, flat above", a `std::vector<SlopeSegment>` is the upgrade path. YAGNI for v1.
- **No noise figure**: a real equalizer is passive but at non-ambient temperature it adds noise. The `noise_added_W` field is already wired; adding NF or cable-temperature fidelity is one follow-up.
- **No phase response**: users needing a Hilbert-transform-coupled min-phase equalizer will want a phase channel. Same upgrade path as the coax phase shift.

## 10. Documentation updates

- New `equalizer/AGENTS.md` (Purpose / Ownership / Work Guidance / Verification / Child DOX Index).
- Root `AGENTS.md` Child DOX Index: replace `equalizer/AGENTS.md — *(pending)* Equalizer engine + widget` with the new file (note: engine-only, no widget).
