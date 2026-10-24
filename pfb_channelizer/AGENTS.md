# pfb_channelizer — AGENTS.md

## Purpose

PFB channelizer engine: splits an input into channels and reports each channel's sampling rate and noise readout.

## Ownership

- `pfb_channelizer/include/` and `pfb_channelizer/src/`: the channelizer engine, filter design, and sampling-ratio math.
- The node, inspector, and hover presentation are app-owned ([app/AGENTS.md](../app/AGENTS.md)). This doc owns the numbers they show.

## Local Contracts

- The PFB channelizer is physically downstream of an RF ADC only. Direct RF-chain-to-PFB links must be rejected.
- Critical sampling (1x) is the default. A persisted 2x oversampling ratio is supported. Channel output `fs_Hz` is `ratio * input Fs / M`. Channel centers stay at `-Fs/2 + (k + 1/2) * Fs/M` (half-bin grid, not DFT bins), and usable channel bandwidth scales by the ratio. At 1x with the default K=8, β=8, a DC or DFT-bin tone on an even-M channel edge reads -6.02 dB in each of the two adjacent channels; other K and β give other edge levels.
- The active channel's integrated noise readout is `ENBW × P̄` in dBm, with ENBW the continuous channel-response integral (grid-independent) and `P̄` the `|H|²`-weighted mean input PSD over the channel's grid support. Channels with fewer than two input bins read unavailable (NaN).
- The PFB node-hover SNR compares the strongest active-channel tone against that integrated noise. It is independent of analyzer RBW. Ordinary analyzer traces and SNR remain RBW-based.

## Work Guidance

- None beyond the contracts above.

## Verification

- `tests/test_issue70_pfb_reconnect.cpp` (standalone): the PFB accepts only ADC input across reconnects, and a project load rejects direct PFB input links.
- `tests/test_issue37_pfb_input_removal.cpp` (standalone): removing an upstream node nulls the PFB's dangling input pointer.
- `tests/test_pfb.cpp` (main `tests` binary): channelizer behavior and output generation.
- `tests/test_pfb_filter_design.cpp`, `tests/test_pfb_calculator.cpp`, and `tests/test_pfb_sampling_ratio.cpp` (standalone): filter design, channel math, and sampling ratio.

## Child DOX Index

- None.
