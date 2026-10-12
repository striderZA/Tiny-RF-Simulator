# Receiver Requirements

Receiver Requirements checks a receiver chain against limits that you set. It measures the chain over a frequency band and reports whether each metric passes or fails. Open it from **View > Receiver Requirements**.

## Set the band

**Band start (Hz)** and **Band stop (Hz)** set the frequency band to check.

## Choose the metrics

Each metric has a checkbox. Tick **Enable gain** to set **Gain min (dB)** and **Gain max (dB)**. Tick **Enable noise figure** to set **NF max (dB)**.

Tick **Enable output power** to check the output power at a reference tone. Enter **Output power min (dBm)** and **Output power max (dBm)**, then choose the **Output reference tone**. If the circuit has no source tones, the field reads "Output reference tone: unavailable".

Tick **Enable IIP3** to check the input-referred third-order intercept point. Enter **IIP3 min (dBm)**, **IIP3 tone spacing (Hz)**, **IIP3 input start (dBm)**, **IIP3 input stop (dBm)**, and **IIP3 input step (dB)**. The input sweep must have between 3 and 101 levels, and the tone spacing must be positive.

## Apply and cancel

Choose **Apply requirements** to save the limits and evaluate the circuit against them. Choose **Cancel** to discard changes that you have not applied.

## Results

An enabled metric shows **Pass**, **Fail**, or **Incomplete**. A metric that is not enabled, or a panel with no applied requirements, shows **Not configured**. If the settings are invalid, every metric shows **Invalid configuration**. The **Overall** line shows the combined result.

A metric shows **Fail** when a point in the band is outside its limits. It shows **Incomplete** when the Network Analyzer sweep does not cover the whole band, or when a point in the band has no data.

Gain, noise figure, output power, and IIP3 use the same Point A, Point B, and sweep as the [Network Analyzer](network-analyzer.md). Set those on the Network Analyzer first, and make its sweep cover the band. The unsupported cases described on that page can also leave Receiver Requirements without data.

Requirements are saved with the project.
