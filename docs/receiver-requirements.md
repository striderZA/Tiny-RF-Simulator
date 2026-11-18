# Receiver Requirements

Receiver Requirements checks the circuit against named requirement modes — each with its own band, gain, noise-figure, output-power, and IIP3 limits, plus its own input level. Open it from **View > Receiver Requirements**. The View item toggles the dockable panel, and its visibility persists between runs.

Every verdict in this view is measured live on the [Network Analyzer](network-analyzer.md)'s points and sweep: Point A is where the measurement starts, Point B is the output, and the sweep is the frequency grid. The Network Analyzer window can stay closed; the measurement still uses its settings. Widen the analyzer's start and stop frequencies until they cover a mode's whole band, or the mode reads **Incomplete**.

## Modes

The panel edits one mode at a time.

- **Mode** picks the mode to view. The choice is saved with the project but does not itself mark the project dirty.
- Type a name in **Mode name**, then **Add mode** to create it. A new mode **copies the selected mode's limits** — a handy way to make a variant.
- **Rename mode** renames the selected mode immediately.
- **Remove mode** asks for confirmation, then removes the mode and its limits.

A mode name is up to 64 characters, unique, and without `#`. Up to 64 modes fit. A project saved before modes existed loads its single limit set as one mode named **Default**.

## Limits

Each section has an enable checkbox except the band, which is always set:

| Section | Fields |
| --- | --- |
| **Band** | **Band start (Hz)**, **Band stop (Hz)** — start below stop. |
| **Gain** | **Enable gain**, then **Gain min (dB)** and **Gain max (dB)**. |
| **Out-of-band suppression** | Up to 16 points, below. |
| **Noise figure** | **Enable noise figure**, then **NF max (dB)**. |
| **Output power** | **Enable output power**, then **Output power min (dBm)** and **max (dBm)**, plus **Input level (dBm)** (−174 to +10), which stays editable while the limit is off. Output power is measured from the input level, not from a generator tone. |
| **IIP3** | **Enable IIP3**, then **IIP3 min (dBm)** and the two-tone test: **IIP3 tone spacing (Hz)**, **input start (dBm)**, **input stop (dBm)**, **input step (dB)** — 3 to 101 sweep levels. |
| **SNR** | **Enable SNR**, then **SNR minimum (dB)** and **SNR bandwidth (Hz)**. SNR uses the input level set under Output power. |

Edits are a draft until you click **Apply requirements**; **Cancel** restores the applied values. A refusal appears in red under the button — for example, an output-power or SNR limit needs an input level first. While a draft is pending, the mode controls are disabled until you apply or cancel. Applying with every metric off clears the mode's configuration.

## Out-of-band suppression

Each point says how far below the in-band **Gain min** the cascade must fall outside the band. Pick the **Anchor** (**Band start** or **Band stop**), an **Offset (Hz)**, and a **Min (dB)**. The point sits at the band edge plus the offset, outside the band, and requires gain limits to be enabled. Up to 16 points.

Out-of-band points are judged by the [RF Budget](rf-budget.md) prediction, not by the live measurement — the measured table here stays in-band only.

## The measured verdict

The **Measured result** table shows one row per metric: the **Limit** as configured, the in-band worst-case **Observed** range, and a **Status** pill.

- **Fail** — at least one in-band sample violates a limit. A single violating sample fails the metric even if coverage is still filling in.
- **Incomplete** — no violation, but the analyzer sweep does not cover the whole band, or no in-band sample exists yet, or a sample is still being measured.
- **Pass** — full band coverage, at least one sample, and no violation.
- **Not configured** — the metric's checkbox is off.

**Overall** fails if any enabled metric fails, passes only if every enabled metric passes, and is otherwise incomplete.

Two metrics have extra conditions:

- **Output power** stays **Incomplete** until the mode has an **Input level**.
- **SNR** never shows **Pass** in this view — the measured table has no SNR series, so an enabled SNR limit reads **Incomplete** here and is judged by the budget prediction instead.

A freshly applied limit can read **Incomplete** for a moment while the measurement fills in.

## Budget prediction

Below the measured table, the **Budget prediction** section shows each reached output's predicted verdict from the [RF Budget](rf-budget.md) — with gain, NF, output power, IIP3, SNR, and OOB columns. `No budget record for this mode` means the mode needs a budget record first: open **View > RF Budget** and click **Budget this mode**.
