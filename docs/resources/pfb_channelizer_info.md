
# Polyphase Filterbank (PFB) Channelizer

## What Problem It Solves

A channelizer takes a wideband input signal sampled at rate `Fs` and splits it into `M` narrowband subchannels, each with bandwidth `Fs/M` and decimated to sample rate `Fs/M`. The naïve approach — apply `M` separate bandpass filters and decimate each — is computationally expensive. The PFB channelizer achieves the same result in `O(N log M)` operations instead of `O(N·M)` by exploiting the Noble identity and the FFT.

---

## Core Concepts

### 1. Prototype Lowpass Filter

Design a single FIR lowpass filter `h[n]` with:
- Cutoff frequency: `Fs / (2M)` (half a channel width)
- Length: `N = K·M` taps (typically `K` = 8–16 taps per polyphase branch)
- Window: Kaiser or Parks-McClellan for good stopband rejection

This is the **prototype filter**. All channel filters are modulated versions of it.

### 2. Polyphase Decomposition

Split `h[n]` into `M` subsequences called **polyphase branches**:

```
E_k[n] = h[n·M + k]    for k = 0, 1, ..., M-1
```

Each branch `E_k` has `K` taps. Branch `k` captures every M-th sample of `h`, offset by `k`. This decomposition is exact — no information is lost.

### 3. The Channelizer Structure

The analysis channelizer (input → channels) works as follows:

```
Input stream x[n]  (rate Fs)
        |
        v
   Commutator / serial-to-parallel
   (collect M new samples into a buffer)
        |
        v
   Apply polyphase branches E_0 ... E_{M-1}
   (each branch filters its portion of the buffer)
        |
        v
   M filter outputs (one per branch)
        |
        v
   M-point IFFT  (or FFT, depending on convention)
        |
        v
   M channel outputs  (rate Fs/M each)
```

**Key insight:** The commutator routes each new input sample to a different polyphase branch in rotation. After every `M` input samples, one output sample is produced per channel. The FFT mixes the branch outputs to form the frequency-shifted channels.

### 4. Why the FFT Appears

By the Noble identity, decimation commutes with filtering when the filter is polyphase-decomposed. After decomposition, each branch runs at the output rate (`Fs/M`). The DFT then simultaneously computes all `M` channel center frequencies — equivalent to multiplying by `M` complex exponentials `exp(j·2π·k·n/M)` for `k = 0 … M-1`, but at the cost of a single FFT.

---

## Detailed Implementation Steps

### Step 1 — Design the Prototype Filter

```python
import numpy as np
from scipy.signal import firwin

M = 32        # number of channels
K = 8         # taps per polyphase branch
N = K * M     # total prototype filter length

h = firwin(N, cutoff=1.0/M, window=('kaiser', 8.0))
# cutoff=1/M normalises to Nyquist=1; this gives Fs/(2M) cutoff
```

### Step 2 — Build the Polyphase Matrix

Reshape `h` into an `(M, K)` matrix where row `k` is branch `E_k`:

```python
# Pad to length N if needed, then reshape
h_padded = np.append(h, np.zeros(M - len(h) % M) if len(h) % M else h)
poly_matrix = h_padded.reshape(M, K)  # shape (M, K)
# Row k = E_k = h[k], h[k+M], h[k+2M], ...
```

> Note: Some references flip the branch indexing or the tap ordering. Be consistent and verify with a known input.

### Step 3 — Maintain a Delay Buffer

The filter state is an `(M, K)` buffer that shifts in `M` new samples per output cycle:

```python
buf = np.zeros((M, K), dtype=complex)

def process_block(x_new):
    """
    x_new : array of M new input samples (newest last)
    Returns: M channel output samples
    """
    # Shift buffer: drop oldest column, insert new samples as newest column
    buf[:, :-1] = buf[:, 1:]
    buf[:, -1] = x_new[::-1]   # reverse so oldest sample aligns with longest delay

    # Apply polyphase filter: element-wise multiply and sum across taps
    branch_out = np.sum(poly_matrix * buf, axis=1)  # shape (M,)

    # FFT across branches to produce channel outputs
    channel_out = np.fft.ifft(branch_out)            # or fft, see convention note
    return channel_out
```

