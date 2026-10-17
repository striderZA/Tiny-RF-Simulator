# Tiny RF Simulator

Tiny RF Simulator is a desktop tool for building RF signal chains and reading their spectra. Place components on a canvas, connect their pins, probe any node, and read the spectrum, power, and network measurements as the chain changes.

This manual is for people who run the simulator. It covers the Windows and Linux release archives. Developer material, such as building from source and the contribution rules, is in the [repository's CONTRIBUTING.md](https://github.com/striderZA/Tiny-RF-Simulator/blob/master/CONTRIBUTING.md).

## Project status

With a saved project open, the menu bar shows its filename and the project's current Git commit and working-tree counts. Hover over the Git summary to see the branch and full counts for staged, unstaged, untracked, and conflicted paths. For an untitled project, status is read from the simulator's current working directory. If Git or a repository is unavailable, the header shows `Git unavailable`.

## Start here

- [Install](install.md): download the release archive and run the simulator.
- [Guided tutorial](tutorial.md): build and probe your first signal chain.
- [Agent (MCP)](agent.md): connect an MCP client and move components in the open project.

## Build and measure

- [Components and ports](circuits/components-and-ports.md): place components and connect their ports.
- [Spectrum Analyzer](measurements/spectrum.md): read the spectrum of a probed signal.

## Release notes

Changes for each version are listed in the [CHANGELOG on GitHub](https://github.com/striderZA/Tiny-RF-Simulator/blob/master/CHANGELOG.md).
