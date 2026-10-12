# Spectrum Analyzer

The Spectrum Analyzer plots the spectrum of each probed signal. Open it from **View > Spectrum Analyzer**. To add a trace, probe a pin, as described in [Probes](../circuits/probes.md).

## Frequency range

**Start Frequency (MHz)** and **Stop Frequency (MHz)** set the span shown. Both accept values from -20000 to 20000 MHz. **Reset Zoom** sets the span back to the full range.

## Bandwidths

**RBW (kHz)** sets the resolution bandwidth, and **VBW (kHz)** sets the video bandwidth. Both accept values from 1 kHz to 100 MHz.

## Trace mode

**Trace Mode** chooses how sweeps are combined:

- **Clear/Write** shows the latest sweep only.
- **Max Hold** keeps the highest level seen at each frequency.
- **Min Hold** keeps the lowest level seen at each frequency.
- **Video Avg** averages the sweeps. **Avg Count** sets how many sweeps are averaged, from 2 to 100. Avg Count is active only in Video Avg mode.

**Reset Hold** clears the accumulated trace.

## Amplitude

**Ref (dBm)** sets the upper limit of the amplitude axis, and **Min level (dBm)** sets the lower limit. Both accept values from -174 to 10 dBm.

## Limits

Edits are clamped into each field's range. A loaded value that lies outside the range is shown in an amber field, with a tooltip that names the range, until you edit it.

## Readouts

The trace header shows the current mode and the peak, in the form "Trace: <mode> | Peak: <frequency> MHz, <level> dBm". A marker's readout shows its frequency and level in the form "Marker: <frequency> MHz, <level> dBm".
