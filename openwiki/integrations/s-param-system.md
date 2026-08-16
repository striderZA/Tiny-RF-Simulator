---
type: Integration Guide
title: S-Parameter System
description: Documentation for the Touchstone-based S-parameter system covering file parsing, interpolation, per-component S-param modes (amplifier, filter, equalizer, attenuator, combiner), and inspector panel integration.
tags: [s-parameter, touchstone, integration, dsp]
---

# S-Parameter System

The S-parameter system provides frequency-dependent component behavior from industry-standard Touchstone (.sNp) files.

---

## Architecture

The S-parameter system has two layers:

### 1. Shared Data Layer (`touchstone/`) — untouched by the S-param rework

| File | Purpose |
|---|---|
| `touchstone/include/s_parameter_data.h` | `SParameterData` — load, interpolate, apply to spectrum |
| `touchstone/include/touchstone_parser.h` | `TouchstoneParser` — parse .sNp files |
| `touchstone/src/s_parameter_data.cpp` | Linear interpolation of complex S-params, `applyToSpectrum()` |
| `touchstone/src/touchstone_parser.cpp` | File parsing, validation, reordering |

### 2. Per-Component S-Param Modes — added in the July 2026 rework

Instead of a single generic `SParamEngine`, five components have their own S-parameter mode toggle:

| Component | S21 Interpolation Replaces | Source File |
|---|---|---|
| `AmplifierEngine` | Flat `gain_dB` | `amplifier/src/amplifier_engine.cpp` |
| `IdealFilterEngine` | Binary `isInPassband()` | `ideal_filter/src/ideal_filter_engine.cpp` |
| `EqualizerEngine` | `G(f) = refGain + slope * log10(f/fref)` | `equalizer/src/equalizer_engine.cpp` |
| `AttenuatorEngine` | Flat `atten_dB` | `attenuator/src/attenuator_engine.cpp` |
| `CombinerEngine` | Manual -3 dB Wilkinson model | `combiner/src/combiner_engine.cpp` |

---

## Touchstone Parser

**`TouchstoneParser::parse(filepath)`** reads industry-standard .sNp files.

### Supported Formats

| Format | Meaning | Example |
|---|---|---|
| DB | Magnitude in dB, angle in degrees | `-3.0 -45.0` |
| MA | Linear magnitude, angle in degrees | `0.707 -45.0` |
| RI | Real, Imaginary | `0.5 -0.5` |

### Supported Parameters

- **S** (scattering) — actively used by components
- Y, Z, H, G — parsed but not used in processing

### Port Count

Inferred from file extension: `.s1p` → 1 port, `.s2p` → 2 ports, up to `.s4p` (4-port tested).

### Data Reordering

Touchstone stores data in **column-major** format (`S11, S21, S12, S22` for 2-port). The parser converts to **row-major** (`S11, S12, S21, S22`) for easier matrix access.

### Validation

- Rejects NaN, inf, negative, or non-monotonic frequency values.
- Enforces 10 million point maximum.
- Missing option line returns `nullopt`.
- Recent fix: uses `lower_bound` for interpolation, clamps `log10(0)`.
- **S2 hardening (2026-08-09 codebase review):** the parser now refuses files > 256 MiB before reading anything (`MAX_FILE_BYTES`, enforced with `tellg()` in binary mode) and caps buffered frequency points at 10M **in-loop** (`MAX_FREQ_POINTS` × values-per-point), so an oversized file bails out before allocating a ~960 MB `raw_values` buffer. Post-loop defense-in-depth check retained.

### S-param Path Containment (S1, 2026-08-09 codebase review)

Untrusted S-param paths are confined to their owning root at both load boundaries:

- **Project load** (`app/src/project_serializer.cpp`): `resolveSparamPath()`/`resolveSparamParams()` rewrite `sparam_filepath`/`sparam_path` in-place. Absolute paths outside the project dir, `..` traversal, or unresolvable paths are neutralized to `""` with a warning. On **save**, `relativizeSparamParams()` re-writes in-project absolute paths as project-relative for portability.
- **Library instantiate** (`app/src/component_library.cpp`): `resolveDataFilePath()` only honors `data_files` entries whose canonical form stays inside the library JSON's directory — escaping entries are skipped, and instantiation falls back when every data file escapes.

Shared helper discipline mirrors `extension_manifest.cpp`'s `pathWithinRoot()`/`resolveWithinRoot()` (canonical-path prefix comparison via `std::filesystem::weakly_canonical`). Covered by the standalone `test_path_containment` executable (5 tests).

### Data File Repository

S-parameter measurement data files live in `component_data/` at the repository root, organized by component type (`amplifiers/`, `filters/`, `equalizers/`, etc.). These are industry-standard Touchstone files used in the per-component S-param modes and test fixtures.

---

## SParameterData

**`SParameterData::load(filepath)`** parses and stores the data.

**`SParameterData::interpolate(freq_Hz, param_idx)`** linearly interpolates complex S-parameters at arbitrary frequencies. Index `param_idx` selects which S-param matrix element (e.g., `1 * numPorts + 0` = S21).

**`SParameterData::applyToSpectrum(signalNode, paramIdx)`** applies magnitude and phase from the interpolated S-param to both tones and noise in a Spectrum.

---

## Per-Component Mode Details

### Amplifier S-Param Mode

