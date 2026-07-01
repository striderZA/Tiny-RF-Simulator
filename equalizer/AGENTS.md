# equalizer — AGENTS.md

## Purpose

Own the `EqualizerEngine`: a 1-in/1-out DSP engine that applies a frequency-dependent loss defined by `L(f) = L_DC + slope_dB_per_decade * log10(max(|f|, 1 Hz))`. Engine-only (no widget); configuration lives in the existing `InspectorPanel`.

## Ownership

- `equalizer/CMakeLists.txt` — `equalizer_engine` static library, exposed as `simulator::equalizer_engine`.
- `equalizer/include/equalizer_engine.h` — class declaration.
- `equalizer/src/equalizer_engine.cpp` — implementation; public `lossAt(f)` helper applies the DC floor.
- `equalizer/AGENTS.md` — this file.

## Local Contracts

- Two free parameters: `L_DC_dB` (loss at DC, default 0 dB) and `slope_dB_per_decade` (default 0 dB/decade). Both unbounded; no clamping.
- Positive slope = more loss at high f (cable-like). Negative slope = pre-emphasis. Zero slope + zero `L_DC` = identity.
- Phase: zero shift. Noise: passive, no NF (`noise_added_W` always zero).
- Setters flip `m_dirty` on actual change; `update()` short-circuits when neither dirty flag nor upstream spectrum changed.
- `|f| < 1 Hz` is clamped to the 1 Hz floor to avoid `log10(0)`.

## Work Guidance

- When extending the equalizer with a noise figure, follow the `AmplifierEngine` pattern: add `m_noise_figure_dB` + `addedNoiseDensity_W_per_Hz(...)` in `update()`. The `noise_added_W` channel is already wired.
- When extending to piecewise slopes, replace `m_slope_dB_per_decade` with a `std::vector<SlopeSegment>` and update `lossAt` to walk the segments.
- Inspector is the only UI surface. If a standalone widget is needed, follow the `coax/` pattern (`equalizer_widget.h/cpp`) and link `simulator::equalizer_widget` into `app`.

## Verification

- `cmake --build build && ctest --test-dir build -R Equalizer` must pass with all 14 `[equalizer]`-tagged tests.
- Manual smoke: launch the app, add a Generator (e.g. 1 GHz, −20 dBm), add an Equalizer (`L_DC=0`, `slope=2 dB/decade`), wire generator → equalizer → spectrum analyzer, verify the probed tone reads ≈ −38 dBm (20 dB loss at 1 GHz from the slope).

## Child DOX Index

No child docs. `equalizer/` is a flat directory.
