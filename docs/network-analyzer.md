# Network Analyzer

The Network Analyzer measures the gain and noise figure of the chain between two points you pick. Open it from **View > Network Analyzer**. The panel is a dockable window; the View item toggles it, and its visibility persists between runs.

The analyzer does not read the live signal. It builds an isolated copy of the chain between your two points and drives it with its own stimulus, so the generator's configured tones do not affect the reading, and measuring never disturbs the live circuit.

## Set the measurement points

Two lists pick the points. Each lists the output pins of every node in the graph, as `<node label> OUT1`, `OUT2`, and so on.

- **Point A (Reference)** is where the analyzer injects its stimulus. The component at A and everything upstream of it is *not* part of the measurement — the stimulus replaces that output.
- **Point B (Measured)** is the output port read as the response. For a multi-output block, pick the exact port.

A and B must sit on two different components in a forward chain. A and B on the same component, or no path from A to B, gives no data. Leaving either point on `(none)` empties the plot and the averages read **no data**.

## Set the sweep

| Control | Range | Default |
| --- | --- | --- |
| **Start Freq (Hz)** | 0 – 20 GHz | 1 GHz |
| **Stop Freq (Hz)** | 0 – 20 GHz | 6 GHz |
| **Points** | 2 – 2001 | 201 |
| **Stimulus Power (dBm)** | −60 – +10 dBm | −30 dBm |

The sweep is linear from start to stop. Each sweep point is measured at the same stimulus power. The default −30 dBm is the small-signal choice: a nonlinear component reads a compressed gain at higher stimulus levels.

Sweep and point edits mark the project dirty and are one undo step, labelled **Set analyzer points** in the **Edit** menu. The points and sweep are saved with the project.

## Read the results

The plot **Gain / Noise Figure vs Frequency** shows two traces: **Gain (dB)** and **Noise Figure (dB)**. The axes reframe to the current data every frame. Below the plot, **Avg Gain** and **Avg NF** show the mean of the finite points over the sweep, or **no data** when no point produced a value.

Gain at a point is the response tone at that frequency minus the stimulus power; a point more than 100 dB down is treated as no data. Noise figure is the output noise referred back to the input against thermal noise (kT at 290 K) through the measured gain.

Points with no data are absent from the traces and skipped by the averages. The panel shows no per-point reason. No data happens when:

- there is no forward path from A to B, or the topology is unsupported — for example a combiner inside the measured chain, a cycle, or a second live source feeding the chain;
- an input pin carries two links;
- no tone arrives at B at that frequency, or it is more than 100 dB below the stimulus;
- the point is outside a Touchstone file's frequency band. The Amplifier, Attenuator, Equalizer, and Ideal Filter hold the edge value outside the band — that held value still sets the live signal — but the analyzer reads such points as no data. The inspector shows each file's band.

The panel has no markers, hold, or single-shot controls; it recomputes live while it is visible.

## Receiver requirements use this sweep

The [Receiver Requirements](receiver-requirements.md) panel reuses the Network Analyzer's points and sweep. Each requirement mode is judged only when this sweep covers the mode's whole band with at least one in-band point and no in-band no-data gaps — otherwise the mode reads **Incomplete**. If a mode reads Incomplete, widen the analyzer's start and stop frequencies.
