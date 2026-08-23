
# RF ADC Software Twin — Semi-Idealized Sampling Model

## Scope and Assumptions

This model targets a **real-input RF ADC** (e.g. AD9680, LTC2208 class) operating in one of its Nyquist zones. "Semi-idealized" means:

- Linear, time-invariant sampling (no nonlinearity, no harmonic distortion, no intermodulation)
- Quantization noise is **white and Gaussian** (valid when the ADC is not clipping and dither is implicit)
- Jitter is **not** modelled (can be added later as phase noise on the sampling clock)
- Full-scale input maps to a fixed digital code range

Everything after the ADC is pure digital arithmetic.

---

## Signal Chain Overview

```
Analog RF Input
      |
      v
[Anti-alias / input BPF]    ← assume ideal brickwall at Fs/2 for zone 1,
      |                         or explicit band for higher Nyquist zones
      v
[Ideal Sampler @ Fs]        ← all Nyquist zone aliasing happens here
      |
      v
[Add Quantization Noise]    ← white noise floor = NSD · BW
      |
      v
[Digital NCO × complex exp] ← Digital Down Conversion (DDC)
      |
      v
[Lowpass FIR + Decimate]    ← narrow to channel BW, output at Fs/D
      |
      v
[Real → I/Q output]         ← already complex after DDC mixer
```

---

## 1. Ideal Sampler and Nyquist Zones

### Zone Mapping

An ADC sampling at `Fs` folds all analog content into `[0, Fs/2)` (first Nyquist zone). Content in higher zones aliases down. Zone `Z` (1-indexed) spans:

```
f_low  = (Z-1) · Fs/2
f_high =  Z    · Fs/2
```

The real sampled waveform's magnitude alias can be described by:

```python
def alias_frequency(f_RF, Fs):
    """
    Returns the positive magnitude alias in [0, Fs/2).
    """
    f = f_RF % Fs
    if f > Fs / 2:
        f = Fs - f
    return f
```

For the complex DDC path, the simulator expands a real RF tone into its `+f` and `-f`
exponentials first, then maps each component with a signed alias:

```python
def signed_alias_frequency(f_RF, Fs):
    """Return the signed sampled frequency in [-Fs/2, Fs/2)."""
    f = f_RF % Fs
    if f >= Fs / 2:
        f -= Fs
    return f
```

This preserves the distinction between the desired component and its image after the complex
NCO shift. The DDC low-pass then keeps only the selected complex-baseband component.

### Zone Inversion (Spectral Flip)

When `Z` is **even**, the alias spectrum is **spectrally inverted** — frequency increases in the zone map to decreasing digital frequency. This must be corrected in the DDC NCO frequency sign or by conjugating the output.

```python
def nyquist_zone(f_RF, Fs):
    return int(f_RF // (Fs / 2)) + 1   # 1-indexed

def is_inverted(f_RF, Fs):
    return nyquist_zone(f_RF, Fs) % 2 == 0
```

### Simulation: Inject a Tone

```python
import numpy as np

def sample_rf_tone(f_RF, amplitude, Fs, N_samples, phi0=0.0):
    """
    Simulate sampling a single real RF tone.
    Returns real ADC output array of length N_samples.
    Handles aliasing and spectral inversion automatically.
    """
    n = np.arange(N_samples)
    f_dig = alias_frequency(f_RF, Fs)
    sign  = -1 if is_inverted(f_RF, Fs) else +1
    # Real sampled signal: alias maps f_RF to f_dig (possibly inverted)
    return amplitude * np.cos(2 * np.pi * sign * f_dig * n / Fs + phi0)
```

To simulate a **band of signals**, generate each tone separately and sum before adding noise.

---

## 2. Quantization Noise and NSD

### Model

For an ideal B-bit ADC with full-scale range `V_FS` (peak-to-peak):

```
LSB       = V_FS / 2^B
SQNR      = 6.02·B + 1.76   dBFS   (for full-scale sine)
Total noise power = V_FS² / (12 · 2^(2B))
NSD       = Total noise power / (Fs/2)   [power per Hz]
          = V_FS² / (6 · 2^(2B) · Fs)   W/Hz  (into 1-ohm normalised)
```

