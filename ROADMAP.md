# RF Simulator Roadmap

> Feature tracking for the RF Simulator project.

| # | Feature | Status | Notes |
|---|---------|--------|-------|
| 1 | **Data-file component descriptions** — describe components with S-parameter / nonlinear data files; built-in utility to plot loaded data file parameters | ✅ Completed | N-port Touchstone parser (1-4 port), row-major reorder, complex interpolation, phase rotation via `std::arg`, forward param combo, multi-trace S-param plot with 16-color palette, native file browser |
| 2 | **Spectrum analyzer enhancements** — more functionality in the spectrum analyzer view | 📋 Planned | |
| 3 | **RF-accurate node-graph components** — improved RF representation in the node graph | 📋 Planned | |
| 4 | **Multi-port components** — support components with >2 ports, multiple signal paths | ✅ Completed | Data structures refactored (SignalNode/GraphNode vectors); Splitter (1→2 ports, −3dB) implemented and tested |
| 5 | **Digital chain** — polyphase filter bank + digital downconversion | ✅ Completed | RF ADC (frequency-domain sampler, Nyquist zone aliasing, NSD noise, IQStream) + PFB channelizer (M channels, K taps, engine + widget, 8 tests + benchmarks) |
| 6 | **Time domain view** — proper oscilloscope-style time-domain visualization | 📋 Planned | |
| 7 | **Pulsed signal generation** — pulse-generation capability | 📋 Planned | |
| 8 | **Spectrum phase** — add phase member to the `Spectrum` class | ✅ Completed | `phase_deg` on `Tone` + per-bin vector, GUI column, propagated through amplifier |
| 9 | **Node tooltips** — tooltips showing noise level, power level, center frequency, etc. | ✅ Completed | Per-pin hover tooltip: tone count + strongest tone, noise floor, freq range; MHz-only display |
| 10 | **Frequency conversion** — mixer components | ✅ Completed | Internal LO, sum+difference sidebands, editable LO freq + conv gain, noise scaled, phase preserved |
| 11 | **Subcircuit groups** — expandable/collapsible node groups with synthesized input/output pins for navigating large circuits | ✅ Completed | Snapshot editing model; visual layer only, DSP graph stays flat; engine + widget + inspector integration |
| 12 | **Attenuator component** — passive attenuator with manual dB control and S-parameter mode | ✅ Completed | Physically accurate noise model (NF = atten), Touchstone S-param support, zigzag schematic symbol, inspector panel, 10 unit tests |
| 13 | **Combiner component** — 2-input → 1-output passive RF combiner with Wilkinson model and S-parameter mode | ✅ Completed | Exact dual of splitter (-3 dB per input), coherent signal combination, 3-port Touchstone S-param support, Y-shaped schematic symbol, inspector panel with mode toggle, 6 unit tests |
| 14 | **IQ plot UX: scroll mode + fixed time/div + zoom** — fix time-domain display to use scroll mode with configurable time window | 📋 Planned (v0.8.0) | Replace growing buffer with fixed-size ring buffer, add time/div control and zoom, stable y-axis. Prerequisite for PFB improvements. |
| 15 | **Component library manager** — file-based library browser with global and per-project libraries | 📋 Planned (v0.8.0) | Users organize S-param files and component configs into named collections, browse from UI, one-click insert. Global (~/.rf-sim/libraries/) and per-project (./rf-sim-libraries/) libraries. |
| 16 | **PFB channelizer improvements** — multi-channel output, per-channel filtering, decimation | 📋 Planned (v0.9.0) | Expose all M channels as separate output pins, per-channel digital filtering, decimation output at Fs/M, optional zoom FFT mode. |
| 17 | **Library enhancements** — search/indexing, metadata extraction, real-world part database | 📋 Planned (v0.9.0) | Add search/filter to library browser, metadata extraction from S-param files, possibly integrate curated real-world component database. |
| 18 | **Plugin system** — extensible component architecture | 📋 Planned (future) | Allow users to extend simulator with custom components. TBD design: C++ SDK, embedded scripting (Lua/Python), or config-driven. Revisit after library manager. |
| 19 | **Modulation components** — AM, FM, PM, and digital modulations (QAM, PSK, OFDM) | 📋 Planned (future) | Fundamental RF building blocks for communication system simulation. |
| 20 | **Measurement instruments** — power meter, phase noise analyzer, SNR/THD/SFDR meters | 📋 Planned (future) | Signal quality metrics and advanced measurement tools. |

## Status Key

| Icon | Meaning |
|------|---------|
| 📋 Planned | Not yet started, on the horizon |
| 📋 Planned (vX.Y.Z) | Scoped for a specific release |
| 🎯 In Design | Being designed / spec written |
| 🔄 In Progress | Active implementation |
| ✅ Completed | Implemented and tested |
