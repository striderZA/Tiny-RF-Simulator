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
| `manual/` | Published user documentation (MkDocs) | Humans |
| `docs/` | Engineering references | Humans |
| `WATCHDOG.yml` | Review-advisor config for tooling; not a DOX file | Tooling owner |

Link to the owning document instead of restating its rules or commands on another surface.

## Shared UI Input Contract

- `utils::inputDouble()` and `utils::inputFrequency()` (`core/include/utils.h`) bound user edits, not displayed values. A value outside the limits is shown as is, flagged (amber field, tooltip naming the range), logged once, and changed only by a user edit.
- A user edit clamps into the limits. An edit that clamping would reverse (a `+` step on a value above the range) is ignored. An edit that clamps back onto the current value reports no change.
- Callers pass a local copy and write it back only when the helper returns true.
- Spectrum-analyzer RBW and VBW span 1 kHz to 100 MHz. The range lives at the widget's `utils::inputFrequency()` call, and its trailing `displayUnit_Hz` picks the unit. Engine setters stay unclamped so tests and API callers can drive any value.
- The analyzer reports noise power integrated over its effective RBW. Its sampled noise kernel is normalized per
  output bin, including sweep edges and RBWs below source-bin spacing. The tone kernel remains peak-normalized;
  changing RBW clears Max Hold, Min Hold, and Video Average history.

## Touchstone Parsing Contract

- Two-port Touchstone v2.0 files require `[Two-Port Data Order]`; the parser honors `12_21` and `21_12`.

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
- [manual/AGENTS.md](manual/AGENTS.md) — User-facing MkDocs manual: page ownership, publish guard, docs contract
- [scripts/AGENTS.md](scripts/AGENTS.md) — Release, changelog, and format scripts and the rules that bind them

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
