# Spec: Configurable RF ADC DDC

Status: proposed

## Objective

Give the RF ADC user control over the digital down-converter tuning and decimation while keeping the simulator's existing frequency-domain signal model. The ADC must expose the NCO tuning as a normalized factor of its input sample rate, matching how an RF ADC DDC is configured.

## Existing Behavior

`AdcEngine` currently performs a fixed ideal DDC:

- input sample rate is `Fs`;
- tones are aliased into `[-Fs/2, Fs/2)`;
- the NCO is fixed at `+0.25 * Fs` and shifts tones by `-Fs/4`;
- the ideal low-pass keeps `[-Fs/4, Fs/4)`;
- output `Spectrum::fs_Hz` is `Fs/2`;
- output is complex baseband;
- NSD and input noise are represented as W/Hz PSD vectors.

The PFB channelizer consumes the ADC output sample rate and is physically connected only to an ADC output. Existing behavior and those link constraints must remain intact.

## Scope

### Included

1. A configurable power-of-two DDC decimation factor with supported values `1`, `2`, `4`, and `8`.
2. A configurable signed NCO frequency factor, stored as cycles per ADC input sample and bounded to `[-0.5, +0.5]`.
3. Output sample-rate and ideal passband changes derived from decimation.
4. Inspector controls with dirty tracking.
5. Project serialization/deserialization.
6. Component-library field metadata and built-in ADC definitions.
7. Focused DSP, propagation, persistence, and invalid-input tests.

### Explicitly Not Included

- FIR implementation, transition bands, ripple, stopband attenuation, or filter coefficients.
- Time-domain IQ sample generation; `IQStream` remains unused by `AdcEngine`.
- New ADC noise, ENOB, jitter, clipping, or calibration controls.
- Changes to PFB link policy or PFB channelizer internals.
- A reusable DDC configuration abstraction before a second consumer requires it.

## DSP Semantics

Let:

- `Fs` be the ADC input sample rate;
- `D` be the selected decimation factor;
- `a` be the normalized NCO factor;
- `f_NCO = a * Fs`.

The NCO factor is relative to the input `Fs`, not the decimated output rate. It remains stable when `Fs` or `D` changes. Positive NCO tuning shifts positive sampled frequencies downward toward DC.

For each input tone:

1. Expand real-domain tones with `conjugateSymmetricExpand()` as the current ADC boundary requires; leave complex input tones unexpanded.
2. Alias each component into `[-Fs/2, Fs/2)`.
3. Mix it to `f_shifted = f_alias - f_NCO`.
4. Retain it only when `-Fs/(2D) <= f_shifted < Fs/(2D)`.
5. Coherently accumulate tones at the same output frequency, preserving the current power and phase behavior.

The output grid spans `[-Fs/(2D), Fs/(2D))`, has uniform spacing, and reports:

```text
output.fs_Hz = Fs / D
output.is_complex_baseband = true
```

For the current compatibility defaults (`D=2`, `a=+0.25`), this is exactly the current `[-Fs/4, Fs/4)` and `Fs/2` behavior.

Noise mapping continues to use the existing nearest-input-bin policy. Its lookup frequency is adjusted for the configurable NCO and wrapped through the sampled Nyquist interval. Noise vectors remain PSD in W/Hz; decimation reduces integrated noise through the narrower retained bandwidth and does not apply an artificial PSD multiplier.

## Configuration Contract

The engine exposes two new parameters with setters that mark the engine dirty:

- `decimation`: one of `1`, `2`, `4`, `8`; default `2`.
- `nco_fs_fraction`: signed normalized factor in `[-0.5, +0.5]`; default `0.25`.

The two endpoints represent the same sampled tuning modulo `Fs`; both are accepted and normalized consistently by the DSP path. Inspector input should use a closed interval because a half-open numeric widget bound is awkward.

Invalid deserialized values must not create an invalid output. The loader clamps NCO factors into `[-0.5, +0.5]`. Invalid decimation values are clamped to the nearest supported choice, with ties resolved toward the lower value; values below `1` become `1`, and values above `8` become `8`.

