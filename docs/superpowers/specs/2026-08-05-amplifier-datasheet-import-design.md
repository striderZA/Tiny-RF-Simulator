# Amplifier Datasheet Import — Design

**Date:** 2026-08-05
**Status:** Draft — pending user review
**Scope:** Replace PR 31's external-tool amplifier generator with a native in-app datasheet import wizard that digitizes gain and noise-figure curves from an image and exports a schema-v2 amplifier JSON plus `.s2p` pair. Extend amplifier runtime/library support so NF vs frequency is preserved and simulated. Mixers remain scalar-NF only and are out of scope.

## 1. Background

PR 31 currently implements a headless external-tool workflow: scaffold a params JSON, hand-edit it, then build a schema-v2 amplifier JSON plus `.s2p` output pair. That matches the current external-tool runner contract, but it does not match the desired workflow.

The intended workflow is interactive: load a datasheet screenshot, click points along the plotted curves (at minimum gain and noise figure), review/edit the captured points, and export an amplifier definition directly usable by the simulator.

The existing external-tool contract is a poor fit for that workflow:

- `ExternalToolRunner` hard-kills tool processes after 30 seconds.
- The current component library consumes schema-v2 JSON plus `s_parameters` data files, not `amp-f`.
- The app already has ImPlot mouse/coordinate interaction patterns, which makes a native UI a better architectural fit than an external process.

## 2. Goals

- Add a native app-side wizard at `Tools > Amplifier: Import From Datasheet...`.
- Allow the user to load an image file (`.png`/`.jpg`) containing a datasheet plot.
- Digitize two curves in v1: gain vs frequency and NF vs frequency.
- Provide a guided flow with an editable review step before export.
- Export the simulator's existing amplifier library format: schema-v2 JSON plus `.s2p`.
- Preserve the full digitized NF curve in the exported definition.
- Update amplifier runtime behavior so S-parameter-mode amplifiers simulate NF vs frequency instead of collapsing to one scalar in generated parts.
- Preserve backward compatibility with existing scalar-`nf_dB` amplifier library files and tests.

## 3. Non-Goals

- **PDF import/rendering.** V1 accepts image files only.
- **Generic multi-component digitizer.** V1 is amplifier-specific.
- **Mixer NF-vs-frequency support.** Mixers keep their existing scalar `nf_dB` behavior.
- **Amp-f import/export.** The simulator continues to use schema-v2 JSON plus `.s2p` as its app-facing format.
- **Automatic curve tracing / OCR.** Point capture is manual clicking only.
- **Extension-system integration.** This feature becomes native app UI, not an `external-tool` extension.

## 4. Architecture

### 4.1 Approach: native app-side wizard

Replace PR 31's extension-driven workflow with a native wizard hosted in `app/`. The wizard owns the full interactive flow:

1. Load image
2. Calibrate axes for Gain
3. Click Gain points
4. Calibrate axes for NF
5. Click NF points
6. Review/edit metadata and points
7. Export JSON + `.s2p`

This follows the existing codebase pattern better than an external process:

- UI stays in `app/`
- export logic stays testable as pure logic/service code
- runtime/library behavior changes stay in the existing amplifier/library layers

### 4.2 New pieces

#### `AmplifierDigitizerModel`

Pure state/model object. Owns:

- source image path
- wizard step state
- calibration for Gain and NF
- clicked point sets for Gain and NF
- typed metadata (`part_number`, `manufacturer`, OIP3/P1dB/OIP2, notes, return loss)
- validation and export-ready transformed data

#### `AmplifierDigitizerWidget`

ImGui/ImPlot wizard UI. Owns rendering and interactions:

- image preview
- calibration controls
- click-to-add point handling
- review/edit tables
- export action

#### `ImageTexture` helper

Small UI/platform helper to decode an image and upload it to an OpenGL texture for ImGui display. This is the only genuinely new UI primitive required; the rest follows existing ImGui/ImPlot patterns.

#### `AmplifierExportService`

Pure logic service that transforms reviewed digitized data into:

