# Amplifier

The Amplifier component adds gain, noise figure, and distortion to a signal. Distortion stays off until you tick **Enable Nonlinearity** in the Inspector. Then the component makes harmonic and intermodulation products from its OIP2 and OIP3 intercept points. While OIP3 is still at its default of 100 dBm, setting P1dB sets OIP3 9.6 dB above it.

Each intercept can be one value for the whole component. For intercepts that change with frequency, load an amp-f file instead.

## Set single values

1. Select the Amplifier. In the Inspector, set **Gain (dB)** and **NF (dB)**.
2. Tick **Enable Nonlinearity**. The **OIP2 (dBm)**, **OIP3 (dBm)**, and **P1dB (dBm)** fields appear.

## Load an amp-f file

An amp-f file (`.ampf`) is a text table of intercepts against frequency. A file can hold several blocks, one for each operating condition, such as a gate voltage or temperature. The format is described in the [amp-f v1 specification](https://github.com/striderZA/Tiny-RF-Simulator/blob/master/docs/resources/ampf_v1_spec.md).

The **amp-f File** section appears only while **Enable Nonlinearity** is on.

1. Click **Browse** and choose a `.ampf` or `.txt` file. The path appears after **File:**.
2. Pick a block in the **Block** list. Each entry lists the block's identity in key order, such as `TEMP=-40 VG=2.5`. The identity holds the block's VAR and COND values and any DEVICE defaults it inherits. A block with no identity values reads `(no VAR or COND)`.
3. A file with one block selects it automatically. **(automatic)** clears your choice, and it works only for a file with one block.
4. To remove the file, click **Clear**.

The Amplifier has no temperature, bias, or supply setting, so you choose the block yourself.

## Which values are used

Each intercept is read at a tone's frequency, not at the product's own frequency: a harmonic uses the tone it is a harmonic of, and an IM3 product (`2f1 ± f2`) uses the fundamental it squares (`f1`). A missing value (`NaN`) passes to the next source at that frequency. Gain below means the amplifier's gain at that same frequency.

**OIP3** (third-order products) comes from the first source with a value:

- With an amp-f block active: `OIP3_dBm`, then `IIP3_dBm` plus gain, then `OP1dB_dBm` plus 9.6 dB, then `IP1dB_dBm` plus gain plus 9.6 dB. Values are interpolated linearly between the file's rows, and the nearest row is held outside the file's range. An active block replaces the AMP file (`.amp`) entirely. If none of these has a value at that frequency, the **OIP3 (dBm)** field is used, never the AMP file.
- With no amp-f block active and an AMP file (`.amp`) loaded that has an OIP3 or IIP3 section: its OIP3 section, or else its IIP3 section plus gain.
- Otherwise: the **OIP3 (dBm)** field.

**OIP2** (second-order products) comes from the first source with a value:

- With an amp-f block active: `OIP2_dBm`, then `IIP2_dBm` plus gain. If neither has a value at that frequency, the **OIP2 (dBm)** field is used.
- Otherwise: the **OIP2 (dBm)** field. An AMP file (`.amp`) does not supply OIP2.

Second-order intermodulation (IM2) products, at the sum and difference of two tones, use the lower of the two tones' OIP2 values to set the cross-term coefficient. Each OIP2 is read at its own tone's frequency, so the product's frequency does not change the value used. This is the worst-case choice: it gives the larger IM2 product.

Only the six intercept columns above affect the signal. Gain, NF, S-parameter, and VSWR columns are read but not applied yet.

## Touchstone S-parameter band

A Touchstone file (`.s2p`) covers a frequency band. Inside the band, S-parameters are interpolated between its points. Outside the band they hold the value of the nearest edge point, which is not a measurement. The inspector shows the file's band. The output marks each out-of-band tone and sweep bin, and the attenuator, equalizer and ideal filter do the same. The held value still sets the gain at those frequencies. A mark stays on a tone that a later block passes through; sweep-bin marks are not carried past other blocks.

## Errors

- A file that cannot be read, or does not follow the format, is not used. Its path stays in the **File** line, and the error appears in red below the section. The message names the file and, for a problem on one line, that line.
- If no block, or more than one block, matches the selection, the error says so, and no block is active.
- When no block is active, the rules above apply without an amp-f block. So an AMP file (`.amp`) still supplies OIP3 when one is loaded.
- Files over 256 MiB are rejected before they are read.

## Save and load

- The project keeps the file reference and the selected block.
- A file inside the project folder is saved as a path relative to the project file.
- A file outside the project folder is copied into the `rf_data` folder beside the project when you save. If a copy cannot be made, for example because the file is missing, is a symbolic link, or is over 256 MiB, the save fails and the existing project file is left unchanged.
