# Spec: PFB Filter Calculator + Real-Prototype Engine Model

Status: proposed

## Objective

Give the user a dockable filter-calculator tool that validates the PFB channelizer's actual configuration (M channels, K taps per branch, Kaiser beta) against an RF-style adjacent-channel rejection target and recommends manual adjustments — plus, as the enabling change, correct the engine's prototype model so the numbers the tool reports are the numbers the simulator enforces.

Investigation showed the current engine (`pfb_channelizer_engine.cpp`) models each channel as a narrow windowed-sinc bin, not a tiling polyphase prototype:

- Response `|kaiser(x)·sinc(K·π·x)|` with `x` = offset / channel width has its first null at `x = 1/K` (e.g. −3 dB at ~5.5% of a channel for K=8) and an exact null at the nominal channel edge x=0.5 for every even K.
- Because each channel integrates `weight²` over |x|≤1 (integral ≈ 0.12 for K=8) and presents that power as density over the full channel width, a flat-spectrum input comes out ~9 dB low at K=8 (up to ~15 dB at K=32) in the full-band output. `[INFERENCE]` — derived from the engine source formula and integration code, not an observed run.

A real M-channel, K-taps-per-branch PFB prototype is a lowpass with passband ±0.5 channel, transition ~1/K channel wide, and a Kaiser-beta-controlled stopband; channels tile the band and flat noise stays flat.

## Scope

### Included

1. A shared, ImGui-free PFB prototype core that synthesizes the real windowed-sinc Kaiser prototype and evaluates its response (single source of truth for engine and tool).
2. Engine model fix: the PFB engine delegates its channel weights to that core; the old analytic `prototypeResponse`/`kaiserWindow` are deleted. Parameters, channel structure, slice semantics, cache guards, and serialization stay unchanged.
3. A dockable Filter Calculator tool window that shows the current M/K/beta's achieved metrics against a user rejection target, plots the prototype response, and applies M/K/beta to a selected PFB component.
4. Metrics: −3 dB passband half-width; band-edge loss; adjacent-channel rejection |H(1.0)|; far-adjacent floor; total taps K·M; flat-noise tilt.
5. Manual guidance text when the rejection target is missed (no solver).
6. Tests: bounded-behavior unit tests for the core and metrics; sweep of existing PFB tests that encoded the old narrow model; a flat-noise tiling regression guard.

### Explicitly Not Included

