---
type: Integration Guide
title: S-Parameter System
description: Traces Touchstone parsing and complex S-parameter application through component engines, with separate project and library path-containment, persistence, fallback, and failure contracts.
tags: [s-parameter, touchstone, rf-components, persistence, path-containment]
sources:
  - id: openwiki-source-3b7269741963097c808f1c17
    resource: repo://amplifier/src/amplifier_engine.cpp
  - id: openwiki-source-5f1fbd4979e8254a53e79f25
    resource: repo://app/src/app.cpp
  - id: openwiki-source-dd0234525c20fcc7f7d85a35
    resource: repo://app/src/component_library.cpp
  - id: openwiki-source-baedf8f3f47fa931244e3545
    resource: repo://app/src/project_serializer.cpp
  - id: openwiki-source-223db2b4571547f4d13aacdc
    resource: repo://attenuator/src/attenuator_engine.cpp
  - id: openwiki-source-534c675a5e29838359b89ced
    resource: repo://combiner/src/combiner_engine.cpp
  - id: openwiki-source-97bab2ebcadc7d70620f063a
    resource: repo://equalizer/src/equalizer_engine.cpp
  - id: openwiki-source-74d53cf0662e7948542d0f7d
    resource: repo://ideal_filter/src/ideal_filter_engine.cpp
  - id: openwiki-source-3103344e965706c3b7acb9cf
    resource: repo://tests/test_amplifier_sparam.cpp
  - id: openwiki-source-8fbec6a99b37874b41a0f8fc
    resource: repo://tests/test_issue117_numeric_correctness.cpp
  - id: openwiki-source-07edc275a69eebf347d2e840
    resource: repo://tests/test_issue79_component_validation.cpp
  - id: openwiki-source-8568eb9d2755f18e4ab13407
    resource: repo://tests/test_path_containment.cpp
  - id: openwiki-source-1e03788e225c3b8fd338e958
    resource: repo://touchstone/src/s_parameter_data.cpp
  - id: openwiki-source-13f3b5d657436db7924cde15
    resource: repo://touchstone/src/touchstone_parser.cpp
generated: { by: "omp", at: "2026-10-05T19:44:12.666Z" }
verified:
  - by: openwiki/0.7.0
    at: 2026-10-05T19:44:12.666Z
---

# S-Parameter System

Touchstone files provide frequency-dependent complex network data for compatible RF components. The shared `touchstone/` target parses and interpolates the data; each consuming engine owns its port mapping, tone/noise model, serialized mode, and fallback. Project files, library definitions, and direct file selection enter through different path boundaries.

## End-to-end path

<!-- openwiki: mermaid parse failed and this diagram was converted to a text fence so it does not break rendering. Fix the diagram source and restore the mermaid fence. Parser error: Heuristic: a semicolon inside a label breaks rendering; rephrase the label. -->
```text
flowchart TD
    A["Touchstone file"] --> B{"Load boundary"}
    B -->|Project .rfsim| C["Resolve under project directory"]
    B -->|Component library| D["Resolve under library JSON directory"]
    B -->|Direct component selection| E["Load selected path immediately"]
    C --> F["SParameterData::load"]
    D --> F
    E --> F
    F --> G{"Parser succeeds?"}
    G -->|no| H["Data stays unloaded; engine falls back"]
    G -->|yes| I["Hz grid + row-major complex matrix"]
    I --> J["Interpolate at tone/bin frequencies"]
    J --> K["Engine applies port mapping, phase, gain, and noise"]
    K --> L["Component Spectrum output"]
    M["Project save"] --> N["Relativize contained absolute paths"]
    N --> O["Reload repeats project-root containment"]
```

*A parsed file is not itself a component model: its consumer selects the network ports and applies its physical noise/fallback rules.*

## Parsing and shared data

