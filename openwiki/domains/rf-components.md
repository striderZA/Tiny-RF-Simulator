# RF Components — DSP Engine Modules

Every RF component in the simulator follows the same pattern: a **pure-DSP engine** (`*Engine`) that inherits from `IComponentEngine` and a **widget** (`*Widget`) for the ImGui property editor. This page documents each component's purpose, parameters, design decisions, and implementation notes.

---

## Signal Generator (`signal_generator/`)

| Property | Value |
|---|---|
| Headers | `include/signal_generator_engine.h`, `include/signal_generator_widget.h` |
| Type | Source node (no input, 1 output) |
| CMake target | `simulator::signal_generator_engine` |

**Purpose:** Produces discrete tones with configurable frequency, power, and phase.

**Parameters:**
- Tone list: `{freq_Hz, power_dBm, phase_deg}[]` (editable table in widget)
- `Fs_Hz` — sample rate for downstream digital components

**Design decisions:**
- Output noise floor is **thermal noise** `k*T = 4.00e-21 W/Hz` (~ -174 dBm/Hz at 290 K).
- Frequency grid is fixed at 10 MHz spacing across [-20 GHz, 20 GHz] (4001 bins).
- Has no input pins — cannot receive signals.

**Widget:** `SignalGeneratorWidget` — table UI with add/delete tone rows, "Measure" checkbox for probe.

---

## Amplifier (`amplifier/`)

| Property | Value |
|---|---|
| Headers | `include/amplifier_engine.h` |
| Type | Processing node (1 input, 1 output) |
| CMake target | `simulator::amplifier_engine` |

**Purpose:** Applies gain, noise figure, optional nonlinear distortion (OIP2/OIP3), and S-parameter-based frequency response.

**Parameters:**
- `gain_dB` — flat gain
- `nf_dB` — noise figure (clamped to >= 0 dB)
- Nonlinear mode: OIP2 (dBm), OIP3 (dBm) — generates harmonics and IMD
- S-param mode: toggle + `.sNp` file path

**Dual mode operation:**
- **Ideal mode:** `G(f) = flat gain`, additive noise from NF model.
- **S-param mode:** `G(f) = |S21(f)|` from Touchstone data. NF still applied on top.

**Design decisions:**
- Noise figure model: `N_added = k * T * (10^(NF/10) - 1) * G_linear`
- Nonlinearity computed on tones only: 2nd/3rd harmonics, IM2/IM3 for up to 3 fundamentals, gain compression.
- `NonlinearModel` (`common/include/nonlinear_model.h`) is a reusable class extracted from the amplifier.
- OIP2/OIP3 clamped to >= -30 dBm.
- No dedicated widget — properties edited via `InspectorPanel`.

**Tests:** `test_amplifier_sparam.cpp`, coverage via `test_main.cpp`.

---

## Mixer (`mixer/`)

| Property | Value |
|---|---|
| Headers | `include/mixer_engine.h` |
| Type | Processing node (1 input, 1 output) |
| CMake target | `simulator::mixer_engine` |

**Purpose:** Down/up-converts signals using an LO frequency.

**Parameters:**
- `lo_freq_Hz` — local oscillator frequency
- `conv_gain_dB` — conversion gain
- `nf_dB` — noise figure

**Design decisions:**
- Each input tone produces two output tones: `|f - LO|` (lower sideband) and `f + LO` (upper sideband).
- Simple ideal mixer: no LO harmonics, no image rejection, no intermodulation.
- Phase is preserved (not conjugated) on the lower sideband.
- Noise figure applied as added noise density.

**Tests:** Covered in `test_main.cpp`.

---

## Splitter (`splitter/`)

| Property | Value |
|---|---|
| Headers | `include/splitter_engine.h` |
| Type | Processing node (1 input, **2 outputs**) |
| CMake target | `simulator::splitter_engine` |

**Purpose:** Splits input signal equally into two outputs.

**Parameters:** None (hardcoded split loss).

