# Mission

<!--
  Owner: humans only. This file is on the protected list; the factory cannot edit it.
  It is the compression of the living spec (README.md, ROADMAP.md, CONTRIBUTING.md)
  down to the part an agent has to OBEY. When the product changes, this changes in
  the same human commit.
-->

**Derived from:** README.md, ROADMAP.md, CONTRIBUTING.md and the module AGENTS.md files
**Last reconciled with them:** 2026-09-09

## What Tiny RF Simulator is

A standalone desktop RF signal-chain simulator. The user composes components — signal
generators, amplifiers, attenuators, mixers, filters, splitters, combiners, coax,
equalizers, an RF ADC and a polyphase-filter-bank (PFB) channelizer — into a node
graph, wires their pins, and probes any node to see its live spectrum: discrete tones
plus a noise power-density on a frequency grid, with an IQ view for the digitized
path. It answers "what does this chain do to my signal?" before any hardware exists.

The design bakes in: single user, local machine, no runtime network dependency,
deterministic frequency-domain steady-state DSP, and project state the user owns as
`.rfsim` files on their disk.

## Who it is for

- RF engineers prototyping a receive/transmit chain — gain, noise figure, frequency
  plan, digitization and channelization — without instruments
- Students of RF: the effect of each block on tone power, noise floor and spectrum is
  visible per node
- Practitioners who have datasheet Touchstone (`.s*p`) files and want to drop them
  into a chain

Tiny RF Simulator is **not** an instrument (oscilloscope/spectrum-analyzer
replacement), **not** a communications-system simulator, and **not** a circuit
(SPICE) simulator.

## Core capabilities (in scope)

The factory may accept issues in these areas.

**Component DSP engines**
- New and improved passive/active component models (gain, NF, attenuation, mixing,
  filtering, splitting/combining, coax loss) as `*_engine` targets with no UI
  dependency
- Nonlinear behavior within the documented model (P1dB/OIP3 compression on
  amplifiers)
- Touchstone S-parameter import (1–4 port) with complex interpolation, and
  data-driven component definitions (`data_files`, JSON component library)

**Digitization chain**
- RF ADC: frequency-domain sampling, Nyquist-zone aliasing, noise spectral density,
  configurable decimation (1/2/4/8) and NCO tuning as a normalized factor of the input
  sample rate
- PFB channelizer: M channels, K taps, sampling ratio 1x (critical) or 2x
  (oversampled), channel Fs = ratio × Fs_in / M, filter-design calculator sharing the
  Kaiser prototype with the engine

**Graph, views and interaction**
- Node graph editing: placement, wiring, deletion, tooltips, subcircuit groups
  (snapshot editing; the DSP graph stays flat)
- Spectrum analyzer (RBW/VBW, trace modes, peaks, markers, PFB dual-trace), IQ plot,
  power meter, network analyzer views
- Component library browser (built-in, global, per-project scan roots)

**Persistence and first-run experience**
- `.rfsim` save/load of circuit, parameters, wiring and probes; unsaved-changes
  dialog; exe-relative layout presets; help window; guided first-run tutorial

**Cross-cutting quality**
- Dirty-flag caching semantics: results recomputed exactly when inputs change
- Catch2 unit/benchmark suites and imgui_test_engine UI automation

## Out of scope — the factory must never build this

**Hardware and captured signals**
- Connections to SDRs, VNAs, signal generators, spectrum analyzers, sound cards or
  any instrument driver / real-time signal input or output path. This product
  simulates; it never acquires or emits.
- Replay or comparison of captured measured data streams (instrument recordings).
  The product consumes static S-parameter *model* files (Touchstone) and
  user-parameterized signal definitions — not measurement logs.

**Product shape**
- Networking, multi-user editing, cloud sync, accounts, activation/licensing, or
  telemetry of any kind.
- A web or mobile front end; the UI is the native ImGui desktop application.
- Communications-system simulation (link budgets, BER estimation, protocol stacks,
  end-to-end transceivers). Chain-level modulation blocks on the roadmap are
  components, not a link simulator.
- General EDA: PCB layout, EM field solving, thermal analysis.

**Execution model**
- Embedded scripting or plugin VMs that run user code inside the app process (Lua,
  Python, JS). Component extension stays data-driven: JSON definitions and
  Touchstone files.
- SPICE-style circuit-level transient simulation. The DSP model stays
  frequency-domain steady-state plus the documented ADC/PFB path.

**Governance**
- Changing the Apache-2.0 license, relicensing, or adding proprietary-only
  components.

## Hard invariants — not tunable by any issue

These are not features. The factory cannot modify them even if an issue asks nicely,
gives a good reason, or calls it a bug. Changing one requires a human commit.

