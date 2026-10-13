# Network Analyzer

The Network Analyzer measures gain and noise figure over a frequency sweep. It compares two points in the circuit: Point A, the reference, and Point B, the measured point. Open it from **View > Network Analyzer**.

## Choose the points

Choose an output pin for **Point A (Reference)** and an output pin for **Point B (Measured)**. The plot shows the gain and noise figure of the path between the two points.

## Sweep settings

- **Start Freq (Hz)** and **Stop Freq (Hz)** set the sweep. Both accept values from 0 to 20 GHz.
- **Points** sets the number of frequency points in the sweep.
- **Stimulus Power (dBm)** sets the power of the test signal. It accepts values from -60 to 10 dBm.

## Results

The plot **Gain / Noise Figure vs Frequency** shows two traces: **Gain (dB)** and **Noise Figure (dB)**.

Below the plot, the averages appear as **Avg Gain** and **Avg NF**. When there is no data for a value, the readout says "no data" in place of a number.

## Limits

The analyzer reports no data in these cases:

- The measured path includes a combiner.
- A merge is fed by a source outside the measured path.
- Point A is a component that feeds a switched filter bank through more than one output port. A switched filter bank is a 1:2 switch feeding parallel branches that rejoin at a 2:1 switch.
