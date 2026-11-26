# AGENTS.md

AI coding agent guidance for the RF Simulator codebase.

Human conventions (setup, build, testing, style, git, CI, and release) live in [CONTRIBUTING.md](CONTRIBUTING.md). Read it before changing code. Other references: [README.md](README.md) for the overview, [`.clang-format`](.clang-format) for style rules, and [openwiki/architecture/overview.md](openwiki/architecture/overview.md) for the generated architecture reference (retrieval only, not binding).

# DOX framework

DOX is this repository's AGENTS.md hierarchy. Agents follow it on every edit.

## Core Contract

- Within the DOX hierarchy, AGENTS.md files are binding work contracts for their subtrees. `.agents/` and `.githooks/` are outside DOX and MUST NOT contain AGENTS.md.
- Work products, source materials, instructions, records, assets, and durable docs must stay understandable from the nearest applicable AGENTS.md plus every parent AGENTS.md above it.
- Parents hold broad rules and a Child DOX Index. Children hold local rules. A child may tighten a parent rule and never weaken DOX.

## Read Before Editing

1. Read the root AGENTS.md.
2. List every file and folder you expect to touch.
3. Walk from the root to each target path, reading every AGENTS.md on the route. When a parent index lists a child whose scope contains the path, read that child and continue.
4. The nearest AGENTS.md is the local contract. If docs conflict, the closer doc controls local details.

Re-read the applicable chain in the current session. Do not rely on memory.

## Update After Editing

Every meaningful change ends with a DOX pass:

1. Re-check the changed paths against the DOX chain.
2. Update the nearest owning AGENTS.md when purpose, scope, ownership, structure, contracts, workflows, inputs, outputs, permissions, side effects, artifacts, a user behavior preference, or the AGENTS.md set (creation, deletion, move, rename, index) changes.
3. Update parents when their structure, ownership, workflow, or child index changes. Update children when a parent change alters their local rules.
4. Refresh every affected Child DOX Index, and delete stale or contradictory text immediately.
5. Run existing verification when relevant. Report any doc left unchanged, and why.

A change that alters no behavior or contract may leave docs unchanged, but the DOX pass still happens.

## Hierarchy and Child Docs

- The root is the DOX rail: project-wide rules, global preferences, durable workflow rules, and the top-level Child DOX Index.
- A child owns its domain rules and its own index. Each parent says what its direct children cover and what stays with the parent.
- Create a child AGENTS.md when a folder becomes a durable boundary with its own purpose, rules, responsibilities, workflow, materials, or quality standards.
- DOX excludes `.agents/` and `.githooks/`. They MUST NOT contain AGENTS.md and must not appear in a Child DOX Index. Their rules live in Agent Rules (skills), CONTRIBUTING.md (hooks), and `.gitignore` (agent-folder ignore rules).
- Sections, in order: Purpose, Ownership, Local Contracts, Work Guidance, Verification, Child DOX Index. Work Guidance names current standards only. Verification names checks that exist. Leave either empty if none exist.
- Work Guidance must reflect the current standards of the project or user instructions; if there are no specific standards or instructions yet, leave it empty
- Verification must reflect an existing check; if no verification framework exists yet, leave it empty and update it when one exists
- The closer a doc is to the work, the more specific and practical it must be

## Style

- Concise, current, operational. Document stable contracts, not diary entries. Delete stale notes instead of explaining history.
- Trim obvious statements, repeated rules, misplaced detail, and warnings for risks that no longer exist.
- Broad rules belong in parents and concrete details in children. Do not copy a rule into several files; point to the owning doc.
- Do not duplicate rules across many files unless each scope needs a local version
- Prefer direct bullets with explicit names.

## Document Surfaces

| Surface | Authority | Edited by |
|---|---|---|
| AGENTS.md (this tree) | Binding subtree contracts and the DOX pass | Humans and agents, through the DOX pass |
| `.agents/skills/` and `.githooks/` | Not DOX. Skill rules: Agent Rules here. Hook rules: CONTRIBUTING.md | Humans and agents, under those rules |
| CONTRIBUTING.md | Human conventions: setup, testing, style, git, CI, release | Humans |
| `openwiki/` and the OPENWIKI block below | Generated, optional, non-binding evidence index | Humans, by manual OpenWiki regeneration after changing `openwiki/INSTRUCTIONS.md` |
| `docs/` | User documentation (plain Markdown, GitHub-browsed) plus engineering references under `docs/resources/` | Humans, per [docs/AGENTS.md](docs/AGENTS.md) |
| `WATCHDOG.yml` | Review-advisor config for tooling; not a DOX file | Tooling owner |

