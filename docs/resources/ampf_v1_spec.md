# amp-f File Format Specification, Version 1

This document fully specifies the `amp-f` (`.ampf`) file format. It is
self-contained: an implementation of a reader or writer needs nothing else.

## Overview

`amp-f` files store RF amplifier characterization data transcribed from
datasheet plots: S-parameters, gain, noise figure, intercept points, and
similar metrics versus frequency, for one device under one or more operating
conditions and sweep-variable settings.

- Encoding: UTF-8 text. Lines end with `\n`.
- Line-based. Parsing never requires look-ahead beyond one block.
- Whitespace-delimited within data lines (one or more spaces or tabs).

## Lexical rules

- `#` begins a comment; the rest of the line is ignored. Comments are allowed
  anywhere EXCEPT between `BEGIN` and `END`.
- Blank lines are allowed anywhere EXCEPT between `BEGIN` and `END`, and are
  ignored.
- Keywords (`DEVICE`, `VAR`, `COND`, `BEGIN`, `END`) are case-sensitive and
  MUST appear as the first token on their line.
- `KEY=VALUE` pairs: `KEY` is `[A-Za-z][A-Za-z0-9_]*`; `VALUE` is numeric
  (decimal float or integer, optionally signed, optionally with exponent),
  except `FREQ_UNITS` on the DEVICE line (see *Frequency unit*).

## File structure

A file is a sequence of lines in this order:

```
# amp-f v1                       <- exactly once, MUST be line 1
# source: HMC1131 datasheet fig.4  <- zero or more comment lines

DEVICE HMC1131 VCC=5.0 TEMP=25   <- exactly once, before any data block
# COLUMNS: S21_dB S21_deg NF_dB  <- exactly once, before any data block

VAR VG=2.5        \               zero to two VAR lines    \
COND TEMP=-40      >  one block's  >  immediately before
BEGIN             /    preamble   /  that block's BEGIN
2500   18.2   45.1   2.1            one or more data rows
2600   17.9   44.8   2.2
END

COND TEMP=85                     <- next block; VARs may repeat or be omitted
BEGIN
...
END
```

### Line 1 â€” magic

Line 1 MUST be exactly `# amp-f v1` (leading whitespace forbidden). A reader
MUST reject any file whose first line differs. This is the version mechanism:
anything an implementation does not recognize after reading it is a spec
violation, not a hint to guess.

### DEVICE line

```
DEVICE <part_number> [KEY=VALUE ...]
```

- Exactly one per file, before the first data block.
- `<part_number>`: non-whitespace token, e.g. `HMC1131`.
- `KEY=VALUE` pairs are default operating conditions inherited by data blocks
  (see *Condition inheritance*), except `FREQ_UNITS` (see *Frequency unit*).
  A key set by any block's `VAR` line MUST NOT appear here (validity rule 6).
  Fixed-unit key names: `TEMP` (Â°C), supply
  rails in volts (`VCC`, `VDD`, `VEE`, â€¦).

### COLUMNS comment

```
# COLUMNS: <name> [<name> ...]
```

- Exactly one per file, before the first data block.
- Declares the data columns, in order, that follow the leading frequency
  column of every data row. The frequency column is NOT listed here.
- The list is global: every data row in the file has exactly
  `len(COLUMNS) + 1` fields. There are no per-block column lists.
- Column names are `<PARAM>_<unit>`, e.g. `S21_dB`, `S21_deg`, `S11_dB`,
  `S11_deg`, `S12_dB`, `S22_dB`, `Gain_dB`, `NF_dB`, `IIP3_dBm`,
  `OIP3_dBm`, `IIP2_dBm`, `OIP2_dBm`, `IP1dB_dBm`, `OP1dB_dBm`,
  `IVSWR`, `OVSWR`. Dimensionless quantities have no unit, so their names have no
  underscore. The list above is the conventional vocabulary, not a
  closed enum: readers MUST accept arbitrary `<PARAM>_<unit>` and dimensionless names.
- Units are encoded in the name; there are no separate unit declarations.

### Data blocks

A data block is a preamble followed by `BEGIN`, one or more data rows, and
`END`.

Preamble lines, in any order, all optional:

