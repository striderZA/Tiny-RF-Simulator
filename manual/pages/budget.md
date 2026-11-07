# Budget Calculator

The Budget panel predicts a receiver chain from library parts, before you build or simulate it. Open it with **View > Budget**.

It builds a cascade in order and plots gain, noise figure, output power, and IIP3 against frequency. Each plot shows your receiver requirements, the predicted cascade, and the live simulation together, so you can compare a design with what the circuit actually does.

## The three layers

Each plot draws three things:

- **Requirement limits** — red dashed horizontal segments, drawn only over the requirement band. They come from the Receiver Requirements panel (**View > Receiver Requirements**).
- **Budget** — a solid blue line: the predicted cascade of the library parts in the table.
- **Simulation** — a green short-dashed line: the live circuit measured by the Network Analyzer.

Gain and noise figure come from the Network Analyzer alone, so they need only **Point A**, **Point B**, and the sweep. Output power and IIP3 come from the receiver measurement instead, so their simulation curves need the matching limit in Receiver Requirements: an output-power limit with an applied reference tone, or an IIP3 limit with a two-tone measurement condition. Without that, that series is empty.

The plots use the Network Analyzer's sweep as their frequency axis, so configure **Point A** and **Point B** and the sweep there. Without a sweep, the panel shows **Simulation unavailable** and draws no plot.

Next to each plot title, the panel shows two statuses against the requirements:

- **`Budget: <status>`** — the predicted cascade.
- **`Simulation: <status>`** — the live circuit.

Each status is **Pass**, **Fail**, **Incomplete**, **Not configured**, or **Invalid configuration**. When no requirements are set, the panel shows **Configure receiver requirements** in place of both. Below the toolbar, the panel repeats the overall result as **`Budget: <status>`** and **`Simulation: <status>`**. A metric with missing values is **Incomplete**, so the overall status is the least favourable of the configured metrics.

A yellow vertical line marks the cursor and is shared by all four plots.

## Adding library parts

1. Click **Add part**.
2. Type in the filter box to match a part number, manufacturer, description, or type.
3. Click a part to append it to the end of the chain.

Only part types the budget can model are listed: amplifiers, attenuators, filters, equalizers, and coax. Generators and other types do not appear. A part backed by data files is prefixed with `[DATA]`; its manufacturer follows the part number after `--`.

Use **Remove**, **Up**, and **Down** on the selected row, or **Clear** to empty the chain. Stage order is the top-to-bottom order of the table.

Each row is a snapshot of the library definition. Editing the library afterwards does not change it, and saving the project keeps the snapshot. Only the picker reads the library.

## The table

Values in the table are read at the cursor:

- **Cursor (Hz)** in the toolbar sets the cursor. It snaps to the nearest sweep point.
- **Input power (dBm)** sets the power used for the output-power prediction. It affects output power only.

The first columns give each stage's own values at the cursor: **Gain (dB)**, **NF (dB)**, **OIP3 (dBm)**, and **P1dB (dBm)**. The `Cum.` columns show the chain ending at that row, not the whole chain: `Cum. gain`, `Cum. NF`, `Cum. OIP3`, and `Cum. P1dB` for stages 1 through that row.

The **OIP3** and **P1dB** columns, and the `Cum. OIP3` and `Cum. P1dB` columns, are output-referred. The **IIP3** plot and its requirement check are input-referred, so the IIP3 curve sits below the table's `Cum. OIP3` by the cascade gain.

A cell with no usable value shows `--`. A stage that cannot be evaluated is shown in red with the reason after `--`, and its row contributes no values.

## The plots

The **Predicted performance** section appears once the chain has at least one stage:

- **Gain (dB)** against frequency.
- **Noise figure (dB)** against frequency.
- **Output power (dBm)** against frequency, from the input power plus the cascade gain.
- **IIP3 (dBm)** against frequency, input-referred.

## Cascade assumptions

The prediction is a classic cascade calculation, not a full simulation:

- Matched 50 Ω ports.
- Unilateral stages: no reverse isolation or mismatch loss.
- A 290 K source.
- Noise figure follows Friis; gain adds in dB; IIP3 and P1dB combine as reciprocal powers.
- P1dB uses the standard small-signal approximation, referred to each stage's input, and is output-referred in the table.

A frequency point is left out when any stage is out of band there, or its gain or noise figure is not finite, or its linear gain is zero or negative. Curves then have a gap at that point. The requirement evaluation treats any invalid in-band point as missing, so a budget whose data band is narrower than the requirement band reports Incomplete for that metric.

## Limits in this version

- **No mixers.** The cascade has no frequency conversion, so a mixer cannot be a stage.
- **Snapshot, not a mirror.** The chain is a copy of library parts, independent of components placed in the circuit.
- **No automatic optimization.** Parts are added, removed, and reordered by hand; the panel does not search a library or solve for a target.
- **One sweep.** The cascade is evaluated over the Network Analyzer's sweep only.