In **dBFS/Hz** (normalised to full-scale, most common in datasheets):

```
NSD_dBFS/Hz = −(6.02·B + 1.76) − 10·log10(Fs/2)
```

Most datasheets quote **SNR** for a specific input level and BW, or quote **NSD** directly in dBFS/Hz or dBm/Hz (with an assumed load). Use whichever is given; back-calculate `sigma_noise` from NSD and `Fs`.

### Simulation

```python
def add_quantization_noise(signal, B, Fs, V_FS=2.0):
    """
    Add white Gaussian quantization noise to a real sampled signal.
    B     : ADC bits
    Fs    : sample rate (Hz) — sets noise bandwidth
    V_FS  : ADC full-scale peak-to-peak voltage
    """
    LSB = V_FS / (2**B)
    # Total noise variance for ideal uniform quantizer
    noise_variance = (LSB**2) / 12.0
    sigma = np.sqrt(noise_variance)
    noise = np.random.normal(0, sigma, size=len(signal))
    return signal + noise
```

> **Practical note:** Real ADCs have an **Effective Number of Bits (ENOB)** lower than B due to thermal noise, aperture jitter, DNL/INL. Replace `B` with `ENOB` in the formula above for a more accurate NSD model. ENOB is read from the datasheet SNR figure: `ENOB = (SNR_dB − 1.76) / 6.02`.

---

## 3. Digital Down Conversion (DDC)

DDC shifts the desired channel from its digital center frequency `f_c` down to DC. It consists of:

1. **Numerically Controlled Oscillator (NCO)** — generates a complex exponential
2. **Complex Mixer** — multiplies the real ADC output by the NCO
3. **Lowpass filter + decimation** — isolates the channel (Section 4)

### NCO

```python
def nco(f_c, Fs, N_samples, phi0=0.0):
    """
    Generate complex NCO: exp(-j·2π·f_c·n/Fs)
    Negative sign shifts f_c to DC (downconversion).
    For spectrally inverted zones, negate f_c before calling.
    """
    n = np.arange(N_samples)
    return np.exp(-1j * 2 * np.pi * f_c * n / Fs + 1j * phi0)
```

### Mixer

```python
def mix_to_baseband(real_signal, f_c, Fs, invert_spectrum=False):
    """
    Multiply real ADC samples by complex NCO to produce I/Q baseband.
    invert_spectrum: set True when input is from an even Nyquist zone.
    """
    f_nco = -f_c if invert_spectrum else f_c
    lo = nco(f_nco, Fs, len(real_signal))
    return real_signal * lo          # complex output
```

After mixing, the desired channel is centred at DC. Unwanted signals and images sit at other frequencies and will be removed by the lowpass filter.

### Image Rejection

Because the input is **real**, the complex mixer produces an image at `−f_c` in addition to the desired signal at DC. The lowpass filter after mixing suppresses this image. No explicit image reject mixer is needed in the digital domain — the FIR filter does the work.

---

## 4. Lowpass Filter and Decimation

### Purpose

After mixing to baseband, the signal occupies `[−BW/2, +BW/2]`. A lowpass FIR:

- Removes the image and all out-of-band signals
- Limits noise bandwidth before decimation (critical — without this, noise aliases)
- Enables decimation by integer factor `D = floor(Fs / Fs_out)` with no signal aliasing

### Filter Design

```python
from scipy.signal import firwin, lfilter

def design_decimation_filter(BW, Fs, D, num_taps=None, window=('kaiser', 8.0)):
    """
    Design a lowpass FIR for decimation by D.
    BW       : desired one-sided channel bandwidth (Hz)
    Fs       : input sample rate (Hz)
    D        : decimation factor
    num_taps : override tap count; defaults to 16*D (rule of thumb)
    """
    if num_taps is None:
        num_taps = 16 * D + 1           # odd length for linear phase
    cutoff = BW / (Fs / 2)              # normalised to Nyquist
    cutoff = min(cutoff, 1.0 / D)       # must not exceed Nyquist of output rate
    h = firwin(num_taps, cutoff, window=window)
    return h
```

### Decimate