- Spec-in design mode (transition width / rejection → M/K/beta).
- Auto-solve / one-click "meets target" recommendation.
- Real per-channel decimation, per-channel filtering rework, or zoom-FFT mode (ROADMAP #17).
- Serializing or exporting real taps; switching the engine to actual tap-based filtering.
- Equiripple (Remez) or non-Kaiser windows.
- Editing filters of other component types.

## Prototype Model Semantics

All semantics must be explicit in the core API and tests.

- Prototype lowpass: `h[n] = w_kaiser[n] · (1/M) · sinc((n − (N−1)/2)/M)`, `N = K·M`, Kaiser window over N samples with parameter beta, DC-normalized so H(0) = 1 exactly.
- Response `H(x)` is the DTFT magnitude of `h` evaluated at normalized offset `x` in channel-width units (`f/Fs = x/M`). H(0)=1 (0 dB), H(0.5) ≈ −6.0 dB (band edge; the classic edge-to-edge contact of critically sampled channels), and H(1.0) is the adjacent-channel-center rejection driven mainly by beta (e.g. ≈ −83 dB at M=32/K=8/beta=8, ≈ −120 dB at beta=12, ≈ −110 dB at K=16/beta=8).
- The engine keeps its existing slice semantics: a channel collects grid bins with |offset| ≤ channel width and applies the prototype magnitude as the bin weight; tones pass when |offset| ≤ channel width. Only the weight function changes (old analytic product → real H).
- Metrics are computed from the shared core so tool numbers and simulator behavior can never drift.

### Metric definitions (x = offset ÷ channel spacing)

- −3 dB passband half-width: smallest x with H(x) ≤ −3 dB. Expect ~0.39–0.47 channel for practical designs.
- Band-edge loss: H(0.5) in dB (≈ −6.0 for all designs; shown for education).
- Adjacent-channel rejection (headline, compared to the user target): H(1.0) in dB.
- Far-adjacent floor: max H(x) over x ∈ [1.0, 1.5] in dB.
- Total taps: N = K·M.
- Flat-noise tilt: `10·log10(∫_{|x|≤1} H²(x) dx)` in dB; expect ≈ −0.6 dB at 32/8/8, within [−1.5, −0.1] for practical designs. Guards that the corrected model tiles (old model: ≈ −9 dB).

## Proposed Contracts

Names may change during implementation; boundaries must remain.

- `PfbFilterDesign(int M, int K, double beta)` — synthesizes taps; `double responseAt(double x)` returns |H(x)|; `int tapCount()`; `double beta()` etc. Pure, no ImGui/app deps, lives in `pfb_channelizer` module.
- `PfbFilterMetrics` — plain struct holding the §Metrics values, produced by a pure function from a `PfbFilterDesign`.
- Target comparison and guidance text are pure functions (`compareToTarget(metrics, targetDb)`, `guidanceText(...)`) returning a state (meets / within-10-dB / misses) and a short human string. ImGui-free, unit-testable.
- The widget (`pfb_calculator_widget.h/.cpp` in `app/`) is the only consumer that touches ImGui, the graph selection, and the engine setters.

## User Workflow

1. Open the Filter Calculator window (Window menu, dockable, layout-persisted).
2. Target a PFB: auto-follows a single selected PFB node; otherwise chosen from a "Target: PFB n" combo; with no PFB in the project the tool runs standalone (Apply disabled).
3. Adjust M/K/beta and the rejection target; see the response plot, metrics table, and guidance immediately.
4. Press Apply to write M/K/beta into the targeted PFB engine.
5. Retargeting another PFB pulls that engine's current M/K/beta first.

## Compatibility

- Project files: serialization untouched (M/K/beta only) — existing projects load and save unchanged.
- Scene behavior changes by design: tones near channel edges go from ~0 weight to ~−6 dB; a tone at an adjacent channel center now appears at ~H(1.0) (−83 dB class) instead of exactly 0; full-band flat noise rises to tile within ~1 dB instead of sitting ~9–15 dB low.
- PFB unit tests asserting the old narrow-model shape are updated to the real-prototype expectations (bounded, not golden-value overfit). Reconnect/serialization tests (test_issue37/70) are expected unaffected; verified during implementation.

## Testing Strategy

### New core/metrics tests (`tests/test_pfb_filter_design.cpp`)

Behavior with bounds, not exact current defaults:

- taps are real and symmetric (linear phase); tap sum ≈ 1 (DC gain 1);
- H(0) = 1; H(0.5) ∈ [−7, −5] dB;
- H(1.0) < −40 dB @ (32,8,8) and < −100 dB @ (32,16,12);
- −3 dB half-width ∈ (0.30, 0.49) for the four reference configs;
- flat-noise tilt ∈ [−1.5, −0.1] dB @ (32,8,8);
- metric target-compare hit/miss/within states; guidance non-empty when missing, empty when meeting;
- responseAt symmetry H(x) = H(−x).

### Existing PFB test sweep

- `test_pfb.cpp`: replace assertions encoding the old narrow shape; add a regression guard that flat full-spectrum noise through M=32/K=8 comes out within ~1 dB of flat (old model dipped ~9 dB).

### Widget/UI

- Add an Apply-semantics check (writes M/K/beta to the targeted engine; pull-on-retarget) only if the existing UI-test harness (`test_engine/ui_tests.cpp`) makes it cheap; otherwise rely on core/metrics unit coverage plus the engine setters already covered by tests.

## Verification Commands

Focused during implementation:

```bash
cmake --build build --target <focused-target>
ctest --test-dir build -R '<pfb|filter-design>' --output-on-failure
```

Full:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
scripts/format.sh --check
```

## Acceptance Criteria

- The engine and the calculator derive channel weights / metrics from one shared prototype core; deleting the old analytic functions changes no serialized state.
- A flat-spectrum input through M=32/K=8 tiles within ~1 dB in the full-band output (regression guard).
- The calculator reports the §Metrics for the current M/K/beta, colors the rejection vs the target (green ≥ target, amber within 10 dB, red below), and shows non-empty guidance when the target is missed.
- Apply writes M/K/beta into the targeted PFB through the existing setters; retarget pulls the engine's current values.
- The tool works standalone when no PFB exists in the project.
- Existing projects load unchanged; new/updated core, metric, and engine tests pass.

## Risks and Mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| Old-model assertions scattered through PFB tests | Broken test suite | Sweep `test_pfb.cpp`; keep reconnect/serialization tests untouched unless evidence says otherwise |
| Scene behavior change surprises users | Existing PFB demos look different (notches gone, edge tones audible/visible) | Documented in spec + release notes; part of the accepted model fix |
| O(grid·N) prototype recompute lags at extreme settings | Slider drag lag at M=2048/K=64 | Cache taps + weights on (M,K,beta,grid,Fs) change only; mark pathological sizes as a known ceiling (`ponytail:` comment) |
| H(1.0) alone understates leakage of wide adjacent signals | Misleading rejection claim | Near-edge response is visible in the plot and guidance; far-adjacent floor shown as secondary metric; guard-band note in guidance |
| Widget selection plumbing diverges from inspector | Confusing dual selection | Reuse the inspector's findSelected/anchored-combo pattern for the PFB target list |

## Open Questions Resolved Conservatively

1. Headline rejection number: |H(1.0)| (adjacent-channel center), the standard spec point for full-band neighbors; near-edge behavior remains visible via the plot and band-edge loss readout.
2. Guidance = manual text only; no solver (user preference).
3. No persistence of tool state (it re-derives from the targeted PFB); consistent with "recompute from live config."
4. Widget Apply is explicit (pull-on-retarget, push-on-Apply) rather than live-linked, avoiding fight between two editors of the same engine.
