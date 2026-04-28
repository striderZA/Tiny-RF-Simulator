# Design: Decouple Bin Width from Noise — Spectral Density Model

**Date:** 2026-04-28
**Status:** Approved

## 1. Goal

Make the signal generator a clean ideal source (tone + thermal noise floor of -174 dBm/Hz) and ensure the spectrum analyzer's displayed noise floor depends only on:
- The resolution bandwidth (RBW) of the analyzer
- The cumulative gain and noise figure of components *after* the generator

The internal frequency grid spacing must no longer affect the displayed noise power.

## 2. Background

Currently, `Spectrum::noise_W`, `noise_added_W`, and `noise_total_W` store **total noise power per internal bin** (W). The per-bin noise is calculated as `k * T * bin_width` for the generator, and `k * Te * G * bin_width` for amplifiers. This means the displayed noise floor changes when the generator's frequency step changes, which is physically incorrect — the analyzer's noise floor should depend on its RBW, not the internal simulation grid.

## 3. Proposed Changes

### 3.1 Data Model: Noise as Spectral Density

The three noise vectors in `Spectrum` change meaning from **W per internal bin** to **W/Hz** (power spectral density).

- `noise_W[i]` — input noise density at frequency `i`
- `noise_added_W[i]` — noise density added by this component
- `noise_total_W[i]` — total output noise density (`noise_W + noise_added_W`)

Tones remain discrete impulses with power in dBm. The internal frequency grid is only used to place frequency sample points and tone bins.

Remove `Spectrum::thermalNoisePower_W(double bin_width)` entirely.

`Spectrum::computeTotalNoise()` stays structurally identical: `noise_total_W[i] = noise_W[i] + noise_added_W[i]`.

### 3.2 Signal Generator: Clean Ideal Source

Remove `m_gain_dB`, `m_nf_dB`, and all related getters/setters from `SignalGeneratorEngine`.

**`rebuildFrequencyGrid()`:**
```cpp
m_node.input.noise_total_W.assign(n, k * T);  // flat thermal noise density
```

**`update()`:**
- Output the active tone at its configured `power_dBm`.
- Pass input noise density straight through (unity gain): `out.noise_W = in.noise_total_W`.
- No added noise: `out.noise_added_W.assign(N, 0.0)`.
- `out.noise_total_W = out.noise_W + out.noise_added_W`.

The generator widget loses its gain and NF input fields.

### 3.3 Amplifier: Density Scaling + Added Noise

Keep `m_gain_dB` and `m_nf_dB`.

In `update()`:
- Scale input noise density by linear gain: `out.noise_W[i] = G * in.noise_total_W[i]`.
- Add amplifier's own noise density:
  ```cpp
  double added_density = k * Te * G;  // W/Hz
  ```
  where `Te = T * (F - 1)` and `F = 10^(nf_dB / 10)`.
- `out.noise_total_W[i] = out.noise_W[i] + out.noise_added_W[i]`.

Add a new helper in `common.h`:
```cpp
inline double addedNoiseDensity_W_per_Hz(double nf_dB, double gain_linear) {
    double Te = calculateNoiseTemp(nf_dB);
    return k * Te * gain_linear;
}
```

Deprecate `addedNoisePerBin_W`.

### 3.4 Spectrum Analyzer: Integrate Density Over RBW

**`integratePowerPerBin(const Spectrum& spec)`:**

For each frequency point:
1. Convert noise density to per-bin power using the grid spacing:
   ```cpp
   double bin_width = spec.frequencies[1] - spec.frequencies[0];
   power_W[i] = spec.noise_total_W[i] * bin_width;
   ```
2. Add tone power (discrete impulse) to the appropriate bin.

**`applyRBW(const std::vector<double>& power_W, double binWidth)`:**

Change the Gaussian kernel so it **peaks at 1** instead of being normalized to sum = 1.

- For a tone (discrete impulse), convolution with a kernel peaking at 1 preserves peak power.
- For noise, the convolution integrates per-bin power across the filter shape. Since per-bin power = density × bin_width, the result is proportional to `density × RBW` and is **independent of the internal bin width**.

**`renderSpectrum` and `renderCombinedSpectrum`** remain structurally unchanged.

### 3.5 UI Changes

- **Signal Generator Widget:** Remove gain (dB) and NF (dB) input fields. Keep frequency, power, and frequency step.
- **Amplifier Widget:** No changes.

### 3.6 Test Impact

- Update `Spectrum::computeTotalNoise` test: expected values are now densities (no bin-width factor).
- Update any tests checking generator output noise: expect `k * T` density instead of `k * T * delta_f`.
- Update amplifier added-noise tests: use `addedNoiseDensity_W_per_Hz`.
- Add test verifying that displayed noise floor depends on RBW, not internal grid spacing.

## 4. Architecture Diagram

```
SignalGeneratorEngine          AmplifierEngine            SpectrumAnalyzerEngine
---------------------          ---------------            ----------------------
tone (freq, power_dBm)  -->    gain + NF              -->   RBW integration
noise density: k*T (W/Hz) -->  noise_W = G * nin         noise_total_W * bin_width
                               noise_added = k*Te*G       = density * RBW (independent
                               noise_total = sum            of internal grid)
```

## 5. Success Criteria

1. Signal generator shows only frequency, power, and frequency step controls — no gain or NF.
2. Generator output noise density is flat at `k * T` W/Hz (~4.0e-21 W/Hz).
3. Amplifier output noise density scales correctly with gain and NF.
4. Spectrum analyzer noise floor changes when RBW is changed, but does **not** change when the generator's frequency step is changed.
5. All existing tests pass after updating to the new density model.
6. New test added: verify RBW-dependent noise floor vs. grid-independent noise floor.

## 6. Files Modified

- `common/common.h` — add `addedNoiseDensity_W_per_Hz`, deprecate `addedNoisePerBin_W`
- `common/spectrum.h` — remove `thermalNoisePower_W`, update comments for density semantics
- `signal_generator/include/signal_generator_engine.h` — remove gain/NF members
- `signal_generator/src/signal_generator_engine.cpp` — implement clean source
- `signal_generator/src/signal_generator_widget.cpp` — remove gain/NF UI controls
- `amplifier/src/amplifier_engine.cpp` — use density model
- `spectrum_analyzer/src/spectrum_analyzer_engine.cpp` — integrate density over RBW, change kernel normalization
- `tests/test_main.cpp` — update existing tests, add new RBW/grid test
