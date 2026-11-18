# Power Meter

The Power Meter reads the total power at one node. Open it from **View > Power Meter**. The panel is a dockable window; the View item toggles it, and its visibility persists between runs.

## Pick a source

The panel has one control: the **Source** list. It shows the output pins of every node that currently carries a signal, labelled `<node label> OUT` for the first output and `OUT2`, `OUT3`, and so on for later outputs. Pick the pin to read. The choice lasts for the session; it is not saved in the project.

The panel recomputes while it is visible. There is no averaging, hold, or span setting here.

## Read the total

**Total Power: X dBm**, to two decimals. The total is the tone power plus the noise integrated over the source's whole frequency grid, so a wider grid or coarser bins read more integrated noise. Exactly zero power reads `Total Power: -inf dBm`.

Tones at the same frequency add as phasors: two in-phase copies of a 0 dBm tone read +6.02 dBm, two 90° apart read +3.01 dBm, and two 180° apart cancel to −inf dBm. Tones at different frequencies add as power: two 0 dBm tones read 3.01 dBm.

## Unavailable

The readout is replaced by **`Unavailable: <reason>`** when:

- no source is selected, or the picked pin no longer exists;
- the source's frequency grid is invalid — fewer than two bins, or uneven or non-increasing spacing;
- the source's noise data is invalid, for example a negative density;
- the source's tone data is invalid, for example a non-finite power.

## Lifecycle

**New**, a successful **Open**, and an agent checkpoint revert clear the source choice. Swapping a placed library part remaps the source to the successor part's matching pin; see [Component Library](component-library.md).
