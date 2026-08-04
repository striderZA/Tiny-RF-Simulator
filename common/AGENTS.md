# common — AGENTS.md

## Purpose

Own the header-only data model shared by all RF Simulator modules: `SignalNode`, `Spectrum`, `IComponentEngine`, `ViewManager`, `Group`, `GroupBoundaryPin`, and the math utilities in `common.h`.

## Ownership

- `common/common.h` — `MIN_FREQ`, `MAX_FREQ`, `MIN_POWER`, `MAX_POWER`, `DEFAULT_VBW`, `DEFAULT_RBW`, physical constants (`k`, `T`, `R`), `dbToLinear`, `calculateNoiseTemp`, `addedNoiseDensity_W_per_Hz`, `addedNoisePerBin_W`, `buildDefaultFrequencyGrid`
- `common/signal_node.h` — `SignalNode` (input + output spectra + view_enabled)
- `common/spectrum.h` — `Spectrum` (frequencies, tones, noise vectors, phase, generation counter, `fs_Hz`, `is_complex_baseband`) and `Peak`; also the free helper `conjugateSymmetricExpand()` for expanding real-domain tones into +-fc conjugate-symmetric pairs
- `common/component_interface.h` — `IComponentEngine` (DSP engine contract)
- `common/view_manager.h` — `ViewManager` (registry of `SignalNode*`)
- `common/include/group.h` — `Group` and `GroupBoundaryPin` (subcircuit grouping data)
- `common/iq_stream.h` — `IQStream` (used by the digital chain)
- `common/nonlinear_model.h` — Amplifier nonlinear model helpers
- `common/session_state.h` — Windows app.ini read/write
- `common/CMakeLists.txt` — `simulator::common` INTERFACE library exposing all of the above

## Local Contracts

- All headers are `pragma once`; the directory forms a single `simulator::common` INTERFACE CMake target.
- Engines (in other modules) include `signal_node.h` and `component_interface.h`. Widgets additionally include nothing from `common/` directly; they receive `SignalNode&` references via `IComponentEngine`.
- `Group` is consumed by `NodeGraphEngine` and `NodeGraphWidget`. It is *not* consumed by any DSP engine — groups are a visual layer.

## Work Guidance

- Changes to `SignalNode` or `Spectrum` affect every engine. Update all engines' `update()` and tests.
- `Spectrum::is_complex_baseband` (default `false`) marks spectra downstream of an ADC's DDC (complex baseband/IQ); every pass-through engine propagates it from its input exactly like `fs_Hz`. Only `AdcEngine`'s output sets it to `true`. `conjugateSymmetricExpand()` must stay render-only (used by the spectrum-analyzer render path for real-domain spectra) — never call it from interior DSP (generator, `nonlinear_model.h`, gain/filter/S-param stages, mixer), which must keep operating on the collapsed single-entry-per-tone representation.
- New fields on `IComponentEngine` must keep a default implementation that preserves backward compat for all existing engines.
- New files in `common/` or `common/include/` are automatically picked up by `common/CMakeLists.txt`'s glob.

## Verification

- `cmake --build build && ctest --test-dir build` must pass with zero failures.
- All existing engines must still compile against modified `IComponentEngine`.

## Child DOX Index

No child docs. `common/` is a flat directory.