1. **Engines are UI-free.** Only widget files may include `imgui.h` / `implot.h` /
   `imnodes.h`; every `*_engine` target builds, runs and is tested headless. Tests and
   reuse depend on this split.
2. **The PFB channelizer is fed only by an RF ADC.** A direct RF-chain→PFB link must
   be rejected: the channelizer digitizes first, then channelizes, and accepting an
   RF-coupled input would model hardware that cannot exist.
3. **Saved projects stay loadable.** Loading legacy `.rfsim` files must keep working;
   absent fields take documented legacy defaults (ADC decimation 2, NCO +0.25×Fs, PFB
   critically sampled 1x). The user's files on disk are the product's memory.
4. **ADC decimation is restricted to 1/2/4/8 and PFB sampling ratio to 1x/2x** —
   physically implementable ratios, clamped to the nearest supported value.
5. **The factory cannot modify governance files.** `MISSION.md`,
   `FACTORY_RULES.md` and the `AGENTS.md` convention files are the constitution. A
   PR touching any of them is an automatic reject.
6. **The factory cannot modify its own judge.** `harness/`, `.factory/locks/` and
   `.factory/holdout/` define what "working" means here. Adding an assertion is
   always welcome; removing or loosening one is a human decision, always.

## Allowed evolutions

Explicitly in scope, so the factory does not reject them as architectural drift:

- New component modules following the established engine + widget pattern (one
  directory, `simulator::*` CMake aliases, standalone Catch2 coverage)
- Richer data-driven authoring: schema additions for JSON component definitions,
  more S-parameter handling, without breaking existing files
- Performance work — dirty-flag cache, allocations, benchmarks — that leaves numeric
  results unchanged

## Definition of done

Every change the factory ships clears all three gates.

**Gate 1 — static checks and tests pass.** `python harness/ci.py` is green: CI-
equivalent clang-format-18 check over the full scanned file set, then a clean
`cmake -B build -G Ninja` configure + build, then the complete `ctest` suite
(Catch2 unit tests, standalone test executables, and the imgui_test_engine UI
suite).

**Gate 2 — numeric truth is asserted.** Any new or changed component behavior gets
unit coverage with `Catch::Approx` asserting the physics (gain adds in dB, noise
scales with attenuation, channel Fs equals ratio × Fs_in / M, decimation clamps to
the supported set) — not a screenshot-level smoke.

**Gate 3 — the end-to-end path passes as a real user.**

1. Start the app (a real window; the default graph seeds an unconnected generator
   and amplifier).
2. Wire the generator into the amplifier, then change the amplifier gain.
3. Probe the node: the tone power moved by exactly the gain in dB, noise floor
   raised accordingly.
4. Save the circuit, restart, reopen: the chain, parameters and probe are intact.

This runs on every change that touches runnable code, including ones that "seem
unrelated". It is not optional. (Runtime automation for Gate 3 is wired through
`harness/END-TO-END.md` + the Archon runtime host; see FACTORY.md for the current
integration state.)

## Open questions — decisions nobody has made yet

These are undecided, not forbidden. **The factory may propose an answer to any of
them**, build against it, and record what it assumed — the merge is then held for a
human, so nothing ships on a guess and nothing stops for one.

- **Q1** Time-domain view (roadmap #6): a scope-style window on any node, or a
  dedicated instrument component in the graph?
- **Q2** PFB multi-channel output pins (roadmap #17): all M channels as separate pins,
  or a channel-select pin with an index parameter?
- **Q3** Modulation components (roadmap #20): which modulation set ships first, and
  does phase carry through the chain at baseband or RF?

**Except these, which do stop the factory** — irreversible-list items, not open:

- A new `.rfsim` schema version that cannot load files written by the previous
  version, or any automatic rewrite of the user's file on save without an explicit
  user action.
- Anything that phones home (telemetry, update checks that send identifiers, license
  activation).

Once answered, an entry moves to `.factory/decisions.md` with its answer and date,
and stops being asked. **A decision is asked once.**

## What the factory does NOT own — permanently human

- **Physical fidelity:** is the model believable to an RF engineer comparing it
  against measured hardware? A green gate means the code says what the mission says,
  not that nature agrees.
- **Visual and interaction quality:** dock layout readability, spectrum plot
  legibility, whether two states read as different.
- **Roadmap direction:** what to build next is a taste decision; the factory builds
  what is filed and triages it against this mission.

The factory owns the domain rules — the signal model, the engine/widget contract,
the persistence format, the DSP math: the layer whose correctness can be asserted.
The list above is reviewed by a human, on purpose, forever.
