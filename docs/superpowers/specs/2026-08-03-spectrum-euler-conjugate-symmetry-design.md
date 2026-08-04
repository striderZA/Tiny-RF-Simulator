# Real-Domain Spectrum: Euler / Conjugate-Symmetry Fix

**Issue:** https://github.com/striderZA/Tiny-RF-Simulator/issues/39

**Bug:** In the "real" (analog) signal domain — everywhere upstream of the DDC inside the ADC —
`Spectrum::Tone` stores a single positive-frequency entry per tone holding the full real-signal
power. A real cosine at `fc` is physically `0.5*(exp(j*2*pi*fc*t) + exp(-j*2*pi*fc*t))`: equal-power
spectral content at `+fc` AND `-fc`, each at half the total power. The current model/display
collapses this to one full-power bin at `+fc` with no representation of the mirrored `-fc`
component, and the ADC's tone-mapping/aliasing math is oblivious to the mirror entirely.

## Scope

Fix reaches the actual physics (not just the picture): the ADC's DDC output power bookkeeping
becomes conjugate-symmetry-aware (accounting for the real input's ±fc power split), and the
real-domain spectrum render path shows the correct ±fc/half-power pair. It deliberately does
**not** touch the interior DSP math (signal generator, `nonlinear_model.h` harmonic/IM math,
gain/S-parameter/filter stages, mixer) — those keep computing on today's single
collapsed-power-per-tone representation. Reason: `nonlinear_model.h`'s harmonic/IM formulas
(`Vp1 = dbmToV(Pout_dBm)`, then `k1*Vp1`, `k2*Vp1^3`, ...) are nonlinear in amplitude and
calibrated against the full real-tone power. Splitting every tone into two half-power entries at
every creation site and letting a generic "for each tone, compute harmonics" loop run over both
halves independently would NOT algebraically recombine to correct total harmonic power — it would
silently corrupt every amplifier's distortion output. So the fix applies conjugate-symmetric
expansion only at the two points where ±fc content is physically meaningful: rendering, and the
ADC's DDC power accounting.

## Architecture

### 1. Data model (`common/spectrum.h`)

- Add `bool is_complex_baseband = false;` to `Spectrum`. Default `false` (real domain). Set `true`
  only by `AdcEngine`'s output; propagated downstream from there exactly like `fs_Hz` already is.
- Add a shared helper (free function or `Spectrum` method), e.g.:
  ```cpp
  std::vector<Spectrum::Tone> conjugateSymmetricExpand(const std::vector<Spectrum::Tone> &tones);
  ```
  For each tone with `freq_Hz != 0`: emit `(+f, P - 3.0103 dB, phase)` and `(-f, P - 3.0103 dB,
  phase)`, where `3.0103 dB = 10*log10(2)` (half linear power per mirror). For `freq_Hz == 0` (DC):
  emit the tone unchanged — a DC component is self-conjugate, `exp(j*0) == exp(-j*0)`, no mirror,
  no power split.

### 2. Interior DSP — unchanged

`signal_generator`, `nonlinear_model.h`, `mixer`, and all gain/S-parameter/filter pass-through
components keep operating on `Spectrum::tones` exactly as today: single collapsed-power entry per
real tone, positive frequency only (harmonics/IM products included). Zero behavior change, zero
regression risk to distortion math.

### 3. Flag propagation

Every producer gets one new line next to its existing `out.fs_Hz = ...` assignment (same locations,
both early-return and main-path branches where applicable):

```cpp
out.is_complex_baseband = in_ptr ? in_ptr->is_complex_baseband : false;
```

Combiner (two inputs) ORs both: `out.is_complex_baseband = (in0 && in0->is_complex_baseband) ||
(in1 && in1->is_complex_baseband);`. Files touched: `amplifier`, `attenuator`, `combiner`,
`equalizer`, `ideal_filter`, `coax`, `splitter`, `touchstone/src/s_parameter_data.cpp`, `mixer`,
`pfb_channelizer`. `signal_generator` explicitly sets `false`. This mirrors the existing `fs_Hz`
propagation pattern already present in each of these files — mechanical, low-risk, easy to review.

### 4. Render expansion point (`spectrum_analyzer/src/spectrum_analyzer_engine.cpp`)

`integratePowerPerBin` calls `conjugateSymmetricExpand(spec.tones)` in place of `spec.tones` only
when `!spec.is_complex_baseband`, before binning power into display bins. Post-ADC spectra
(`is_complex_baseband == true`) render as today — already correctly one-sided.

### 5. ADC/DDC — flag only, no power-math change

Cross-checked against `docs/resources/rf_adc_info.md` (the project's own authoritative ADC/DDC
model doc), and re-derived the power arithmetic concretely (P = −20 dBm, Fs = 1 GHz, fc = 250 MHz)
before finalizing this section — both passes changed the design from earlier drafts.

`rf_adc_info.md` §3 ("Image Rejection") and validation-checklist item 6 confirm: because the ADC
input is real, the complex DDC mixer produces a wanted component and an unwanted image at the
mirror frequency, and **the lowpass filter after mixing suppresses the image** — no explicit
image-reject logic is needed. `alias_frequency` is deliberately unsigned/sign-blind (folds into
`[0, Fs/2)`) because real Nyquist folding genuinely cannot distinguish `+fc` from `-fc` — that
ambiguity is resolved afterward by the fixed `-Fs/4` NCO shift + `[-Fs/4, Fs/4)` window already in
the code: `f_a - Fs/4` always lands inside that window for `f_a in [0, Fs/2)`, while the mirror
candidate `-f_a - Fs/4` always lands at or below `-Fs/4` (outside, touching the boundary only at
`f_a = 0`). So the existing single-branch tone loop already deterministically selects the correct
surviving image and rejects the other — no change to `alias_frequency` or the aliasing/NCO-shift
math is needed.

**Power math needs no change either — this is the correction from the previous draft.** Splitting
a real tone of power `P` into ±fc conjugate pairs puts half the linear power (`P - 3.0103 dB`) on
each mirror; only the surviving image carries that `P - 3.0103 dB` through the DDC. To match
today's ADC convention (`power_dBm` numerically unchanged end-to-end), that surviving image needs
`+3.0103 dB` of DDC gain compensation — and `-3.0103 dB` (split) `+ 3.0103 dB` (compensation) = 0
dB net. Since interior tones are never actually split in code (§2 — they stay at full `P` the
whole way to the ADC's input), applying that net-zero result means: **leave `t.power_dBm` exactly
as copied from the input tone.** That is precisely what `adc_engine.cpp` already does today
(`Spectrum::Tone t = tone;`, no power adjustment). Adding an explicit `+3.0103 dB` on top of the
already-unsplit input power (as an earlier draft of this doc proposed) would double-apply the
compensation and push ADC output 3 dB hot, breaking the "ADC DDC preserves tone power and phase"
regression test. **The only required change in `adc_engine.cpp` is setting
`out.is_complex_baseband = true;`** on both the empty-input early-return path and the main path.

**Explicitly out of scope:** `docs/resources/rf_adc_info.md` §"Zone Inversion (Spectral Flip)"
documents that even Nyquist zones need a spectral-inversion correction (NCO sign flip or output
conjugation) that the current `adc_engine.cpp` does not implement. That's a pre-existing gap
unrelated to Euler/conjugate-symmetry and is not touched by this fix.

## Files changed

- `common/spectrum.h` — `is_complex_baseband` field, `conjugateSymmetricExpand` helper
- `signal_generator/src/signal_generator_engine.cpp` — explicit `false` flag set
- `amplifier/src/amplifier_engine.cpp` — flag propagation
- `attenuator/src/attenuator_engine.cpp` — flag propagation
- `combiner/src/combiner_engine.cpp` — flag propagation (OR of two inputs)
- `equalizer/src/equalizer_engine.cpp` — flag propagation
- `ideal_filter/src/ideal_filter_engine.cpp` — flag propagation
- `coax/src/coax_cable_engine.cpp` — flag propagation
- `splitter/src/splitter_engine.cpp` — flag propagation
- `touchstone/src/s_parameter_data.cpp` — flag propagation
- `mixer/src/mixer_engine.cpp` — flag propagation
- `pfb_channelizer/src/pfb_channelizer_engine.cpp` — flag propagation (true, inherited from ADC)
- `spectrum_analyzer/src/spectrum_analyzer_engine.cpp` — render-time expansion in
  `integratePowerPerBin`
- `adc/src/adc_engine.cpp` — `is_complex_baseband = true` on both output paths; no change to
  `alias_frequency`, the NCO-shift/windowing math, or tone `power_dBm` (see §5 for why)
- `tests/*` — new coverage (see Testing)

## Testing

- New unit tests for `conjugateSymmetricExpand`: verifies mirror power split (`P - 3.0103 dB` each
  side), DC exemption (no mirror for `freq_Hz == 0`), phase preserved on both mirrors.
- Spectrum-analyzer render test: real-domain spectrum (`is_complex_baseband == false`) with a tone
  at `fc` renders two peaks at `±fc`, each `~3.0103 dB` below the tone's nominal `power_dBm`.
  Post-ADC spectrum (`is_complex_baseband == true`) with the same tone renders unchanged (single
  peak, no mirroring).
- `adc_engine` regression test: existing "ADC DDC preserves tone power and phase" continues to
  pass with unchanged expected values (proves the ADC's already-unmodified power math remains
  correct), plus a new assertion that `out.is_complex_baseband == true` on that output.
- Full existing suite (`build/bin/tests.exe`) run to confirm no regressions in nonlinear model,
  S-parameter interpolation, or pass-through gain/filter tests — none of that interior math changes.
