---
type: Domain Guide
title: RF Components — DSP Engine Modules
description: Detailed reference for every RF signal-processing component in the simulator, including design decisions, parameters, dual-mode operation, and test coverage.
tags: [rf-components, dsp-engine, reference]
---

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
- `p1db_dBm` — 1-dB compression point (default 100 dBm, disabled)
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
- **P1dB** (v0.9.0): first-class 1-dB compression point with automatic OIP3 derivation (`OIP3 = P1dB + 9.6 dB`) when OIP3 is at default value (100 dBm). Explicit OIP3 setting is preserved when P1dB changes. Serialized in project save/load.
- **Library data file import** (v0.10.0): when instantiated from a library JSON definition with `data_files` referencing an S-param file, the amplifier auto-loads the Touchstone file via `setSParamFilepath()` during `ComponentLibrary::instantiate()`. Falls back to single-point parameters if the file is missing or invalid.
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

## Attenuator (`attenuator/`)

| Property | Value |
|---|---|
| Headers | `include/attenuator_engine.h` |
| Type | Processing node (1 input, 1 output) |
| CMake target | `simulator::attenuator_engine` |
| Added | July 2026 (v0.7.0) |

**Purpose:** Passive attenuator with configurable attenuation, physically accurate noise model, and S-parameter mode.

**Parameters:**
- `atten_dB` — attenuation in dB (clamped 0–200 dB)
- S-param mode: toggle + `.sNp` file path

**Dual mode operation:**
- **Manual mode:** Flat attenuation applied to all tones (`P_out = P_in - atten_dB`). Noise follows passive model: `noise_total = noise_in * G + k*T*(1 - G)` where `G = 10^(-atten/10)`.
- **S-param mode:** S21 interpolation replaces flat attenuation. Complex S21 magnitude/phase applied to tones, `|S21|^2` scaling applied to noise. Same passive noise model.

**Design decisions:**
- Physically accurate noise model: as attenuation increases, output noise converges to `k*T` (thermal floor), never below it.
- Phase unchanged in manual mode; S-param mode applies `arg(S21)` phase rotation.
- Dedicated widget not required — properties edited via InspectorPanel with attenuation slider.
- Zigzag schematic symbol in node graph.

**Tests:** `test_attenuator.cpp` — pass-through at 0 dB, flat 6 dB attenuation, passive noise model, noise floor convergence at high attenuation, S-param mode, clamping, dirty-flag skip, hover summary.

---

## Combiner (`combiner/`)

| Property | Value |
|---|---|
| Headers | `include/combiner_engine.h` |
| Type | Processing node (**2 inputs**, 1 output) |
| CMake target | `simulator::combiner_engine` |
| Added | July 2026 (v0.7.0) |

**Purpose:** Passive 2-input → 1-output RF combiner with Wilkinson model and 3-port S-parameter mode.

**Parameters:**
- Manual / S-param mode toggle
- S-param mode: `.sNp` file path (3-port .s3p files for S21, S31)

**Dual mode operation:**
- **Manual mode (Wilkinson):** Each input sees `COMBINER_LOSS_DB = 3.0103 dB` loss. Tones from both inputs are combined into a single output list. Noise is summed incoherently, then scaled by loss: `noise_out = G * (n0 + n1)`.
- **S-param mode:** Uses 3-port Touchstone data. Port 0 = input 0, port 1 = input 1, port 2 = output. S21 applied to input 0 tones/noise, S31 applied to input 1 tones/noise. Passive noise model with thermal floor.

**Design decisions:**
- Exact dual of splitter (same -3 dB loss per path).
- Coherent signal combination: tones from both inputs are preserved with their original frequencies, powers, and phases (minus combiner loss).
- S-param mode supports true 3-port devices — S21 (in0→out) and S31 (in1→out) are interpolated separately.
- Y-shaped schematic symbol in node graph.
- No added noise in manual mode (ideal passive combiner); S-param mode adds thermal noise `k*T*(1 - |S21|^2 - |S31|^2)`.

**Tests:** `test_combiner.cpp` — basic combination with -3 dB loss, single input, both inputs unconnected, dirty-flag skip, hover summary, S-param mode.

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

---

## Component Data Files

S-parameter measurement data files are stored in `component_data/` at the repository root, organized by type:

| Directory | Contents |
|---|---|
| `amplifiers/` | Amplifier .s2p files |
| `equalizers/` | Equalizer .s2p files |
| `filters/` | Filter .s2p files |
| `fixed_attenuators/` | Fixed pad .s2p data |
| `splitters/` | Splitter .s2p data |
| `step_attenuators/` | Step attenuator .s2p data |

These feed the per-component S-param modes described above.

---

## Component Library (`app/`)

| Property | Value |
|---|---|
| Headers | `app/include/component_library.h`, `app/include/library_browser_widget.h` |
| Type | Application-level feature (not a DSP engine) |
| Added | v0.9.0 |

**Purpose:** File-based component definition system with a library browser panel for one-click insertion into the node graph.

**JSON definitions** live in `component_data/library/` organized by type → manufacturer:
- `component_data/library/amplifiers/mini-circuits/mga-62563.json`
- `component_data/library/amplifiers/mini-circuits/zx60-33ln.json`
- `component_data/library/filters/mini-circuits/bfc-160.json`
- (and corresponding entries for attenuators, splitters, mixers, equalizers, combiners, ADCs)

Each JSON definition contains datasheet parameters (gain, NF, OIP3, P1dB) used to pre-populate the instantiated component.

**Library Browser panel** (`LibraryBrowserWidget`):
- Tree view grouped by component type → manufacturer
- Text filter for quick search
- One-click insert places the component in the node graph with all parameters pre-set
- Three scan roots: built-in examples, global `~/.rf-sim/libraries/`, per-project `./rf-sim-libraries/`
- Accessed via View menu

**Part number display** (v0.9.1): instantiated component blocks in the node editor show the library part number as a subtitle below the title bar.

**Tests:** `tests/test_component_library.cpp` — 11 test cases covering JSON loading, directory scanning, instantiation of all 7 component types, part number propagation.

---

## Notes on the Noise Model

The `Spectrum` data type stores noise as **power spectral density in W/Hz** throughout the signal chain. This was migrated from an earlier per-bin W model. The old helper function `addedNoisePerBin_W()` is deprecated in favor of PSD-based computation. Each engine adds noise density appropriate to its physical model:

- **Signal Generator:** Thermal noise floor `k·T = 4.00e-21 W/Hz` (≈ −174 dBm/Hz at 290 K)
- **Amplifier:** `N_added = k·T·(10^(NF/10) − 1)·G_linear`
- **Attenuator:** Passive noise model where NF equals attenuation value
- **Mixer:** Noise figure applied as added noise density
- **ADC:** NSD (noise spectral density) in dBm/Hz
- **Coax:** Noise from physical loss model
