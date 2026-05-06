# General Amplifier Model — Nonlinear Behaviour Reference

This document summarises the nonlinear amplifier model from the reference PDF. It is intended as a specification for an agent to implement nonlinear behaviour into an existing amplifier model.

---

## 1. Model Architecture

The model processes signals through five sequential stages:

1. **Signal & Noise Input** — dBm signal levels and noise floor are provided as inputs.
2. **Harmonic Generation** — nonlinear harmonics (2nd, 3rd) are computed per input tone.
3. **k-coefficient Extraction** — nonlinearity coefficients k1 and k2 are derived from harmonic voltage amplitudes.
4. **Intermodulation Distortion (IMD)** — two-tone IMD products are computed using k1 and k2.
5. **Output Power** — total output power is the sum of signal, harmonics, IMD products, and amplified noise.

---

## 2. Power Calculations

### Signal Power (linear sum of input tones)
```
Psignal = sum over k of 10^(SignalLevel_k / 10)   [in mW]
```

### Noise Power (integrated over span)
```
Pnoise = sum over k of 10^(NoiseLevel_k / 10) * (Span / N)   [in mW]
```

### Total Input Power
```
Ptotal = Psignal + Pnoise
```

### Output Noise Power (includes external noise, thermal noise, noise figure, gain)
```
Pout = [(Pext + kT0*B) * (f - 1)] * g
```
Where:
- `Pext` = external input noise power
- `kT0*B` = thermal noise power (Boltzmann * reference temp * bandwidth)
- `f` = noise figure (linear)
- `g` = gain (linear)

---

## 3. Harmonic Generation

For a given input frequency `Fc` with gain `Gain`, the harmonic array is computed as:

```c
HArr[0] = Fc;
HArr[1] = Fc * 2;
HArr[2] = Fc * 3;
HArr[3] = Fc + Gain;

// Harmonic power levels (dBm):
HArr[5] = 10 * log10((pow(k1, 2) * pow(Vp1, 2)) / 2) / 50) / 0.001);
HArr[6] = 10 * log10((pow(k2, 2) * pow(Vp1, 3)) / 4) / 50) / 0.001);
```

- `HArr[5]` = 2nd harmonic power level (dBm)
- `HArr[6]` = 3rd harmonic power level (dBm)
- `Vp1` = fundamental voltage amplitude

---

## 4. Nonlinearity Coefficients (k1, k2)

These are extracted from the harmonic voltage amplitudes at the output:

```c
Vp1 = sqrt(pow(10, (Ys[0] / 10)) * 0.001 * 50);   // Fundamental voltage
Vp2 = sqrt(pow(10, (Ys[1] / 10)) * 0.001 * 50);   // 2nd harmonic voltage
Vp3 = sqrt(pow(10, (Ys[2] / 10)) * 0.001 * 50);   // 3rd harmonic voltage

k[0] = 1.0;
k[1] = (2 * Vp2) / pow(Vp1, 2);        // 2nd order nonlinearity coefficient
k[2] = (4 * Vp3) / pow(Vp1, 3);        // 3rd order nonlinearity coefficient
```

- `k1` captures second-order nonlinearity (drives 2nd harmonic and 2nd-order IMD)
- `k2` captures third-order nonlinearity (drives 3rd harmonic and 3rd-order IMD, i.e. IM3)

---

## 5. Intermodulation Distortion (Two-Tone)

For two input tones at frequencies `Fc1` and `Fc2`:

### IMD Frequency Products
```c
IMDArr[0]  = |Fc1 - Fc2|
IMDArr[1]  = |Fc1 + Fc2|
IMDArr[2]  = |2*Fc1 + Fc2|
IMDArr[3]  = |2*Fc1 - Fc2|
IMDArr[4]  = |2*Fc2 + Fc1|    // Note: fabs(Fc1 - (2 * Fc2)) in code
IMDArr[5]  = |Fc1 - 2*Fc2|
```

### IMD Voltage Amplitudes
```c
Vp1 = sqrt(pow(10, (Y1 / 10)) * 0.001 * 50);   // Tone 1 amplitude
Vp2 = sqrt(pow(10, (Y2 / 10)) * 0.001 * 50);   // Tone 2 amplitude

IMDArr[7]  = k1 * Vp1 * Vp2                             // 2nd order: f1±f2
IMDArr[8]  = k1 * Vp1 * Vp2                             // (same)
IMDArr[9]  = (3/4) * k2 * pow(Vp1, 2) * Vp2            // 3rd order: 2f1±f2
IMDArr[10] = (3/4) * k2 * pow(Vp1, 2) * Vp2
IMDArr[11] = (3/4) * k2 * Vp1 * pow(Vp2, 2)            // 3rd order: f1±2f2
IMDArr[12] = (3/4) * k2 * Vp1 * pow(Vp2, 2)
```

### Convert IMD Voltages to dBm
```c
for (i = 7; i <= 12; i++)
    IMDArr[i] = 10 * log10(((pow(IMDArr[i], 2) / 50) / 0.001));
```

---

## 6. Implementation Checklist for Agent

To add nonlinear behaviour to an existing amplifier model, implement the following:

- [ ] **Harmonic generator**: compute 2nd and 3rd harmonic frequencies and power levels using `k1`, `k2`, and the fundamental voltage amplitude `Vp1`.
- [ ] **k-coefficient extraction**: derive `k1` and `k2` from measured or simulated harmonic output voltages (Vp1, Vp2, Vp3).
- [ ] **Two-tone IMD**: for any two input tones, calculate the 6 IMD frequency products and their power levels (IMDArr[7..12]).
- [ ] **Power summation**: sum signal, harmonic, and IMD contributions (in linear mW) before converting back to dBm.
- [ ] **Noise at output**: apply the noise figure and gain to compute output noise using `Pout = [(Pext + kT0B) * (f - 1)] * g`.
- [ ] **Compression**: the Input vs. Output curve shows gain compression — the nonlinear model naturally produces this when harmonic/IMD power is subtracted from the fundamental.

---

## 7. Reference Plots

The original model produces two validation plots:

- **Input vs. Output**: fundamental, 2nd, and 3rd harmonic output power vs. input power (range: -60 to 0 dBm). Shows gain compression at high input levels.
- **Gain & NF vs. Frequency**: flat gain (~13 dB) and noise figure (~4 dB) from 0–20 GHz, confirming broadband operation.

---

## 8. Key Variables Reference

| Variable | Description |
|----------|-------------|
| `Fc`, `Fc1`, `Fc2` | Input carrier frequency / two-tone frequencies |
| `Gain` | Amplifier gain (dB) |
| `k1` | 2nd-order nonlinearity coefficient |
| `k2` | 3rd-order nonlinearity coefficient |
| `Vp1, Vp2, Vp3` | Voltage amplitudes: fundamental, 2nd harmonic, 3rd harmonic |
| `f` | Noise figure (linear) |
| `g` | Gain (linear) |
| `kT0B` | Thermal noise power (Boltzmann × T₀ × bandwidth) |
| `Pext` | External input noise power |
| `Span` | Frequency span of analysis |
| `N` | Number of noise data points |