Link to the owning document instead of restating its rules or commands on another surface.

## Shared UI Input Contract

- `utils::inputDouble()` and `utils::inputFrequency()` (`core/include/utils.h`) bound user edits, not displayed values. A value outside the limits is shown as is, flagged (amber field, tooltip naming the range), logged once, and changed only by a user edit.
- A user edit clamps into the limits. An edit that clamping would reverse (a `+` step on a value above the range) is ignored. An edit that clamps back onto the current value reports no change.
- Callers pass a local copy and write it back only when the helper returns true.
- Spectrum-analyzer RBW and VBW span 1 kHz to 100 MHz. The range lives at the widget's `utils::inputFrequency()` call, and its trailing `displayUnit_Hz` picks the unit. Engine setters stay unclamped so tests and API callers can drive any value.
- The analyzer reports noise power integrated over its effective RBW. Its sampled noise kernel is normalized per
  output bin, including sweep edges and RBWs below source-bin spacing. The tone kernel remains peak-normalized;
  changing RBW, VBW, or Avg Count clears Max Hold, Min Hold, and Video Average history.

## Spectrum Analyzer Measurement Contract

- **Measure on** is view state, not project data: it does not enter `ProjectSerializer`, does not mark the project dirty, and is not undo.
- `MeasurementSelection` (`spectrum_analyzer/include/spectrum_analyzer_measurement.h`, UI-free) owns the chosen probe identity `(node pointer, output index)` and resolves it against the current probe list and trace visibility. Identity survives reordering, so adding or removing another probe does not retarget the selection.
- `All visible` (no selection) keeps the combined readout. A chosen probe scopes Peak, average noise, markers, Snap to Peak, and Next Peak to that probe's own trace on its own grid. Held values still advance for every probe.
- A chosen probe that is hidden or removed resolves to the all-visible readout with `Fallback::Hidden` or `Fallback::Removed`, and the panel readout names the reason. Showing or re-probing the trace resumes the selection.
- Changing the measurement source clears every marker's cached peak list (`MarkerModel::clearPeakCaches()`), so Next Peak adopts the selected trace's peaks rather than cycling the previous source's.
- A node probed at two outputs keeps one probe slot per output; the selected output's spectrum is added to the readout set even though the active-node list holds only the node's last-probed output.

## Spectrum Analyzer Marker Contract

- Markers are view state, never project data: they do not enter `ProjectSerializer`, do not mark the project dirty, and are not undo records.
- `MarkerModel` (`spectrum_analyzer/include/spectrum_analyzer_markers.h`, UI-free) owns positions, selection, delta flags, per-marker peak caches, and the session token. M1 (index 0) is the reference and always reads absolute; delta markers read frequency and dB offsets from it, and report an unavailable reference while M1 is off.
- Marker readouts resolve to the nearest bin of the same jitter-free combined visible-trace axis as the Peak readout, so they follow trace visibility, Max Hold/Min Hold, and Video Avg.
- A manual move (drag, Ctrl+click placement, or a peak-table row selection) clears that marker's cached peak list; re-placing the current value keeps it, so the widget's per-frame drag-line update does not invalidate Next Peak.
- `RfSimulatorApp` persists the marker session token under `[SpectrumAnalyzer] Markers` through `SessionState` (Windows only), like window visibility.

## Spectrum Analyzer Peak Table Contract

- `PeakTableModel` (`spectrum_analyzer/include/spectrum_analyzer_peak_table.h`, UI-free) owns the show/hide flag, the up-to-five rows, and the selected row. Rows come from `findPeaks()` on the same jitter-free readout trace as Peak and the markers, so they follow **Measure on**, trace visibility, and Max Hold, Min Hold, and Video Avg.
- Rows are sorted by descending amplitude. Row 1 is the strongest peak and anchors every delta column at zero; the other rows read their frequency and dB offsets from it.
- Selecting a row, or stepping with `PeakTableModel::step()`, moves the selected marker onto that peak and enables the marker first. Selection clamps to a shrinking search and wraps when stepping.
- The table is view state: it does not enter `ProjectSerializer`, does not mark the project dirty, and is not undo. `RfSimulatorApp` persists its visibility under `[SpectrumAnalyzer] PeakTable` through `SessionState` (Windows only).