- schema-v2 amplifier JSON
- unilateral approximation `.s2p`

The collision-safe naming and S2P synthesis ideas from PR 31 can be reused, but the implementation should live in C++ with the app rather than in `extensions/amplifier-generator/` Python.

## 5. Wizard Flow

### 5.1 Step 1 — source image

- User chooses a `.png` or `.jpg` file.
- The wizard loads the image and shows an inline preview.
- Failure to decode keeps the user on Step 1 with an inline error.

### 5.2 Step 2 — Gain curve calibration

The user identifies the Gain plot region by calibration clicks:

- two X-axis reference points
- two Y-axis reference points
- typed real-world values for each reference

Axis configuration:

- X axis: linear or log (user-selectable)
- Y axis: linear
- units: frequency on X, dB on Y

The model derives a pixel-to-plot mapping for the Gain view.

### 5.3 Step 3 — Gain point capture

- User clicks along the gain trace.
- Each click becomes a `(freq_Hz, gain_dB)` point.
- Points auto-sort by frequency.
- The UI supports undo-last, clear-all, and direct table editing.

At least two Gain points are required before progressing.

### 5.4 Step 4 — NF curve calibration

Repeat the same calibration flow for the NF graph. Calibration is independent of Gain so the two curves may come from different regions of the same image.

### 5.5 Step 5 — NF point capture

- User clicks along the NF trace.
- Each click becomes a `(freq_Hz, nf_dB)` point.
- Points auto-sort by frequency.
- Undo/clear/edit behavior matches Gain.

At least two NF points are required before export.

### 5.6 Step 6 — review/edit

Final review page shows:

- editable Gain point table
- editable NF point table
- typed scalar metadata fields:
  - part number
  - manufacturer
  - OIP3
  - P1dB
  - OIP2
  - input return loss
  - output return loss
  - notes / description

The user may adjust points numerically before export.

### 5.7 Step 7 — export

Export writes:

- `rf-sim-libraries/amplifiers/<manufacturer>/<part>.json`
- matching `<part>.s2p`

On name collisions, export never overwrites silently; it writes `<part>-2`, `<part>-3`, etc.

## 6. Data Model

### 6.1 Exported runtime shape

Generated amplifier JSON remains schema-v2 and app-loadable. For v1 it should include:

```json
{
  "schema_version": 2,
  "type": "amplifier",
  "part_number": "...",
  "manufacturer": "...",
  "parameters": {
    "nf_db_vs_freq": [[100000000.0, 1.2], [200000000.0, 1.3]],
    "oip3_dBm": 35.0,
    "p1db_dBm": 20.0,
    "oip2_dBm": 50.0
  },
  "data_files": [
    {"type": "s_parameters", "path": "PART.s2p"}
  ]
}
```

Frequencies in `nf_db_vs_freq` are stored in **Hz** inside app JSON to avoid unit ambiguity.

### 6.2 Backward-compatible scalar fallback

Existing amplifier parts and tests already hardcode scalar `nf_dB`. That fallback stays supported:

- If `parameters.nf_db_vs_freq` exists, amplifier runtime uses it.
- Else if scalar `parameters.nf_dB` exists, runtime uses the legacy scalar path.
- Existing hand-authored/built-in parts continue to load unchanged.

### 6.3 Authoring metadata

The exported JSON should also preserve enough authoring metadata to reconstruct the wizard session later under a dedicated object, e.g.:

- `authoring.source_image_path`
- `authoring.gain_calibration`
- `authoring.nf_calibration`
- `authoring.gain_db_vs_freq`
- `authoring.nf_db_vs_freq`
- `authoring.exported_at_utc`

This metadata is not a new runtime format; it is retained for traceability and future re-editing.

## 7. Amplifier Runtime Semantics

### 7.1 Current limitation

Today, S-parameter-mode amplifier noise uses:

- per-bin interpolated gain `|S21|^2` for input noise propagation
- one scalar `m_nf_dB` with one average S21 gain to compute a single added-noise density for all bins