```python
def filter_and_decimate(iq_signal, h, D):
    """
    Apply FIR filter h to complex IQ signal, then decimate by D.
    Uses direct convolution (lfilter). For large arrays, use
    scipy.signal.fftconvolve or overlap-add for efficiency.
    """
    from scipy.signal import lfilter
    filtered = lfilter(h, 1.0, iq_signal)
    return filtered[::D]                # keep every D-th sample
```

Output sample rate: `Fs_out = Fs / D`

### Decimation Aliasing

If the lowpass filter cutoff exceeds `Fs/(2D)`, signals outside the passband **alias back** into the channel at the output rate. Always verify:

```python
assert BW <= Fs / D, "Channel BW exceeds output Nyquist — increase D or reduce BW"
```

---

## 5. Real → I/Q Conversion via Complex Mixer

In the DDC above, mixing a **real** signal with a **complex exponential** already produces complex (I/Q) output. This section clarifies the mechanics for completeness.

### What the Mixer Produces

For real input `x[n]` and NCO `exp(−j·2π·f_c·n/Fs)`:

```
y[n] = x[n] · (cos(2π·f_c·n/Fs) − j·sin(2π·f_c·n/Fs))

I[n] = Re{y[n]} = x[n] · cos(...)
Q[n] = Im{y[n]} = x[n] · (−sin(...))
```

`I[n]` and `Q[n]` are the in-phase and quadrature components. After the lowpass filter, these form the complex baseband signal `I + jQ`.

### Analytic Signal Alternative

For offline processing (not real-time), you can produce I/Q from a real signal using the **Hilbert transform**:

```python
from scipy.signal import hilbert

def real_to_iq_hilbert(x_real):
    """
    Produces analytic (single-sideband) I/Q from real input.
    Only valid if the signal is already at baseband.
    For RF signals, use DDC + mixer instead.
    """
    return hilbert(x_real)   # returns x_real + j·H{x_real}
```

> The Hilbert approach is **not** equivalent to DDC for bandpass signals — it suppresses the negative-frequency image globally, whereas DDC + LPF selects a specific channel. Use DDC for channel-selective work.

### I/Q Imbalance (future extension)

Real analog I/Q mixers suffer gain and phase imbalance between the I and Q paths. In this software twin, the digital mixer is perfect by construction. Add imbalance later as:

```python
def apply_iq_imbalance(iq, gain_err_dB=0.0, phase_err_deg=0.0):
    alpha = 10**(gain_err_dB / 20)
    phi   = np.deg2rad(phase_err_deg)
    I = np.real(iq)
    Q = np.imag(iq)
    return (I + alpha * np.exp(1j * phi) * Q)
```

---

## 6. Aliasing — Full Treatment

Three distinct aliasing mechanisms exist in this chain. The model must handle all three.

### 6.1 Nyquist Zone Aliasing (at the ADC)

Any analog signal at `f_RF` aliases to `alias_frequency(f_RF, Fs)` as described in Section 1. This is **by design** for undersampling receivers (Zone 2, 3, …) but is also the mechanism by which out-of-band interferers corrupt the wanted channel. The pre-ADC anti-alias filter (assumed ideal brickwall in this twin) is the only defence.

### 6.2 Decimation Aliasing (after LPF)

If the lowpass filter has insufficient attenuation at frequencies `k·(Fs/D) ± f` for integer `k ≥ 1`, those components alias back into `[0, Fs/(2D)]` after decimation. Rule: the filter stopband must begin at or before `Fs/(2D)` and must provide attenuation equal to the required spurious-free dynamic range (SFDR) of the output.

```
Stopband attenuation ≥ SFDR_required_dB
Stopband edge        ≤ Fs / (2·D)
```

### 6.3 NCO Spectral Leakage (not true aliasing, but related)

If the NCO frequency is not an exact integer multiple of the FFT bin (when doing downstream spectral analysis), the tone appears spread across bins. This is a **windowing / coherency** issue, not aliasing. It does not affect time-domain processing.

### Summary Table

| Source | Cause | Prevention in twin |
|---|---|---|
| Nyquist zone fold | `f_RF > Fs/2` | Ideal BPF before sampler; correct NCO sign for zone |
| Decimation alias | LPF cutoff > `Fs/(2D)` | Assert cutoff ≤ `1/D` (normalised); use sufficient filter order |
| Spectral inversion | Even Nyquist zone | Negate NCO frequency or conjugate output |