**Design decisions:**
- Pure resistive model: `SPLIT_LOSS_DB = 3.0103 dB` per branch.
- No added noise, no phase change.
- Powers on tones and noise are divided equally.

**Tests:** Covered in `test_main.cpp`.

---

## Ideal Filter (`ideal_filter/`)

| Property | Value |
|---|---|
| Headers | `include/ideal_filter_engine.h` |
| Type | Processing node (1 input, 1 output) |
| CMake target | `simulator::ideal_filter_engine` |

**Purpose:** Perfect brickwall passband/stopband filter.

**Parameters:**
- `FilterType`: LPF, HPF, BPF, BSF
- Cutoff frequency(ies): `freq1_Hz`, `freq2_Hz` (for BPF/BSF)
- S-param mode: toggle + `.sNp` file path

**Dual mode operation:**
- **Ideal mode:** Binary `isInPassband()` — tones outside passband dropped entirely, noise zeroed in stopband.
- **S-param mode:** S21 interpolation replaces binary decision.

**Design decisions:**
- Perfect rejection in stopband, zero insertion loss in passband.
- No phase distortion in ideal mode.
- Cutoff edge: tones exactly at cutoff **pass** (LPF) or **block** (HPF) per test.

**Tests:** `test_ideal_filter.cpp`, `test_ideal_filter_sparam.cpp`.

---

## Equalizer (`equalizer/`)

| Property | Value |
|---|---|
| Headers | `include/equalizer_engine.h` |
| Type | Processing node (1 input, 1 output) |
| CMake target | `simulator::equalizer_engine` |
| Added | July 2026 (S-param rework) |

**Purpose:** Applies a configurable gain-vs-frequency slope.

**Parameters:**
- `ref_gain_dB` — reference gain
- `ref_freq_Hz` — reference frequency (clamped to >= 1 Hz)
- `slope_dB_per_decade` — gain slope per frequency decade
- S-param mode: toggle + `.sNp` file path

**Dual mode operation:**
- **Ideal mode:** `G(f) = refGain + slope * log10(f / refFreq)`
- **S-param mode:** S21 interpolation. No added noise.

**Design decisions:**
- Gains are referenced to a user-specified frequency.
- Slope is in dB/decade (not dB/octave).
- NaN protection: `log10(0)` clamped in both frequency and noise computations.
- S-param mode applies complex S21 magnitude/phase to tones and `|S21|^2` to noise.

**Tests:** `test_equalizer.cpp`.

---

## Coaxial Cable (`coax/`)

| Property | Value |
|---|---|
| Headers | `include/coax_cable_engine.h`, `include/coax_presets.h` |
| Type | Processing node (1 input, 1 output) |
| CMake target | `simulator::coax_cable_engine` |

**Purpose:** Models frequency-dependent loss and phase delay for coaxial cables.

**Parameters:**
- `preset_index` — cable type from `kCoaxCablePresets[6]`
- `length_m` — cable length (clamped 0–1000 m)
- `connectors_loss_dB` — additional connector loss

**Preset cables (MilTech):**
Only **MT 340** is fully populated. Others (MT 210, 230, 265, 300, 480) are stubbed with `TODO`.

| Preset | K1 | K2 | Delay (ns/m) | Max Freq |
|---|---|---|---|---|
| MT 340 | 0.000375 | 0.0 | 3.95 | 18 GHz |

**Design decisions:**
- Loss model: `loss_dB = (K1 * sqrt(f) + K2 * f) * length_m + connectors_loss_dB`
- Phase shift: linear `-360 * (f/1e9) * length * delay` degrees.
- Clamps frequency to preset `max_freq_GHz` with one-time warning.
- Noise is attenuated by the inverse of loss (passive component behavior: noise in, quieter out).
- Phase is applied per-bin and per-tone.

**Tests:** `test_coax_cable_engine.cpp`, `test_coax_cable_presets.cpp`.

---

## RF ADC (`adc/`)

| Property | Value |
|---|---|
| Headers | `include/adc_engine.h` |
| Type | Processing node (1 input, 1 output) |
| CMake target | `simulator::adc_engine` |