That is insufficient when NF vs frequency is a critical datasheet characteristic.

### 7.2 New behavior

For amplifier S-parameter mode:

- interpolate local `|S21(f)|^2` per output bin
- interpolate local `NF(f)` from `nf_db_vs_freq`
- compute added noise per bin using the local gain and local NF
- sum propagated input noise and local added noise per bin

This makes the generated/imported amplifier simulate frequency-dependent NF rather than collapsing to one scalar.

### 7.3 Scope boundary

This upgrade is **amplifier-only**.

Mixers keep their existing separate scalar `nf_dB` model and are not changed in this design.

## 8. Validation and Error Handling

### 8.1 Wizard validation

- image must decode successfully
- calibration requires two valid X references and two valid Y references per curve
- Gain requires at least 2 points
- NF requires at least 2 points
- part number and manufacturer are required
- return loss and nonlinear fields must remain in valid numeric ranges

### 8.2 Point editing invariants

- all captured/editable points auto-sort by frequency
- manual edits that break monotonic frequency order are either rejected or re-sorted immediately
- duplicate frequencies are invalid and must be resolved before export

### 8.3 Export validation

- `nf_db_vs_freq` frequencies must be strictly increasing
- Gain frequencies must be strictly increasing
- export path collisions suffix rather than overwrite
- malformed authoring state never produces partial output files

## 9. Testing

### 9.1 Model / logic tests

- calibration math for linear axes
- calibration math for log-frequency X axis
- point add/undo/clear/edit/sort behavior
- export includes `nf_db_vs_freq`
- collision-safe file naming
- S2P synthesis from Gain points

### 9.2 Library / validation tests

- `ComponentLibrary` accepts amplifier definitions with `nf_db_vs_freq`
- old amplifier JSON with scalar `nf_dB` still loads
- invalid `nf_db_vs_freq` ordering is rejected
- component authoring validation recognizes the new amplifier parameter shape

### 9.3 Amplifier engine tests

- scalar `nf_dB` behavior remains unchanged for legacy parts
- S-parameter-mode amplifier uses `nf_db_vs_freq` when present
- per-bin added noise changes as NF curve changes across frequency
- generated/imported amplifier with both `.s2p` and `nf_db_vs_freq` instantiates correctly

### 9.4 UI / integration tests

- wizard can open and close safely
- image fixture loads successfully
- end-to-end flow creates JSON + `.s2p`
- exported files load into `ComponentLibrary` and instantiate an amplifier

## 10. Files / Areas Affected

| Area | Planned change |
|---|---|
| `app/` | Add native digitizer model/widget, menu action, image helper, export wiring |
| `amplifier/` | Add NF-vs-frequency runtime support for S-parameter mode |
| `app/src/component_library.cpp` + related validation/registry code | Accept/load `nf_db_vs_freq` for amplifiers while preserving scalar fallback |
| `tests/` | Add model, library, engine, and UI/integration tests |
| `extensions/amplifier-generator/` | Remove or supersede PR 31's external-tool implementation |

## 11. Migration Plan for PR 31

PR 31 should be reshaped from "external-tool extension" into two tightly related product changes:

1. **Native amplifier datasheet import wizard**
2. **Amplifier NF-vs-frequency support**

Useful ideas to carry forward from PR 31:

- unilateral `.s2p` synthesis from Gain-vs-frequency points
- collision-safe export naming
- end-to-end tests that prove exported files load into the app

Ideas to drop from PR 31:

- params-template scaffolding
- batch hand-edited JSON workflow
- extension manifest/menu integration
- Python-only implementation as the shipping feature

## 12. Risks

- **Image handling is new app-side infrastructure.** Mitigated by keeping v1 to image files only.
- **NF-vs-frequency changes runtime semantics.** Mitigated by explicit scalar fallback and focused amplifier tests.
- **UI complexity creep.** Mitigated by keeping v1 to one amplifier wizard, two curves, and manual clicking only.
- **Scope bleed into other component types.** Explicitly rejected; mixers remain scalar-only.