---

## 7. Putting It All Together

```python
import numpy as np
from scipy.signal import firwin, lfilter

def rf_adc_twin(
    tones,          # list of (f_RF_Hz, amplitude, phase_rad)
    f_channel,      # center frequency of desired channel (Hz) — RF frequency
    BW,             # one-sided channel bandwidth (Hz)
    Fs,             # ADC sample rate (Hz)
    B,              # ADC bits (or ENOB)
    D,              # decimation factor
    N_samples,      # number of ADC samples to simulate
    V_FS=2.0,       # ADC full-scale peak-to-peak
):
    # 1. Generate real sampled signal (all tones alias into [0, Fs/2))
    x = np.zeros(N_samples)
    for (f_RF, amp, phi) in tones:
        x += sample_rf_tone(f_RF, amp, Fs, N_samples, phi)

    # 2. Add quantization noise
    x = add_quantization_noise(x, B, Fs, V_FS)

    # 3. Determine NCO frequency (digital alias of f_channel)
    f_dig   = alias_frequency(f_channel, Fs)
    inverted = is_inverted(f_channel, Fs)

    # 4. Mix to baseband (DDC)
    iq = mix_to_baseband(x, f_dig, Fs, invert_spectrum=inverted)

    # 5. Design lowpass filter and decimate
    h = design_decimation_filter(BW, Fs, D)
    iq_out = filter_and_decimate(iq, h, D)

    Fs_out = Fs / D
    return iq_out, Fs_out
```

### Example Usage

```python
tones = [
    (2.4e9, 0.5, 0.0),   # desired signal at 2.4 GHz
    (2.41e9, 0.1, 0.3),  # interferer 10 MHz away
]

iq, Fs_out = rf_adc_twin(
    tones      = tones,
    f_channel  = 2.4e9,
    BW         = 5e6,       # 5 MHz channel
    Fs         = 1e9,       # 1 GSPS ADC (zone 3 for 2.4 GHz: zone = floor(2.4e9/500e6)+1 = 5)
    B          = 12,
    D          = 50,        # output rate = 20 MSPS
    N_samples  = 100_000,
    V_FS       = 2.0,
)
```

---

## 8. Validation Checklist

1. **Aliasing correctness**: inject a tone at `f_RF = Z·Fs/2 + δ` for small `δ`. Verify the digital alias lands at `δ` (odd zone) or `Fs/2 − δ` (even zone).
2. **NSD floor**: with no input signal, measure PSD of `iq_out`. Should be flat at `NSD_dBFS/Hz − 10·log10(D)` relative to ADC full-scale. (Decimation narrows noise BW, so NSD at output rate is lower.)
3. **SNR vs B**: increase `B` by 1, expect SNR to rise by ~6 dB.
4. **Spectral inversion**: inject a two-tone signal in an even zone; verify that after DDC the tone ordering is correct (not mirrored).
5. **Decimation filter stopband**: inject an interferer at exactly `Fs/D` from the channel centre; verify it is attenuated by at least the designed stopband rejection.
6. **Image rejection**: inject a tone at `−f_c` (mirror of desired channel). Verify it is suppressed by the lowpass filter after mixing.

---

## 9. Key Parameters Quick Reference

| Parameter | Symbol | Typical values | Notes |
|---|---|---|---|
| Sample rate | `Fs` | 250 MSPS – 10 GSPS | Sets Nyquist zone boundaries |
| ADC bits / ENOB | `B` | 12–16 / 8–14 | Use ENOB for NSD accuracy |
| Full-scale range | `V_FS` | 1–2 V p-p | Datasheet "input full-scale" |
| Decimation factor | `D` | 4–256 | `Fs_out = Fs/D` |
| NCO frequency | `f_c` | 0 to `Fs/2` | Digital alias of RF channel centre |
| Taps per filter | — | `16·D + 1` (min) | More taps → better alias rejection |
| NSD | — | −150 to −165 dBFS/Hz | Increases ~6 dB/bit |