### Step 4 — Drive the Channelizer

```python
channels = np.zeros((M, len(x) // M), dtype=complex)

for i, start in enumerate(range(0, len(x) - M + 1, M)):
    channels[:, i] = process_block(x[start:start + M])
```

Each column of `channels` is one output time step; each row is one subband.

---

## FFT vs IFFT Convention

| Convention | When to use |
|---|---|
| `np.fft.fft` | Channel `k` maps to positive-frequency bin `k` (standard analysis) |
| `np.fft.ifft` | Flips frequency order; common in some DSP textbook derivations |

Pick one and verify: inject a tone at exactly `k·Fs/M` Hz and confirm it peaks in channel `k`.

---

## Frequency Channel Mapping

Channel `k` is centered at:

```
f_k = k · Fs / M        (for k = 0, 1, ..., M/2)
f_k = (k - M) · Fs / M  (for k = M/2+1, ..., M-1)
```

This is the same as standard DFT bin mapping. Use `np.fft.fftfreq(M, d=1.0/Fs)` to get center frequencies.

---

## Critical Implementation Details

| Detail | Recommendation |
|---|---|
| Filter length | Use `N = K·M` exactly; pad `h` to this length if your design returns fewer taps |
| Buffer shift direction | Oldest sample at index 0 or K-1 — pick one and be consistent with poly_matrix orientation |
| Input ordering | Pass samples newest-last; reverse before loading into buffer column |
| Channel 0 content | DC + aliases; may need post-processing depending on use case |
| Overlapping channels | For raised-cosine prototype or oversampled PFB, increase `K` and use `M/2` stride |
| Output scaling | `np.fft.ifft` divides by `M`; multiply back if you need unity gain per channel |

---

## Validation Checklist

1. **Single tone test**: Input `exp(j·2π·f0·n/Fs)` with `f0 = k·Fs/M`. Channel `k` should have near-unity power; all others near zero.
2. **White noise test**: All `M` channel powers should be approximately equal.
3. **Reconstruction test** (if implementing synthesis PFB): Sum all channel outputs through the synthesis bank; verify the round-trip signal matches the original (up to group delay).
4. **Spectral leakage**: Check that a tone does not leak more than `−stopband_dB` into non-adjacent channels. Increase `K` if leakage is excessive.

---

## Synthesis (Inverse) PFB

The synthesis channelizer reconstructs a wideband signal from `M` subbands. It is the time-reverse of analysis:

```
M channel inputs (rate Fs/M each)
        |
        v
   M-point IFFT  (or FFT)
        |
        v
   Apply polyphase branches (upsampling path)
        |
        v
   Commutator (parallel-to-serial, interleave M outputs)
        |
        v
   Wideband output (rate Fs)
```

The same prototype filter `h` is used; branch ordering is the same. The key difference: after the FFT, each branch output is inserted into the output stream with stride `M`, then the prototype filter accumulates (overlap-add style).

---

## Complexity Summary

| Method | Multiplications per output sample per channel |
|---|---|
| Naïve (M separate FIR + decimate) | `N` |
| PFB channelizer | `K + log2(M)/M` ≈ `K` |

For `M=32`, `K=8`: naïve costs `256` mults/sample vs PFB's `~8` — a **32× reduction**.

---

## Quick Reference: Minimal Working Parameters

```python
M  = 32          # channels
K  = 8           # taps per branch (increase for better rejection)
N  = K * M       # = 256 prototype filter taps
Fs = 1.0         # normalised sample rate (or set to actual Hz)
h  = firwin(N, cutoff=1.0/M, window=('kaiser', 8.0))
E  = h.reshape(M, K)   # polyphase matrix
```

