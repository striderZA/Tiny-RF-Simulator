---
type: Integration Guide
title: S-Parameter System
description: Traces Touchstone parsing and complex S-parameter application through the five S-parameter-capable component engines, the component-form picker, library and project data-file resolution, path containment, and the read-only data_file_read agent tool.
tags: [s-parameter, touchstone, rf-components, persistence, path-containment, data-file-read]
sources:
  - id: openwiki-source-5463a01543f8384d18b05f52
    resource: repo://agent/api/src/tool_data_file.cpp
  - id: openwiki-source-ea4d2883fe61311539ee1809
    resource: repo://agent/protocol/src/agent_catalog.cpp
  - id: openwiki-source-3b7269741963097c808f1c17
    resource: repo://amplifier/src/amplifier_engine.cpp
  - id: openwiki-source-c26268b659d2e081b2bf2494
    resource: repo://app/include/component_form_model.h
  - id: openwiki-source-78878acfa0447c3372e0ed0d
    resource: repo://app/include/component_library.h
  - id: openwiki-source-5f1fbd4979e8254a53e79f25
    resource: repo://app/src/app.cpp
  - id: openwiki-source-7521912f4cd608ba12b28d9b
    resource: repo://app/src/component_form_model.cpp
  - id: openwiki-source-b4bcfb66a97653e4a88182fb
    resource: repo://app/src/component_form_widget.cpp
  - id: openwiki-source-dd0234525c20fcc7f7d85a35
    resource: repo://app/src/component_library.cpp
  - id: openwiki-source-12d90ca6eb6eefd9b169f36a
    resource: repo://app/src/component_type_registry.cpp
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
  - id: openwiki-source-f87d3ee6c75c1ccc4ec8d422
    resource: repo://touchstone/include/s_parameter_data.h
  - id: openwiki-source-1e03788e225c3b8fd338e958
    resource: repo://touchstone/src/s_parameter_data.cpp
  - id: openwiki-source-13f3b5d657436db7924cde15
    resource: repo://touchstone/src/touchstone_parser.cpp
generated: { by: "omp", at: "2026-10-10T18:57:17.727Z" }
verified:
  - by: openwiki/0.7.0
    at: 2026-10-10T18:57:17.727Z
---

# S-Parameter System

Five component types can replace their ideal model with measured complex data from a Touchstone file: amplifier, attenuator, ideal filter, equalizer, and combiner. A file reaches an engine through three routes: the component form, which copies it beside the library JSON on save; library JSON `data_files` and path-bearing parameters; and project files, which store `sparam_filepath`. The `data_file_read` agent tool summarizes these files without changing them.

## Parsing

TouchstoneParser supports DB, MA, and RI encodings, converts supported frequency units to Hz, infers port count from .sNp filenames, reorders Touchstone column-major parameters into row-major storage, validates strictly increasing finite nonnegative frequencies, and enforces 256 MiB and 10,000,000-point limits while reading.

## SParameterData and engine application

SParameterData clears prior state before parsing, reports load failure without becoming loaded, linearly interpolates complex parameters with endpoint clamping, and applies magnitude/phase and |S|² noise scaling to spectrum data.

Each S-parameter-capable engine applies its data inside its own update() by calling SParameterData::interpolate() and scaling tones and noise itself rather than through applyToSpectrum(); the amplifier, equalizer, and ideal filter apply their selected forward entry, the amplifier also keeps its noise figure and nonlinear processing, the attenuator adds a passive thermal noise term k*T*(1-|S21|^2), the combiner applies S21 to input 0 and S31 to input 1, and each engine falls back to its manual model whenever its S-parameter branch is inactive.

## Activation and persisted keys

S-parameter-capable engine deserializers reload the persisted file path before enabling a requested S-parameter mode, and activation requires a successful parse plus enough ports: at least two for the amplifier, attenuator, equalizer, and ideal filter and exactly three for the combiner, so missing, malformed, or too-small data leaves the branch disabled.

The amplifier, equalizer, and ideal filter persist their forward entry as sparam_fwd_idx, labelled Forward S-Parameter Index in the component type registry; loading a file through setSParamFilepath selects the S21 entry, while deserialize reads a missing sparam_fwd_idx as index 0 (S11).

Only the attenuator and combiner deserializers fall back to the legacy sparam_path key; the amplifier, equalizer, and ideal filter read sparam_filepath alone. Project load and library instantiate resolve both keys in place under their original names, so a legacy-only sparam_path value is never copied into sparam_filepath, and those three engines fall back to their manual model.

## Project files

Project load resolves persisted sparam_filepath and legacy sparam_path values relative to the project directory, rejects parent traversal or weakly-canonical escapes by blanking them before engine deserialization, and save rewrites only contained absolute paths to relative; an external absolute path is therefore neutralized on a later reload.

## Library data

ComponentLibrary validates definitions at loadFile, upsert, and instantiate boundaries and drops or refuses any definition that carries validation issues; instantiate resolves path-bearing S-parameter parameters and s_parameters data files under the library JSON's directory, skips escaping or unresolvable entries, and rolls back a newly created engine when deserialization or part-number assignment throws.

componentDataFilePath decides by the first s_parameters entry alone and returns nullopt when that entry escapes the JSON directory, whereas library instantiate skips an escaping s_parameters entry and loads the next contained one, so the two disagree for a library whose first entry escapes and a later entry is contained.

## Component form and authoring

The component form shows an S-parameter File row with a Browse... control for each descriptor that sets supports_sparam_file, which covers the amplifier, attenuator, ideal filter, equalizer, and combiner, and saving copies the selected file beside the authored JSON as an s_parameters data_files entry named from the sanitized part number and the source extension.

Component authoring builds the manufacturer directory, the JSON name, and the copied S-parameter file name from sanitizePathSegment(), which keeps only letters, digits, hyphen, underscore, and space; the copy destination comes from dataFileCopyDestination(), which refuses any name that is not a bare file name, and a failed staging copy removes the staged temporaries and aborts the save.

## data_file_read

data_file_read summarizes one S-parameter file without accepting any file path or writing anything: the component route reads the component's stored absolute sparam_filepath after an epoch check, the library route reads the part's first s_parameters entry with no epoch check and refuses with NOT_FOUND when that entry escapes or is missing, exactly one of component or part_number is accepted, and the result reports the file's base name and up to max_points evenly spaced samples of one S(row,col) entry.

## Built-in libraries

Built-in component libraries prefer the executable-relative component_data/library location for installed binaries and fall back to the source-tree-relative component_data/library path, separately from user and project library roots.

## Tests

- `tests/test_path_containment.cpp`: project load and save of S-parameter paths, library containment, `componentDataFilePath()`, and authoring names.
- `tests/test_issue79_component_validation.cpp`: library validation, instantiate rollback, and path-bearing parameter containment.
- `tests/test_amplifier_sparam.cpp` and `tests/test_issue117_numeric_correctness.cpp`: engine S-parameter activation and combiner port checks.
- `tests/test_agent_api.cpp`: `[data_file_read]` cases.

## Related pages

- [Architecture overview](../architecture/overview.md)
- [RF components](../domains/rf-components.md)
- [Agent tool calls](../workflows/agent-tool-calls.md)
- [DSP pipeline](../workflows/dsp-pipeline.md)
- [Testing guidance](../testing/guidance.md)