**Purpose:** Models an RF ADC with sampling, aliasing, and NSD noise.

**Parameters:**
- `fs_Hz` — sample rate (default 1 GHz)
- `nsd_dBm_per_Hz` — noise spectral density (default -155 dBm/Hz)

**Design decisions:**
- Semi-idealized sampling: tones alias into Nyquist zones via `alias_frequency()`.
- Center frequency shifted by `-Fs/4` (digital down-conversion / NCO shift).
- Noise density injected as `noise_added_W`.
- Output grid spans `[-Fs/4, Fs/4)`.
- Output `fs_Hz` = `Fs/2`.
- Removed parameters: `bits` (dead), `v_fs` (dead) — these were removed in a recent cleanup.

**Tests:** `test_adc.cpp`.

---

## PFB Channelizer (`pfb_channelizer/`)

| Property | Value |
|---|---|
| Headers | `include/pfb_channelizer_engine.h`, `include/pfb_channelizer_widget.h` |
| Type | Processing node (1 input, **2 outputs**) |
| CMake target | `simulator::pfb_channelizer_engine` |

**Purpose:** Decomposes wideband input into M equally-spaced channels using a polyphase filter bank.

**Parameters (PFBConfig):**
- `M` — number of channels (2–2048)
- `K` — taps per branch (1–64)
- `Fs` — sample rate (auto-read from input `fs_Hz`)
- `beta` — Kaiser window beta (0–20)

**Outputs:**
- `outputs[0]` — active channel (weighted bin zoom)
- `outputs[1]` — full reconstructed spectrum (overlap-averaged for flatness)

**Design decisions:**
- Prototype filter: Kaiser window * sinc, window method.
- Active channel query via `activeChannelId()`, `activeChannelBandwidth()`, `activeChannelCenterFreq()`.
- Noise flatness at overlap boundaries: <1% ripple.
- `Fs_Hz` is automatically read from the input Spectrum (set by ADC).

**Tests:** `test_pfb.cpp`.

---

## Spectrum Analyzer (`spectrum_analyzer/`)

| Property | Value |
|---|---|
| Headers | `include/spectrum_analyzer_engine.h`, `include/spectrum_analyzer_widget.h` |
| CMake target | `simulator::spectrum_analyzer_engine` |

**Purpose:** Renders spectrum traces from probed nodes onto an ImPlot display.

**Parameters:**
- Span (start/stop frequency)
- Reference level (dBm)
- RBW (resolution bandwidth, Hz)
- VBW (video bandwidth, Hz)
- Noise jitter amplitude (default 1.5 dB)

**Performance features:**
- **RBW caching:** Gaussian convolution result cached until spectrum generation or RBW changes.
- **Jitter + VBW** applied every frame on top of cached RBW result.
- **Combined spectrum:** sums per-bin power across all probed nodes.

**Marker features:**
- Peak search with snap-to-peak navigation.
- Next/previous peak.
- Drag-to-zoom on frequency axis with reset.

**Tests:** Benchmarks in `test_bench_dsp.cpp`.

---

## IQ Plot (`iq_plot/`)

| Property | Value |
|---|---|
| Headers | `include/iq_plot_dsp.h`, `include/iq_plot_widget.h` |
| CMake target | `simulator::iq_plot_widget` |

**Purpose:** Converts frequency-domain spectrum to time-domain I/Q samples via IFFT and displays them.

**Key functions:**
- `build_iq_spectrum()` — pure DSP helper: constructs complex frequency vector from noise PSD + tones.
- `runIDFT()` — calls `kiss_fft` (backward IFFT).

**Design decisions:**
- Uses **kiss_fft** for IFFT (lightweight, no heavy dependency).
- Ring buffer: `kMaxSamples = 4096`, new IFFT output appended, front trimmed.
- EMA smoothing on Y-axis for stable auto-scaling.
- Drag-to-zoom, reset-zoom, auto-scale buttons.

**Tests:** `test_iq_plot.cpp`.
