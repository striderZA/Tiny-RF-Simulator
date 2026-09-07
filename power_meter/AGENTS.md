# power_meter — AGENTS.md

## Purpose

Own the UI-independent total-power measurement engine and its optional ImGui instrument panel.

## Ownership

- `power_meter_engine.h/.cpp` — validates `Spectrum` input and reports instantaneous total tone-plus-noise power in dBm.
- `power_meter_widget.h/.cpp` — selects a live graph output pin and renders the measurement; it does not perform DSP or persist source selection.
- `CMakeLists.txt` — exposes `simulator::power_meter_engine` and `simulator::power_meter_widget`.

## Local Contracts

- The engine depends only on `Spectrum`; it must not include ImGui, graph, or app headers.
- Tone powers are summed linearly; real-domain tone mirroring is not applied.
- `noise_total_W` is W/Hz on a finite, increasing, uniformly spaced frequency grid; an empty noise vector means zero noise.
- Invalid or missing inputs return `PowerMeasurement::valid == false`, a `PowerMeterError`, and NaN dBm.
- The meter is an observer instrument, not an `IComponentEngine`, registry entry, graph node, or project-serialized source.

## Work Guidance

- Keep measurement behavior in standalone Catch2 tests under `tests/test_power_meter.cpp`.
- Keep UI changes in the widget and app integration; do not add measurement concerns to existing analyzers.

## Verification

- Build and run `test_power_meter` through CTest.
- Build the `power_meter_widget` and `app` targets after UI changes.

## Child DOX Index

No child docs. The module is flat.