## Spectrum Analyzer Harmonic Measurement Contract

- `HarmonicModel` (`spectrum_analyzer/include/spectrum_analyzer_harmonics.h`, UI-free) owns the show/hide flag, the `useMarker()` switch, the search rules, and the last result; the widget supplies the selected marker's frequency.
- The measurement reads the same jitter-free readout trace/axis as Peak and the markers, so it follows **Measure on**, trace visibility, and Max Hold, Min Hold, and Video Avg.
- The fundamental is the strongest tone-like peak at least `kPeakClearance_dB` (6 dB) above the trace median, or the supplied marker frequency taken as given. A tone-like peak is a local maximum (a two-bin plateau counts, a flat run does not) with a 6 dB dip on both sides within one RBW (at least one bin). The search is sign-agnostic: a one-sided complex-baseband trace measures a negative-frequency tone, and on a mirrored real-domain trace an exact `+-f` tie prefers the positive image. A raised noise band is not a peak and reads `NoFundamental`, as does a 0 Hz fundamental (a marker left at the axis midpoint, or a dc tone), which has no harmonic sequence.
- Harmonics are searched within `max(rbw/2, bin width)` of `n*f0`. The displayed-span check runs first (`OutOfSpan`), then the resolution rule (`half_window >= |f0|/2` is `Unresolved`, and `Entry::rbw_limited` says whether the RBW or the bin spacing is the limit), then the peak must clear the same noise-floor threshold (`NotFound`). A fundamental with no clear tone is `NoFundamental`; a supplied marker outside the span is `FundamentalOutOfSpan`. The statuses are reported, never guessed.
- The block is view state: it does not enter `ProjectSerializer`, does not mark the project dirty, and is not undo. `RfSimulatorApp` persists its visibility under `[SpectrumAnalyzer] Harmonics` through `SessionState` (Windows only); the marker-fundamental switch is session-only.

## Spectrum Analyzer IM3 Measurement Contract

- `Im3Model` (`spectrum_analyzer/include/spectrum_analyzer_im3.h`, UI-free) owns the show/hide flag, the `useMarkers()` switch, the tone and product search rules, and the last result; the widget supplies the two marker frequencies and owns presentation.
- The measurement reads the same jitter-free readout trace/axis as Peak and the markers, so it follows **Measure on**, trace visibility, and Max Hold, Min Hold, and Video Avg.
- Auto-detection takes the two strongest tone-like peaks under the harmonic rule (a local maximum 6 dB clear of the trace median with a 6 dB dip on both sides within one RBW). A negative peak that mirrors a near-equal positive peak is the same tone's real-domain image and is not a second tone; a one-sided complex-baseband trace keeps its negative tones. The pair is ordered by frequency (f1 lower); equal levels resolve lower frequency first. Fewer than two peaks is `NeedTwoTones`.
- Supplied M1/M2 frequencies are taken as given and ordered by frequency. Either outside the span is `TonesOutOfSpan`; equal frequencies, or 0 Hz, are `NeedTwoTones`.
- Products `2*f1-f2` (nearest f1) and `2*f2-f1` (nearest f2) are checked in order: outside span, unresolved (`half_window >= spacing/2`, with `rbw_limited` naming the RBW or the bin spacing), then a peak search within `max(rbw/2, bin)`; no peak above the threshold is `NotFound`. `relative_dBc` is relative to the nearest fundamental.
- TOI is a spectrum-derived output-intercept estimate `(P1 + P2 - dBc)/2`, averaged over the measured products, valid when at least one product is measured. It is not the Receiver Requirements IIP3 test.
- The block is view state: it does not enter `ProjectSerializer`, does not mark the project dirty, and is not undo. `RfSimulatorApp` persists its visibility under `[SpectrumAnalyzer] Im3` through `SessionState` (Windows only); the marker-tone switch is session-only.

## Touchstone Parsing Contract