- `m_sparam_mode` (bool) toggles between ideal and S-param.
- `m_sparam_filepath` stores the loaded .sNp path.
- `SParameterData m_sparam_data` holds the parsed data.
- In S-param mode: `G(f) = |S21(f)|`, phase from `arg(S21)`. Noise figure and nonlinearity are still applied on top.
- Interpolation at any frequency within the data's range; out-of-band frequencies use nearest neighbor.

### IdealFilter S-Param Mode

- Same toggle pattern.
- S21 replaces the binary passband check.
- Only forward-transmission S21 is used (no reverse isolation considered).

### Equalizer S-Param Mode

- S-param interpolation replaces the `G(f) = refGain + slope * log10(f/fref)` ideal model.
- No added noise in either mode.

### Attenuator S-Param Mode

- S21 replaces flat `atten_dB`. Each tone's power is modified by `20*log10(|S21|)`, phase by `arg(S21)`.
- Passive noise model: `noise_total = noise_in * |S21|^2 + k*T*(1 - |S21|^2)`.
- Supports 2-port .s2p Touchstone files.

### Combiner S-Param Mode

- Uses 3-port Touchstone data (`.s3p`): port 0 = input 0, port 1 = input 1, port 2 = output.
- Two S-parameters interpolated independently: S21 (in0→out) and S31 (in1→out).
- Passive noise model with thermal floor: `noise_added = k*T*(1 - |S21|^2 - |S31|^2)`.
- Manual mode fallback applies -3 dB Wilkinson loss per input.

---

## Inspector Panel UI

The InspectorPanel (`app/src/inspector_panel.cpp`) provides S-param controls for each compatible component:

1. **Mode combo box** — "Ideal" / "S-Parameter"
2. **File browser button** — click to open native file dialog (via portable-file-dialogs)
3. **Status indicator** — shows loaded file path or "None" with error state
4. **Gain control** — disabled when in S-param mode (amplifier)
5. **S-param mode switching** — the mode combo triggers the switch to S-param mode and back (fixed after the original S-param rework)

---

## Project Save/Load Integration

`.rfsim` files persist `sparam_mode` and `sparam_filepath` per engine. Since v0.16.3 (fix #56/#44), `deserialize()` **reloads the Touchstone data immediately** from the persisted path (`m_sparam_data.load(filepath)`) instead of waiting for the user to re-toggle the mode, and only re-enables `sparam_mode` if the file actually loads (`j.value("sparam_mode", false) && m_sparam_data.loaded()`). This applies to all five S-param-capable engines (amplifier, ideal filter, equalizer, attenuator, combiner). Covered by the "Round-trip: S-param mode survives save/load (issue #44)" test in `tests/test_project_file.cpp`.

---

## Historical Context

The S-parameter system evolved through several phases:

| Phase | When | What |
|---|---|---|
| Original SParamFilter/SParamAmp | Pre-June 2026 | Separate engines for filter and amp S-param support |
| Unified SParamEngine | June 19, 2026 | Merged into a single generic engine |
| Multi-port SParamEngine | June 19, 2026 | Added dynamic pins, full matrix, Splitter/Combiner modes |
| **S-param rework** | **July 6, 2026** | **Deleted generic SParamEngine, created per-component modes** |

The rework was motivated by the observation that a generic SParamEngine duplicated component-specific behavior (NF, nonlinearity for amps; no noise for filters). Each component now loads its own `.sNp` and uses S21 in its `update()` branch.

---

## Key Source Files

| File | Role |
|---|---|
| `touchstone/src/touchstone_parser.cpp` | File format parser (256 MiB size guard + 10M point cap) |
| `touchstone/src/s_parameter_data.cpp` | Data storage + interpolation |
| `amplifier/src/amplifier_engine.cpp` | Amplifier S-param mode |
| `ideal_filter/src/ideal_filter_engine.cpp` | Filter S-param mode |
| `equalizer/src/equalizer_engine.cpp` | Equalizer S-param mode |
| `attenuator/src/attenuator_engine.cpp` | Attenuator S-param mode |
| `combiner/src/combiner_engine.cpp` | Combiner 3-port S-param mode |
| `app/src/inspector_panel.cpp` | S-param file browser UI |
| `app/src/project_serializer.cpp` | S1 load/save path containment (`resolveSparamPath`/`relativizeSparamParams`) |
| `app/src/component_library.cpp` | S1 library `data_files` containment (`resolveDataFilePath`) |
| `tests/test_path_containment.cpp` | S1/S2 regression tests (standalone executable) |
| `component_data/` | Touchstone data files for testing |

---

## For Future Agents

- **Forward param is always S21** (index `1 * numPorts + 0`). For 3-port combiner, S31 (`2 * numPorts + 1`) is also used.
- **NF and nonlinearity stay with the amplifier** — these are physical device properties not derivable from S-parameters.
- To add S-param mode to a new component: add `SParameterData` member + `bool m_sparam_mode`, branch on `update()`, add inspector UI in `draw*Properties()`, add file browser support.
- **Contain any S-param path read from an untrusted file** at the load boundary (project load or library instantiate) using the `pathWithinRoot()`/`weakly_canonical` discipline documented in the [S1 containment](#s-param-path-containment-s1-2026-08-09-codebase-review) section; save-side should relativize in-project paths for portability.
- The `touchstone/` data layer should remain untouched — it's the stable shared library.