- `VAR <name>=<value>` â€” an independent sweep-variable assignment (e.g.
  `VAR VG=2.5`). A block may have zero, one, or two VAR lines (matching the
  application's Var1/Var2 sweep axes).
- `COND <KEY>=<value>` â€” an operating condition for this block (e.g.
  `COND TEMP=-40`). Zero or more. A key MUST NOT appear twice in one block.

Data rows:

```
<freq> <col_1> <col_2> ... <col_N>      where N = len(COLUMNS)
```

- `<freq>`: frequency in the unit given by `FREQ_UNITS` (see below).
- Each `<col_i>`: a decimal number or the exact token `NaN`.
- Every data row MUST have exactly `N + 1` fields. Rows with fewer or more
  fields are invalid.
- Frequency within a block SHOULD be strictly ascending but readers MUST NOT
  rely on it.
- A block MUST contain at least one data row. Empty blocks are invalid.
- `BEGIN`/`END` are not nested and cannot appear inside a data block.

### Frequency unit

`FREQ_UNITS=<Hz|kHz|MHz|GHz>` MAY appear as a `KEY=VALUE` pair on the DEVICE
line. Default when absent: `MHz`. It applies to the whole file. It MUST NOT
appear on a `VAR` or `COND` line.

### Condition inheritance

- A DEVICE `KEY=VALUE` pair (other than `FREQ_UNITS`) is inherited by every
  data block whose preamble does not set that key on a `COND` line. A block's
  own `COND` line for the key takes precedence.
- A DEVICE key MUST NOT be set by a `VAR` line in any block (validity rule 6),
  so a sweep value is never inherited.
- If a key appears in neither, the condition is **unknown** for that block.
  Unknown MUST NOT be interpreted as any default value (not 25 Â°C, not room
  temp); it is simply absent.

### Block identity

A block is identified by the complete set of its (VAR, COND) pairs after
inheritance. Two blocks in one file MUST NOT have identical identity;
duplicate identity is a parse error. A consumer selects blocks by matching
the VAR/COND values it cares about and ignores the rest.

## Missing data

- A value missing at a single frequency: `NaN` in that cell. The row remains
  well-formed (correct field count, all numeric or NaN).
- A parameter not characterized for an entire block: the whole column is
  `NaN` for that block. This is the normal encoding for "not characterized at
  this condition," not an error.
- Absent data is ALWAYS `NaN`. Blank fields, empty strings, `None`, `inf`,
  `-inf`, and `nan` in any other spelling are invalid.
- How a consumer handles NaN (interpolate, skip the block, fail) is the
  consumer's policy and outside this spec.

## Validity summary

A file is valid if and only if:

1. Line 1 is exactly `# amp-f v1`.
2. Exactly one DEVICE line and exactly one `# COLUMNS:` comment, both before
   the first data block.
3. Every data block has a valid preamble, at least one data row, unique
   identity, and no repeated COND key.
4. Every data row has exactly `len(COLUMNS) + 1` whitespace-separated fields,
   each a decimal number or `NaN`.
5. No comments or blank lines between `BEGIN` and `END`.
6. No DEVICE key is set by a `VAR` line in any block.
7. No key appears on a `VAR` line in any block and on a `COND` line
   in any block.
8. `FREQ_UNITS`, if present, is a `KEY=VALUE` pair on the DEVICE line with
   value `Hz`, `kHz`, `MHz`, or `GHz`. No `VAR` or `COND` line sets it.

## Worked example

```
# amp-f v1
# source: HMC1131 datasheet fig.4 â€” gain and NF vs frequency
DEVICE HMC1131 VCC=5.0
# COLUMNS: S21_dB S21_deg NF_dB

VAR VG=2.5
COND TEMP=-40
BEGIN
2500   18.2   45.1   2.1
2600   17.9   44.8   2.2
2700   17.6   44.5   2.3
END

VAR VG=5.0
COND TEMP=-40
BEGIN
2500   19.0   46.2   NaN
2600   18.8   45.9   NaN
2700   18.5   45.6   NaN
END

COND TEMP=85
BEGIN
2500   18.0   44.9   2.4
2600   17.7   44.6   2.5
2700   NaN    NaN    2.6
END
```

In the example: the VG=5.0 block has no NF data (whole column NaN), the
TEMP=85 block has no VAR line and no VG on the DEVICE line, so VG is unknown
there, and its S21 columns drop out above 2.6 GHz
(NaN cells).