## UI

`InspectorPanel::drawAdcProperties()` adds:

- a decimation combo labeled `Decimation` with `1`, `2`, `4`, and `8`;
- a numeric input labeled `NCO (×Fs)` with the signed normalized factor.

The existing sample-rate and NSD controls remain unchanged. All edits use the engine setters, set `m_param_edited`, and follow existing app dirty tracking.

## Persistence and Component Definitions

`AdcEngine::serialize()` adds:

```json
{
  "decimation": 2,
  "nco_fs_fraction": 0.25
}
```

alongside the existing `sample_rate_Hz` and `nsd_dBm_per_Hz` fields.

`deserialize()` accepts missing new fields as compatibility defaults (`2` and `0.25`) so existing `.rfsim` files preserve their current output. It validates present values using the configuration contract above.

The ADC `ComponentTypeRegistry` descriptor adds matching authorable fields. Built-in ADC component definitions explicitly include the same parameters so authored components and saved projects describe their full DDC behavior.

## Testing Strategy

Use a focused standalone ADC configuration test executable rather than adding more test cases to the shared `tests` executable, which is near the MinGW registration ceiling.

Cover:

- `D=1`, `2`, `4`, and `8` output `fs_Hz` and grid spans;
- default compatibility behavior (`D=2`, NCO `0.25`);
- NCO tuning of positive and negative tones;
- NCO factors at the normalized bounds;
- combined NCO and decimation filtering;
- real-tone image expansion and complex-input behavior;
- dirty recomputation after either parameter changes;
- output `fs_Hz` propagation through ADC → PFB;
- serialization round-trip;
- missing fields in legacy JSON;
- clamping invalid NCO and decimation values;
- empty-input output metadata.

Existing ADC tests that assert the current fixed behavior remain valid under the compatibility defaults. Project persistence coverage belongs with the existing project-file tests if the test-registration ceiling permits; otherwise use the standalone executable.

## Acceptance Criteria

- Users can select decimation `1/2/4/8` and a signed NCO factor in the ADC inspector.
- NCO tuning is always interpreted relative to the ADC input `Fs`.
- Output `fs_Hz` is exactly `Fs/D` and downstream PFB processing receives it.
- The ideal DDC passband is exactly the output Nyquist span.
- Existing projects without the new fields retain today's ADC behavior.
- Invalid saved values are constrained without crashing or producing invalid sample rates.
- Existing ADC, PFB, signal-domain, and project persistence behavior remains passing.

## Alternatives Considered

### Dedicated DDC configuration type

Rejected for this release. It would create a reusable abstraction with one current consumer and add API surface without reducing the requested change.

### Full hardware-style ADC model

Rejected for this release. A real FIR and explicit alias-rejection model would require additional filter controls and substantially expand the DSP and test surface. The current ideal frequency-domain filter is sufficient for configurable tuning and rate behavior.

### Neutral defaults (`D=1`, NCO `0`)

Rejected for compatibility. The existing simulator deliberately behaves as a fixed `Fs/4` DDC with decimation by two, and changing that default would alter current ADC outputs and downstream PFB sample rates. New controls make the existing behavior explicit without silently changing projects.

## Risks and Mitigations

| Risk | Mitigation |
|---|---|
| NCO interpreted relative to output rather than input rate | Store and document `nco_fs_fraction` as a factor of input `Fs`; test while changing `D`. |
| Decimation creates an invalid or aliased output span | Restrict choices to `1/2/4/8` and derive the ideal passband from `Fs/(2D)`. |
| Legacy projects change behavior | Missing fields deserialize to `D=2`, NCO `0.25`. |
| PSD is incorrectly treated as integrated noise | Keep PSD values unchanged and test integrated bandwidth behavior separately. |
| New tests disappear on MinGW | Use a standalone executable if the shared registration ceiling blocks additional cases. |
