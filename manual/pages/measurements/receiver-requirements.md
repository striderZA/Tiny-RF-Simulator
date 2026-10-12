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

Each enabled metric shows a status: **Pass**, **Fail**, **Incomplete**, **Not configured**, or **Invalid configuration**. The **Overall** line shows the combined result.

The measurements use the same measured path as the [Network Analyzer](network-analyzer.md), so the limits on circuits described there apply here too.

Requirements are saved with the project.
