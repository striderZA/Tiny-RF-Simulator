# RF Simulator Roadmap

> Feature tracking for the RF Simulator project.

| # | Feature | Status | Notes |
|---|---------|--------|-------|
| 1 | **Data-file component descriptions** — describe components with S-parameter / nonlinear data files; built-in utility to plot loaded data file parameters | ✅ Completed | N-port Touchstone parser (1-4 port), row-major reorder, complex interpolation, phase rotation via `std::arg`, forward param combo, multi-trace S-param plot with 16-color palette, native file browser |
| 2 | **Spectrum analyzer enhancements** — more functionality in the spectrum analyzer view | 📋 Planned | |
| 3 | **RF-accurate node-graph components** — improved RF representation in the node graph | 📋 Planned | |
| 4 | **Multi-port components** — support components with >2 ports, multiple signal paths | ✅ Completed | Data structures refactored (SignalNode/GraphNode vectors); Splitter (1→2 ports, −3dB) implemented and tested |
| 5 | **Digital chain** — polyphase filter bank + digital downconversion | 📋 Planned | |
| 6 | **Time domain view** — add time-domain visualization | 📋 Planned | |
| 7 | **Pulsed signal generation** — pulse-generation capability | 📋 Planned | |
| 8 | **Spectrum phase** — add phase member to the `Spectrum` class | ✅ Completed | `phase_deg` on `Tone` + per-bin vector, GUI column, propagated through amplifier |
| 9 | **Node tooltips** — tooltips showing noise level, power level, center frequency, etc. | ✅ Completed | Per-pin hover tooltip: tone count + strongest tone, noise floor, freq range; MHz-only display |
| 10 | **Frequency conversion** — mixer components | ✅ Completed | Internal LO, sum+difference sidebands, editable LO freq + conv gain, noise scaled, phase preserved |

## Status Key

| Icon | Meaning |
|------|---------|
| 📋 Planned | Not yet started, on the horizon |
| 🎯 In Design | Being designed / spec written |
| 🔄 In Progress | Active implementation |
| ✅ Completed | Implemented and tested |
