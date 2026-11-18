# Tiny RF Simulator — User Documentation

Tiny RF Simulator is a desktop tool for building RF signal chains and reading their spectra. Place components on a canvas, connect their pins, probe any node, and read the spectrum, power, and network measurements as the chain changes.

This documentation is for people who run the simulator. It covers the Windows and Linux release archives. Developer material, such as building from source and the contribution rules, is in the [repository's CONTRIBUTING.md](../CONTRIBUTING.md).

## Start here

- [Install](install.md) — download the release archive, run the simulator, and know which files the app writes.
- [Guided tutorial](tutorial.md) — build and probe your first signal chain, then meet the panels that check, budget, and automate a design.
- [Utility Views](utility-views.md) — the second docking group beside the main window: which panels open there, how to move views between the groups, and how the layout persists.

## Editing the circuit

- [Node editing](node-editing.md) — navigate the canvas, add and connect components, align, distribute, and snap to grid.
- [Undo and redo](undo-redo.md) — what Ctrl+Z covers, how far back it reaches, and what the **Edit** menu names.

## Instruments

- [Spectrum Analyzer](spectrum-analyzer.md) — probe nodes and read live spectra with RBW, hold, and averaging controls.
- [Network Analyzer](network-analyzer.md) — measure gain and noise figure between two points in the chain.
- [Power Meter](power-meter.md) — read total power at any node.

## Receiver design

- [Receiver Requirements](receiver-requirements.md) — define band, gain, noise-figure, output-power, and IIP3 limits per mode, and see pass, fail, or incomplete verdicts.
- [RF Budget](rf-budget.md) — build a stage table and predict each mode's cascade before it is wired.

## Parts and automation

- [Component Library](component-library.md) — browse parts, author reusable parts, refresh, and swap a placed part in place.
- [Amplifier](amplifier.md) — gain, noise figure, and distortion from OIP2/OIP3 intercept points or amp-f files.
- [Test Flows](test-flows.md) — author parameter sweeps, capture measurements, and export results as JSON.
- [Agent (MCP)](agent-mcp.md) — connect an MCP client and let it read, edit, measure, and automate the open project.

## Project status

With a saved project open, the menu bar shows its filename and the project's current Git commit and working-tree counts. Hover over the Git summary to see the branch and full counts for staged, unstaged, untracked, and conflicted paths. For an untitled project, status is read from the simulator's current working directory. If Git or a repository is unavailable, the header shows `Git unavailable`.

The right edge of the menu bar shows the build you are running: the version and the short commit it was built from, for example `0.31.0 (9e8ded4)`. On a narrow window the project filename and Git summary shorten so that stamp stays visible.

## Release notes

Changes for each version are listed in the [CHANGELOG](../CHANGELOG.md).

## Engineering references

Reference material for file formats and models, aimed at preparing data for the simulator:

| Document | Description |
|----------|-------------|
| [ampf_v1_spec.md](resources/ampf_v1_spec.md) | amp-f v1 file format spec — parser reference for `.ampf` intercept files |
| [amplifier_nonlinear_model.md](resources/amplifier_nonlinear_model.md) | Nonlinear amplifier model (gain compression, harmonics, IMD) — engineering reference for the amplifier module |
| [pfb_channelizer_info.md](resources/pfb_channelizer_info.md) | Polyphase filter bank channelizer — theory, design notes, and the simulator's 1x/2x oversampling contract |
| [rf_adc_info.md](resources/rf_adc_info.md) | RF ADC software twin — semi-idealized sampling model (aliasing, Nyquist zones, NSD) |
| [touchstone_v2_parser_spec.md](resources/touchstone_v2_parser_spec.md) | Touchstone v2.0 file format spec — parser reference for `.s2p`/`.s3p`/`.s4p` support |