- Two-port Touchstone v2.0 files require `[Two-Port Data Order]`; the parser honors `12_21` and `21_12`. Files with three or more ports use row-major parameter order.
- Reference impedance: the option line `R n` sets it; v2.0 `[Reference]` overrides it and may span lines. A `[Reference]` with differing per-port values is refused and logged, since per-port renormalisation is not implemented. `R` is accepted by the parser; `SParameterData::load()` refuses a non-positive R.
- `SParameterData::load()` renormalises a uniform reference R to 50 Ω: Z = R(I−S)⁻¹(I+S), S' = (Z−50)(Z+50)⁻¹. `TouchstoneParser::parse()` returns file values and the file reference unchanged.
- The agent `data_file_read` tool reports raw file samples with `reference_impedance_ohm`; it does not renormalise. Read the reference field to interpret them.

## S-parameter Contract

- `SParameterData::interpolate()` evaluates negative frequencies at `|f|` and returns the conjugate response, matching a real-valued network's frequency symmetry.

## Passive Coax Noise

- Coax cable models a passive lossy stage at 290 K: each output bin is input noise times power gain plus `kT(1−gain)`.

## Passive Splitter and Combiner Noise

- Splitter outputs pass `gain·noise_in` and add `kT(1−gain)` per bin, the attenuator model at the split gain (0.5, −3.01 dB). A kT input reads NF 3.01 dB.
- Combiner noise per bin is `Σ gain_i·noise_i` over connected inputs, plus `kT·max(0, 1 − Σ gain_i)`, plus `gain_i·kT` for each absent input (a 290 K termination). Manual mode uses gain 0.5 per input; S-parameter mode uses `|S21|²` and `|S31|²`. Both modes share this rule in `combinerBinNoise()`; one connected input reduces to the attenuator.

## Mixer Noise Contract