`TouchstoneParser::parse(filepath)` returns `std::nullopt` when the file cannot be opened, has no option line, has invalid point cardinality, exceeds its safety bounds, or has invalid frequency values. The option line recognizes frequency units `Hz`, `kHz`, `MHz`, and `GHz`; parameter kinds `S`, `Y`, `Z`, `H`, and `G`; encodings `DB`, `MA`, and `RI`; and reference impedance. Frequencies are converted to Hz. The parser represents the declared parameter kind but does not convert Y/Z/H/G matrices into S-parameters for the component engines.

Port count is inferred from a `.sNp` suffix when possible, with a two-port fallback. Data rows are read in Touchstone column-major order and stored row-major; for a two-port device the stored order is `S11, S12, S21, S22`. The parser rejects negative, non-finite, repeated, or decreasing frequencies. It rejects files above 256 MiB before buffering and stops accumulating after 10,000,000 frequency points.

`SParameterData::load()` clears its previous matrix before parsing. A failed parse leaves it unloaded; a successful parse moves the Hz frequency vector and complex matrix into the owner. `interpolate(freq_Hz, param_idx)` linearly interpolates the complex value between adjacent frequencies and clamps out-of-range queries to the nearest endpoint. An unloaded object or invalid parameter index returns the neutral complex value `{1.0, 0.0}`.

`SParameterData::applyToSpectrum()` is the generic one-path helper: it copies the spectrum shape, changes tone power by `20*log10(|S|)`, adds `arg(S)` to phase, and scales noise PSD by `|S|²`. Components with a different passive thermal model can apply the same interpolation themselves and add their own noise term.

## Component engine ownership

The component type registry marks amplifier, ideal filter, equalizer, attenuator, and combiner as S-parameter-capable. Each engine stores a file path and mode in its own parameter JSON and chooses how matrix entries affect signal and noise:

- **Amplifier:** forward `S21` is indexed as `1 * numPorts + 0`. It applies magnitude and phase to tones and `|S21|²` to input noise. Its noise figure and nonlinear model remain amplifier behavior.
- **Ideal filter:** forward `S21` replaces the ideal passband decision. It does not interpret reverse isolation as a second graph path.
- **Equalizer:** forward `S21` replaces the ideal reference-gain/slope profile; input noise uses magnitude-squared gain and the equalizer adds no separate noise.
- **Attenuator:** applies S21 magnitude and phase to tones, then uses the passive model `noise_in * |S21|² + kT*(1 - |S21|²)` at 290 K. Manual mode remains available.
- **Combiner:** requires a three-port file. It maps input 0 through S21 and input 1 through S31; its passive thermal residual is clamped at zero. A two-port file does not enter this branch and manual combining remains available.

`AmplifierEngine`, `IdealFilterEngine`, `EqualizerEngine`, and `AttenuatorEngine` set S-parameter mode when a selected file loads successfully. The combiner has a separate inspector mode toggle that can be selected before a file is chosen; `update()` uses its S-parameter path only when the loaded file has three ports. This keeps the file picker reachable while preventing missing or wrong-port-count data from becoming an active model.

On project deserialization, compatible engines reload their saved file path. They enable the persisted mode only when parsing succeeds; the combiner also requires three ports. A missing, malformed, or containment-rejected file therefore leaves the component in its ordinary model. A successful direct file selection dirties the engine so the next update recomputes its output.

## Project-file path boundary

For `.rfsim` load, `ProjectSerializer` resolves both `sparam_filepath` and the legacy `sparam_path` against the project file's directory. It rejects any path with a `..` path component, canonicalizes with `weakly_canonical`, and verifies path components remain under that directory. Escaping or unresolvable values are replaced with an empty string before engine deserialization. Relative in-project files are resolved against the project directory, not the process working directory.

On save, an absolute S-parameter path that remains inside the project directory is rewritten relative to the project. Already-relative paths remain relative. An external file selected directly in the current session can load immediately, but save leaves its outside-root absolute path unchanged; a later project reload neutralizes it. Copy the file into the project directory and use a relative reference when the project must reopen portably.

`tests/test_path_containment.cpp` proves that external absolute paths and `..` paths are not loaded from a project, a contained relative path is resolved from the project directory, and a contained absolute path is saved as a relative path that reloads successfully.

