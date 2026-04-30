# Touchstone File Format Specification — Parser Reference
> Derived from the official IBIS Open Forum Touchstone® File Format Specification Version 2.0 (April 24, 2009).
> This document is written for an agent implementing a Touchstone file parser. It focuses on rules, data layouts, and gotchas rather than background prose.

---

## Table of Contents
1. [File Overview](#1-file-overview)
2. [General Syntax Rules](#2-general-syntax-rules)
3. [Versions: 1.0 vs 2.0](#3-versions-10-vs-20)
4. [File Structure](#4-file-structure)
5. [Keywords (Version 2.0)](#5-keywords-version-20)
6. [Option Line](#6-option-line)
7. [Network Data](#7-network-data)
   - [Single-Ended: 1-port and 2-port](#71-single-ended-1-port-and-2-port)
   - [Single-Ended: 3-port and 4-port](#72-single-ended-3-port-and-4-port)
   - [Single-Ended: 5-port and above](#73-single-ended-5-port-and-above)
   - [Mixed-Mode Data](#74-mixed-mode-data)
8. [Noise Parameter Data](#8-noise-parameter-data)
9. [Matrix Formats](#9-matrix-formats)
10. [Compatibility Notes](#10-compatibility-notes)
11. [Worked Examples](#11-worked-examples)

---

## 1. File Overview

A Touchstone file (also called an **SnP file**) is an ASCII text file containing frequency-dependent n-port network parameters for a device or interconnect. It stores parameters such as S, Y, Z, H, or G at discrete frequency points.

### File Extensions
| Convention | Usage |
|---|---|
| `.snp` | Traditional — `n` is the port count (e.g., `.s2p` for 2-port) |
| `.s2p` | Sometimes used even for non-2-port files for OS compatibility |
| `.ts` | **Recommended for Version 2.0 files** |

---

## 2. General Syntax Rules

These rules apply to **both** Version 1.0 and 2.0 files.

- **Case-insensitive** throughout.
- **Encoding:** US-ASCII only. Permitted characters:
  - Graphic characters: `0x20`–`0x7E`
  - Tab: `0x09`
  - Line endings: `LF (0x0A)`, `CR+LF (0x0D 0x0A)`, or `CR (0x0D)`
- **Comments:** Preceded by `!`. May appear on their own line or after the last data value on a line. No multi-line comments.
- **Blank lines:** Permitted anywhere.
- **Angles:** Always in degrees.
- **Scientific notation:** Allowed (e.g., `1.2345e-12`). No precision limits.
- **Keywords (v2.0):**
  - Enclosed in `[` and `]`.
  - Must start at column 1.
  - No space/tab immediately after `[` or before `]`.
  - Multi-word keywords use exactly one space or one dash between words.
- **Keyword arguments:** Separated from the closing `]` by at least one whitespace character (not newline, unless the keyword explicitly allows it).
- **Data values:** Separated by one or more whitespace characters.
- **Frequency ordering:** All network data and noise data must be in strictly increasing frequency order.

---

## 3. Versions: 1.0 vs 2.0

### Version 1.0
- No `[Version]` keyword (its presence is what signals v2.0).
- Option line is the first non-comment, non-blank line.
- No additional keywords — just the option line followed by data.
- G-, H-, Y-, Z-parameters are **normalized** to the reference resistance.
- Effective noise resistance is **normalized**.
- No mixed-mode support.
- Max 4 data pairs per line.
- Max 99 ports (original Touchstone limitation; not enforced in v1.0 spec).

### Version 2.0
- **Must** begin with `[Version] 2.0` as the first non-comment, non-blank line.
- Uses explicit keywords to structure the file.
- G-, H-, Y-, Z-parameters are **not normalized**.
- Effective noise resistance is **not normalized**.
- Supports mixed-mode parameters.
- No line-length restriction on data lines.
- A Version 1.0 file is valid as Version 2.0 if `[Version]` and `[Number of Ports]` are omitted.

---

## 4. File Structure

### Version 1.0 Structure
```
[comments]
# <option line>
[comments]
<network data>
[noise data — 2-port only]
```

### Version 2.0 Structure
The following keywords **must appear in this order** at the top:
```
[Version] 2.0
# <option line>
[Number of Ports] <n>
```

The following keywords appear **after** `[Number of Ports]` and **before** `[Network Data]`, in any order relative to each other:
```
[Two-Port Data Order] <12_21|21_12>        (required if n=2)
[Number of Frequencies] <int>              (required)
[Number of Noise Frequencies] <int>        (required if noise data present)
[Reference] <r1> <r2> ... <rn>            (optional)
[Matrix Format] <Full|Lower|Upper>         (optional, default: Full)
[Mixed-Mode Order] <descriptors>           (optional)
[Begin Information]                        (optional)
  ...
[End Information]
```

Then the data section, in this order:
```
[Network Data]
<network parameter data>
[Noise Data]                               (required if [Number of Noise Frequencies] defined)
<noise parameter data>
[End]
```

---

## 5. Keywords (Version 2.0)

### `[Version]`
- **Argument:** `2.0` (only valid value)
- Must be the first non-comment, non-blank line in a v2.0 file.
- Exactly one occurrence allowed.

---

### `[Number of Ports]`
- **Argument:** A single positive integer — the number of **single-ended** ports.
- Must appear immediately after the option line.
- Exactly one occurrence.

---

### `[Two-Port Data Order]`
- **Required when and only when** `[Number of Ports]` is `2`.
- **Arguments:** `21_12` or `12_21`
  - `21_12` → data order is `N11, N21, N12, N22` (Version 1.0 convention)
  - `12_21` → data order is `N11, N12, N21, N22` (natural matrix order)

---

### `[Number of Frequencies]`
- **Argument:** Positive integer — the number of frequency points in `[Network Data]`.
- Required in v2.0. Does not affect noise data.

---

### `[Number of Noise Frequencies]`
- **Argument:** Positive integer — the number of noise frequency points.
- Required in v2.0 if and only if noise data is present.
- Prohibited if no noise data is present.

---

### `[Reference]`
- **Arguments:** One positive real number (ohms) per port, in port order (port 1 through port n).
- Arguments may span multiple lines.
- Optional. If absent, reference impedance comes from the option line `R n` value.
- Overrides the option line `R n` for S-parameter data.
- Has **no effect** on G-, H-, Y-, or Z-parameter data in v2.0.
- Has **no effect** on noise data.
- Complex or imaginary impedance values are **not supported**.

---

### `[Matrix Format]`
- **Arguments:** `Full`, `Lower`, or `Upper`
- Default: `Full`
- `Full`: all n×n matrix elements are present.
- `Lower`: only lower triangular elements (including diagonal) — assumes symmetry.
- `Upper`: only upper triangular elements (including diagonal) — assumes symmetry.
- Applies to single-ended and mixed-mode network data. No effect on noise data.

**Parser note:** Use `[Number of Ports]` and `[Matrix Format]` to determine how many values to read per frequency block:
- `Full`: `2n² + 1` values per frequency point (1 freq + 2 values per element × n² elements)
- `Lower` or `Upper`: `n² + n + 1` values per frequency point

---

### `[Mixed-Mode Order]`
- Required only if mixed-mode data is present.
- Arguments are **descriptors** separated by whitespace (including newlines):
  - `S<port>` — single-ended port
  - `D<port>,<port>` — differential-mode (no spaces around comma)
  - `C<port>,<port>` — common-mode (no spaces around comma)
- Rules:
  - Every port number from 1 to n must appear in exactly one descriptor (or two, in the case of a port appearing in both a C and D pair).
  - If `D<i>,<j>` is present, `C<i>,<j>` must also be present (and vice versa).
  - The second port in a pair (`D<i>,<j>`) is the **reference** port.
  - The number of descriptors must equal `[Number of Ports]`.
  - The highest port number in any descriptor must equal `[Number of Ports]`.

---

### `[Begin Information]` / `[End Information]`
- Optional block for metadata. Reserved for future expansion.
- Must appear after `[Number of Ports]` and before `[Network Data]`.
- Content between these tags consists of information keywords (not yet defined in v2.0 spec).

---

### `[Network Data]`
- Marks the start of the network parameter data block.
- Required. Exactly one occurrence.

---

### `[Noise Data]`
- Marks the start of the noise parameter data block.
- Required if and only if `[Number of Noise Frequencies]` is defined.

---

### `[End]`
- Marks the end of the file. Any non-comment content after this is an error.
- Required in v2.0.

---

## 6. Option Line

The option line starts with `#` and sets global defaults for interpreting the data.

### Syntax
```
# [frequency_unit] [parameter] [format] [R n]
```

An empty option line (`#` alone) uses all defaults.

### Fields

| Field | Values | Default |
|---|---|---|
| `frequency_unit` | `Hz`, `kHz`, `MHz`, `GHz` | `GHz` |
| `parameter` | `S`, `Y`, `Z`, `H`, `G` | `S` |
| `format` | `DB`, `MA`, `RI` | `MA` |
| `R n` | Positive real number (ohms) | `50` |

- `H` and `G` parameters are valid **only for 2-port networks**.
- `Y` and `Z` parameters are valid for any port count in v1.0/v2.0 (unlike the original proprietary Touchstone).
- `DB` = 20×log₁₀(|magnitude|), paired with angle in degrees.
- `MA` = magnitude and angle in degrees.
- `RI` = real and imaginary parts.
- Fields may appear in any order except `#` must come first and the value after `R` must immediately follow `R`.
- The option line format does **not** apply to noise parameters.

### Examples
```
#                          ! all defaults: GHz, S, MA, R 50
# GHz S RI R 100
# kHz Y RI R 100
# Hz Z MA R 10
# kHz H RI R 1
# MHz G DB R 1
```

---

## 7. Network Data

### Data Pair Format
Each network parameter is represented as a **pair** of values according to the option line `format`:
- `MA`: `<magnitude> <angle_degrees>`
- `DB`: `<dB_value> <angle_degrees>`
- `RI`: `<real_part> <imaginary_part>`

Notation: `<Nij>` below denotes one such pair (two numbers).

---

### 7.1 Single-Ended: 1-port and 2-port

**1-port (any version):**
```
<freq> <N11>
```

**2-port (Version 1.0 — fixed order):**
```
<freq> <N11> <N21> <N12> <N22>
```

**2-port (Version 2.0 — order depends on `[Two-Port Data Order]`):**
- `21_12` → `<freq> <N11> <N21> <N12> <N22>`
- `12_21` → `<freq> <N11> <N12> <N21> <N22>`

**2-port with `[Matrix Format] Lower` or `Upper`:**
Both formats are identical for 2-port (since `N12 = N21` by symmetry):
```
<freq> <N11> <N21> <N22>
```

---

### 7.2 Single-Ended: 3-port and 4-port

Data is arranged in **matrix row-wise order**. Each row starts on a new line in v1.0. In v2.0, data may span any number of lines as long as values are parsed contiguously.

**3-port Full:**
```
<freq> <N11> <N12> <N13>
       <N21> <N22> <N23>
       <N31> <N32> <N33>
```

**4-port Full:**
```
<freq> <N11> <N12> <N13> <N14>
       <N21> <N22> <N23> <N24>
       <N31> <N32> <N33> <N34>
       <N41> <N42> <N43> <N44>
```

**Version 1.0 constraint:** Max 4 data pairs per line. Rows continue on subsequent lines.

**Version 2.0:** No per-line limit. Parse by total value count using the formula above.

---

### 7.3 Single-Ended: 5-port and above

Same row-wise layout. In Version 1.0, rows exceeding 4 pairs wrap to the next line(s).

**Example: 6-port Full (v1.0 style, 4 pairs per line max):**
```
<freq> <N11> <N12> <N13> <N14>   ! row 1, values 1-4
       <N15> <N16>                ! row 1, values 5-6
       <N21> <N22> <N23> <N24>   ! row 2
       <N25> <N26>
       ...
```

**Lower triangular (6-port):**
```
<freq> <N11>
       <N21> <N22>
       <N31> <N32> <N33>
       <N41> <N42> <N43> <N44>
       <N51> <N52> <N53> <N54> <N55>
       <N61> <N62> <N63> <N64> <N65> <N66>
```

**Upper triangular (6-port):**
```
<freq> <N11> <N12> <N13> <N14> <N15> <N16>
             <N22> <N23> <N24> <N25> <N26>
                   <N33> <N34> <N35> <N36>
                         <N44> <N45> <N46>
                               <N55> <N56>
                                     <N66>
```

**Parser note for v2.0:** Ignore line breaks entirely when parsing network data. Use value counts to detect frequency boundaries:
- `Full`: new frequency every `2n² + 1` values
- `Lower` or `Upper`: new frequency every `n² + n + 1` values

---

### 7.4 Mixed-Mode Data

Mixed-mode data replaces single-ended data when `[Mixed-Mode Order]` is present. Both cannot coexist as separate data sets in the same file.

**Supported parameter types:** S, Y, Z only (no H or G).

**How to interpret the matrix:**
The `[Mixed-Mode Order]` descriptor list defines both the row and column labels of the mixed-mode matrix. The i-th descriptor is the row/column label for `Nii`. Element `Nij` uses descriptor `i` as the row (response) and descriptor `j` as the column (stimulus).

**Example:** For a 3-port with `[Mixed-Mode Order] D1,2 S3 C1,2`, the matrix layout is:
```
<freq>  <D1,2→D1,2>  <D1,2→S3>  <D1,2→C1,2>
        <S3→D1,2>    <S3→S3>    <S3→C1,2>
        <C1,2→D1,2>  <C1,2→S3>  <C1,2→C1,2>
```

**Reference impedance in mixed-mode (v2.0 only):**
Given single-ended reference `R` for ports i and j forming a differential pair:
- Differential-mode reference impedance: `RD = 2R`
- Common-mode reference impedance: `RC = R/2`

This requires both ports in a pair to have the same single-ended reference impedance.

**Incident/reflected wave relations:**
```
a_D(i,j) = (a_i - a_j) / sqrt(2)
a_C(i,j) = (a_i + a_j) / sqrt(2)
b_D(i,j) = (b_i - b_j) / sqrt(2)
b_C(i,j) = (b_i + b_j) / sqrt(2)
```

**Transformation from mixed-mode to single-ended S-parameters:**
```
S_std = M^T · S_mm · M
```
where M is the orthogonal transformation matrix built from port pair groupings.

---

## 8. Noise Parameter Data

Noise parameters are **only valid for 2-port networks**. They appear after the network data.

### Format
Each noise frequency point occupies exactly **one line** with five values:

```
<x1> <x2> <x3> <x4> <x5>
```

| Field | Description |
|---|---|
| `x1` | Frequency (same units as the option line) |
| `x2` | Minimum noise figure (dB) |
| `x3` | Magnitude of source reflection coefficient for minimum noise (dimensionless) |
| `x4` | Phase of source reflection coefficient (degrees) |
| `x5` | Effective noise resistance (ohms in v2.0; normalized to option line impedance in v1.0) |

### Rules
- Noise data format is **fixed** — not affected by the option line `format` field (MA/DB/RI). `x3` is always a magnitude.
- The `x3`/`x4` reflection coefficient is referenced to the option line impedance (or 50Ω default).
- First noise frequency must be ≤ highest network parameter frequency.
- All noise data must be in increasing frequency order.
- Noise frequencies do not need to match network parameter frequencies.
- `[Reference]` has no effect on noise data.
- In v1.0, the parser detects noise data by frequency overlap rules. In v2.0, `[Number of Frequencies]` determines where network data ends and `[Noise Data]` keyword marks the noise section.

---

## 9. Matrix Formats

| Format | Elements included | Total element count (n ports) |
|---|---|---|
| `Full` | All n×n elements | n² pairs |
| `Lower` | Lower triangle + diagonal | n(n+1)/2 pairs |
| `Upper` | Upper triangle + diagonal | n(n+1)/2 pairs |

For `Lower` and `Upper`, missing off-diagonal elements are assumed equal to their symmetric counterpart (i.e., `N_ij = N_ji`).

---

## 10. Compatibility Notes

### Original Proprietary Touchstone (pre-spec) Restrictions
The following restrictions existed in the original Agilent Touchstone tool but are **not enforced** by the v1.0 or v2.0 spec:
- DB/angle format was not allowed for G-, H-, Y-, Z-parameters.
- Y- and Z-parameters were not allowed for 3-port or larger networks.
- Maximum of 99 ports.

### Version 1.0 vs 2.0 Behavioral Differences

| Behavior | Version 1.0 | Version 2.0 |
|---|---|---|
| G-, H-, Y-, Z-parameter normalization | Normalized to reference resistance | Not normalized |
| Effective noise resistance | Normalized | Not normalized |
| Mixed-mode support | Not supported | Supported |
| Max data pairs per line | 4 | Unlimited |
| `[Number of Ports]` keyword | Not permitted | Required |
| `[Two-Port Data Order]` keyword | Not permitted | Required for 2-port |
| `[Network Data]` keyword | Not permitted | Required |
| `[Noise Data]` keyword | Not permitted | Required if noise present |
| `[End]` keyword | Not permitted | Required |
| `[Version]` keyword | Not permitted | Required |

---

## 11. Worked Examples

### Example A: Version 1.0 — 2-port S-parameters (RI format)
```
!2-port S-parameter file, three frequency points
# GHz S RI R 50.0
!freq ReS11 ImS11 ReS21 ImS21 ReS12 ImS12 ReS22 ImS22
1.0000 0.3926 -0.1211 -0.0003 -0.0021 -0.0003 -0.0021 0.3926 -0.1211
2.0000 0.3517 -0.3054 -0.0096 -0.0298 -0.0096 -0.0298 0.3517 -0.3054
10.000 0.3419  0.3336 -0.0134  0.0379 -0.0134  0.0379 0.3419  0.3336
```

### Example B: Version 2.0 — 4-port S-parameters, Full matrix
```
[Version] 2.0
# GHz S MA R 50
[Number of Ports] 4
[Number of Frequencies] 1
[Reference] 50 75 0.01 0.01
[Matrix Format] Full
[Network Data]
5.00000 0.60 161.24 0.40 -42.20 0.42 -66.58 0.53 -79.34 !row 1
        0.40 -42.20 0.60 161.20 0.53 -79.34 0.42 -66.58 !row 2
        0.42 -66.58 0.53 -79.34 0.60 161.24 0.40 -42.20 !row 3
        0.53 -79.34 0.42 -66.58 0.40 -42.20 0.60 161.24 !row 4
[End]
```

### Example C: Version 2.0 — 4-port S-parameters, Lower triangular
```
[Version] 2.0
# GHz S MA R 50
[Number of Ports] 4
[Number of Frequencies] 1
[Reference] 50 75
0.01 0.01
[Matrix Format] Lower
[Network Data]
5.00000 0.60 161.24                                    !row 1: N11
        0.40 -42.20 0.60 161.20                        !row 2: N21 N22
        0.42 -66.58 0.53 -79.34 0.60 161.24            !row 3: N31 N32 N33
        0.53 -79.34 0.42 -66.58 0.40 -42.20 0.60 161.24 !row 4: N41 N42 N43 N44
[End]
```

### Example D: Version 2.0 — 2-port with noise data
```
[Version] 2.0
#
[Number of Ports] 2
[Two-Port Data Order] 21_12
[Number of Frequencies] 2
[Number of Noise Frequencies] 2
[Reference] 50 25.0
[Network Data]
! NETWORK PARAMETERS (GHz, S, MA, default)
2  .95 -26  3.57 157  .04 76  .66 -14
22 .60 -144  1.30  40  .14 40  .56 -85
[Noise Data]
! NOISE PARAMETERS: freq  NFmin(dB)  |Gamma_opt|  angle(deg)  Rn(ohms)
4  .7  .64  69  19
18 2.7 .46 -33  20
[End]
```

### Example E: Version 2.0 — 6-port Mixed-mode Y-parameters
```
[Version] 2.0
# MHz Y RI R 50
[Number of Ports] 6
[Number of Frequencies] 1
[Reference] 50 75 75 50 0.01 0.01
[Mixed-Mode Order] D2,3 D6,5 C2,3 C6,5 S4 S1
[Network Data]
5.00  8.0  9.0  2.0 -1.0  3.0 -2.0  1.0  3.0  1.0  0.1  0.2 -0.2
      2.0 -1.0  7.0  7.0  1.8 -2.0 -1.0 -1.0 -0.5  0.5  0.2 -0.1
      3.0 -2.0  1.8 -2.0  5.8  6.0  1.2  0.8  0.9  0.7  0.3 -0.5
      1.0  3.0 -1.0 -1.0  1.2  0.8  6.3  8.0  2.0 -0.5  1.5  0.6
      1.0  0.1 -0.5  0.5  0.9  0.7  2.0 -0.5  4.7 -6.0 -1.0  2.0
      0.2 -0.2  0.2 -0.1  0.3 -0.5  1.5  0.6 -1.0  2.0  5.5 -7.0
[End]
```

---

## Parser Implementation Checklist

Use this as a guide when implementing the parser:

- [ ] Strip comments (`!` to end of line) before parsing any line
- [ ] Handle all three line endings: LF, CR+LF, CR
- [ ] Detect version: presence of `[Version] 2.0` keyword → v2.0 mode; otherwise v1.0
- [ ] Parse option line: extract `frequency_unit`, `parameter`, `format`, `R n`
- [ ] For v2.0: parse `[Number of Ports]` to get `n`
- [ ] For v2.0 with n=2: require and parse `[Two-Port Data Order]`
- [ ] Parse `[Reference]` if present (may span multiple lines)
- [ ] Parse `[Matrix Format]` (default: Full)
- [ ] Parse `[Mixed-Mode Order]` if present; validate descriptor rules
- [ ] Determine values-per-frequency-block from `n` and matrix format
- [ ] Parse network data: use value count (v2.0) or row/line structure (v1.0) to group by frequency
- [ ] For v1.0: enforce max 4 pairs per line; rows start on new lines for n≥3
- [ ] Parse noise data if `[Number of Noise Frequencies]` is defined (v2.0) or if trailing data exists after network data (v1.0)
- [ ] Validate noise data: 5 values per line, x3 always a magnitude regardless of format
- [ ] Apply normalization: v1.0 normalizes G/H/Y/Z and noise Rn; v2.0 does not
- [ ] Validate frequency ordering (strictly increasing) for both network and noise data
- [ ] For v2.0: stop parsing at `[End]`; treat anything after it as an error