- `MixerEngine` scales input noise by conversion gain and adds it to both sidebands: each input bin's density `G·n` goes to the nearest output-grid bin at `|f − LO|` and at `f + LO`. Noise follows the double-sideband (DSB) convention, the same full-gain-per-sideband rule as tones: a flat input gives `2·G·S` in an output bin whose two images (`LO ± f`) both lie in the input band, and `G·S` where only one image exists (for example after an RF band-pass filter). Sideband images that fall beyond the output grid's edges are dropped, so the integrated output is not conserved on a finite grid. Mixer-added noise (NF) remains distributed over the output grid. This assumes the DSB convention; the LO power that would set the real sideband split is not modelled (issue #282).
- `MixerEngine` emits one tone per sideband (no merge, no snapping). Copies that differ only by rounding (`tone ± LO` computed separately) are grouped by the analyzer and power meter through `coherentToneWatts()`, which sums copies within 4 ulps of the largest tone frequency in the list as phasors. The analyzer reads real-domain spectra after conjugate-symmetric expansion, so each real tone appears at +f and -f at half power. Network-analyzer comb readouts still take the first matching tone, so their gain and NF are unchanged.

## ADC Output Grid Contract

- `AdcEngine` output labels are bin centres: `label_k = -Fs/(2D) + (k + 0.5)·df_out`, bin k covers `[label_k - df/2, label_k + df/2)`. The lower output edge `-Fs/(2D)` lies in bin 0.
- `SpectrumAnalyzerEngine::binPowerComponents` maps tones into the same cells. A tone on a negative half-bin edge goes to the bin above (`floor(x + 0.5)`, with a 1e-9-bin float tolerance at the lower edge).
- Even N puts DC on a half-bin tie, so a 0 Hz tone shows at +df/2. This is the accepted trade-off of the bin-centre convention.

## Agent Rules

- Durable behavior change requested by the user: record it in the closest owning AGENTS.md, or here for global behavior.
- Never run filesystem-wide searches (`find /`, `dir /s`, drive-root greps). They hang headless runs past the watchdog. Search within the repository.
- Planning and spec drafts (`docs/superpowers/`, `docs/specs/`, `.superpowers/`) are gitignored working material. Do not commit them.
- Releases: follow CONTRIBUTING.md (Release Process). The binding release-script rules live in [scripts/AGENTS.md](scripts/AGENTS.md).
- Repo skills live in `.agents/skills/<name>/`. The folder name matches the frontmatter `name` (lowercase, hyphenated, verb-first). The `description` starts with "Use when", is third person, and lists triggers and symptoms only, never the workflow.
- Keep `SKILL.md` under 500 words. Put error tables, field lists, and option matrices in a `reference.md` beside it, linked by relative path. Do not use `@` links, which force-load files.
- Skills state only behavior verified against code or tests. A skill's facts come from the code it describes; for `using-rf-sim-mcp`, that is `agent/`, governed by `agent/AGENTS.md`. Update a skill in the same change as the code it describes.
- Create or revise skills with the `writing-skills` workflow: run a blind baseline without the skill, then re-test with it, using the scenarios that matter.
- Each `<skill-name>/` folder owns its `SKILL.md` and any reference file beside it.
- Root `AGENTS.md` may reference OpenWiki. `openwiki/` holds no `AGENTS.md`: it is generated, non-binding content.

## Child DOX Index

- [common/AGENTS.md](common/AGENTS.md) — Header-only data model shared by all modules (`SignalNode`, `Spectrum`, `IComponentEngine`, `Group`, `GroupBoundaryPin`, etc.)
- [node_graph/AGENTS.md](node_graph/AGENTS.md) — Topology-only graph engine + ImNodes editor widget, schematic symbols, and the shared `rewireComponentInputs()` DSP pass
- [pfb_channelizer/AGENTS.md](pfb_channelizer/AGENTS.md) — PFB channelizer engine: ADC-only upstream, sampling ratio, and channel noise readout
- [power_meter/AGENTS.md](power_meter/AGENTS.md) — UI-independent total-power measurement engine and singleton observer panel
- [app/AGENTS.md](app/AGENTS.md) — UI-free circuit runtime and editor actions plus the application orchestrator (`CircuitRuntime`, `GraphEditorActions`, `RfSimulatorApp`, `ComponentRegistry`, `InspectorPanel`)
- [tests/AGENTS.md](tests/AGENTS.md) — Catch2 unit and benchmark tests
- [help/AGENTS.md](help/AGENTS.md) — Help window widget with data-driven quick reference content
- [layout/AGENTS.md](layout/AGENTS.md) — Exe-relative ImGui layout persistence (default and named presets)
- [tutorial/AGENTS.md](tutorial/AGENTS.md) — Guided first-run walkthrough with panel highlighting and exe-relative completion marker
- [test_flow/AGENTS.md](test_flow/AGENTS.md) — GUI-free test-flow (ATP) harness: flow-file schema, parameter sweeps, metric capture, JSON result export
- [agent/AGENTS.md](agent/AGENTS.md) — Agent protocol framing, shared errors, and agent-domain boundaries
- [docs/AGENTS.md](docs/AGENTS.md) — User documentation base: plain-Markdown page contract, vanilla-syntax rule, README index, broken-link gate
- [scripts/AGENTS.md](scripts/AGENTS.md) — Release, changelog, format, docs-link, and release-archive verification scripts and the rules that bind them

<!-- OPENWIKI:START -->

## OpenWiki

This repository has a generated `openwiki/` evidence index. It is optional just-in-time context, not required startup reading.

- Do not enumerate, preload, or search wikis at task start. Use retrieval when the user asks for it, when unfamiliar architecture or dependency behavior materially affects the task, or when source inspection leaves an important uncertainty. Stop once the question is grounded.
- When those conditions apply and OpenWiki retrieval tools are available, use `openwiki_search` for just-in-time context and `openwiki_read` for the relevant complete sections. If search returns `workspace_required`, ask which listed workspace to use and retry with its ID.
- Use `openwiki_list_workspaces` or `openwiki_list_wikis` when workspace membership itself needs to be discovered.
- If the retrieval tools are unavailable, read `openwiki/quickstart.md` and follow its links to the relevant pages.
- Treat source code and tests as authoritative. A brief's unknowns and review items are verification gaps, not automatic requirements.
- Prefer the narrowest quiet validation that proves the changed behavior. Preserve complete failure output.

The scheduled OpenWiki GitHub Actions workflow refreshes the repository wiki. Do not hand-edit generated OpenWiki pages unless explicitly asked; prefer updating source code/docs and letting OpenWiki regenerate.

<!-- OPENWIKI:END -->