## Component-library path and authoring boundaries

A library JSON is the root for both S-parameter path-bearing parameters and `data_files`. Relative paths resolve from the JSON's directory; absolute contained paths are accepted; parent traversal, outside-root, and unresolvable paths are skipped or removed. The canonical path is passed to the engine only after containment checks. If an S-parameter cannot be loaded, the instance remains available in its manual/ideal fallback model. `ComponentLibrary` validates definitions at load/upsert/instantiate boundaries and removes a just-created engine if deserialization or metadata application fails.

The component-authoring form copies a selected S-parameter beside the library JSON. Its destination must be a bare filename: separators, roots, drive prefixes, `.` and `..` are refused. Manufacturer and part-number path segments are sanitized before composition, and an unsafe name or failed copy aborts the save. Editing an existing library component writes beside that entry rather than redirecting it to a newly selected root.

Built-in component data discovery is separate from project/library containment. The app searches user and project library roots, then prefers `<exe_dir>/component_data/library` for installed builds and falls back to the source-tree-relative library location only when the executable-relative location is absent.

## Limits and failure behavior

The parser's 256 MiB file cap runs before it reads file contents; its 10,000,000-point cap is checked during numeric accumulation as well as after parsing. Invalid option/data shape, unreadable files, absent option lines, invalid frequency sequences, and cap violations return no data instead of a partially loaded matrix. `SParameterData::load()` has already cleared any prior matrix on failure, and engine setters/deserializers gate S-parameter mode on successful loading.

Project and library containment use component-wise canonical path comparisons, not string-prefix tests. The project load path neutralizes rejected references but continues restoring the component with its non-file model. The library path skips an escaping asset; it does not open a decoy file outside the JSON's directory. A schema-valid library entry can still fail during engine deserialization, in which case instantiation rolls back the engine and graph node.

## Focused verification

- `tests/test_touchstone.cpp` covers real and synthetic one-/two-port data, DB/MA decoding, frequency units, row-major parameter values, missing files, and missing option lines.
- `tests/test_amplifier_sparam.cpp` covers ideal fallback, S21 gain/phase, interpolation, out-of-band endpoint behavior, amplifier NF, nonlinearity, and failed file selection.
- `tests/test_path_containment.cpp` exercises project path neutralization/relativization, library `data_files`, the 256 MiB parser guard, and sanitized authoring-copy names and destinations.
- `tests/test_issue79_component_validation.cpp` covers library type/range validation, revalidation at upsert/instantiate, deserialization rollback, and contained versus escaping S-parameter path parameters.
- `tests/test_combiner.cpp` and `tests/test_issue117_numeric_correctness.cpp` cover three-port mapping, the two-port fallback, mode selection before file choice, and passive-noise clamping.

## Source map

- `touchstone/include/touchstone_parser.h` and `touchstone/src/touchstone_parser.cpp`: format parsing, validation, file/point limits, and matrix ordering.
- `touchstone/include/s_parameter_data.h` and `touchstone/src/s_parameter_data.cpp`: loaded state, interpolation, endpoint behavior, and generic spectrum application.
- `amplifier/src/amplifier_engine.cpp`, `ideal_filter/src/ideal_filter_engine.cpp`, `equalizer/src/equalizer_engine.cpp`, `attenuator/src/attenuator_engine.cpp`, and `combiner/src/combiner_engine.cpp`: component mode branches, port mappings, noise, and fallback behavior.
- `app/src/project_serializer.cpp`: project-root containment and relative save paths.
- `app/src/component_library.cpp`, `app/src/component_form_model.cpp`, and `app/src/app.cpp`: library-root resolution, validation/rollback, and authoring file copies.
- `tests/test_touchstone.cpp`, `tests/test_amplifier_sparam.cpp`, `tests/test_issue79_component_validation.cpp`, and `tests/test_path_containment.cpp`: focused parser, engine, library, and containment regressions.
