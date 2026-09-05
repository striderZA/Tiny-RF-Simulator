# AGENTS.md

AI coding agent guidance for the RF Simulator codebase.

All project conventions — development setup, build commands, architecture, code style, and git workflow — are documented in [CONTRIBUTING.md](CONTRIBUTING.md). Read that file before making changes.

Additional references:

- [README.md](README.md) — project overview, features, quick start
- [Architecture overview](openwiki/architecture/overview.md) — full architecture deep-dive (engine+widget pattern, signal chain, subcircuit groups, dirty-flag caching)
- [`.clang-format`](.clang-format) — code style rules
- [`.github/workflows/ci.yml`](.github/workflows/ci.yml) / [`.github/workflows/release.yml`](.github/workflows/release.yml) — PR CI and tag release configuration

# DOX framework

- DOX is highly performant AGENTS.md hierarchy installed here
- Agent must follow DOX instructions across any edits

## Core Contract

- AGENTS.md files are binding work contracts for their subtrees
- Work products, source materials, instructions, records, assets, and durable docs must stay understandable from the nearest applicable AGENTS.md plus every parent AGENTS.md above it

## Read Before Editing

1. Read the root AGENTS.md
2. Identify every file or folder you expect to touch
3. Walk from the repository root to each target path
4. Read every AGENTS.md found along each route
5. If a parent AGENTS.md lists a child AGENTS.md whose scope contains the path, read that child and continue from there
6. Use the nearest AGENTS.md as the local contract and parent docs for repo-wide rules
7. If docs conflict, the closer doc controls local work details, but no child doc may weaken DOX

Do not rely on memory. Re-read the applicable DOX chain in the current session before editing.

## Update After Editing

Every meaningful change requires a DOX pass before the task is done.

Update the closest owning AGENTS.md when a change affects:

- purpose, scope, ownership, or responsibilities
- durable structure, contracts, workflows, or operating rules
- required inputs, outputs, permissions, constraints, side effects, or artifacts
- user preferences about behavior, communication, process, organization, or quality
- AGENTS.md creation, deletion, move, rename, or index contents

Update parent docs when parent-level structure, ownership, workflow, or child index changes. Update child docs when parent changes alter local rules. Remove stale or contradictory text immediately. Small edits that do not change behavior or contracts may leave docs unchanged, but the DOX pass still must happen.

## Hierarchy

- Root AGENTS.md is the DOX rail: project-wide instructions, global preferences, durable workflow rules, and the top-level Child DOX Index
- Child AGENTS.md files own domain-specific instructions and their own Child DOX Index
- Each parent explains what its direct children cover and what stays owned by the parent
- The closer a doc is to the work, the more specific and practical it must be

## Child Doc Shape

- Create a child AGENTS.md when a folder becomes a durable boundary with its own purpose, rules, responsibilities, workflow, materials, or quality standards
- Work Guidance must reflect the current standards of the project or user instructions; if there are no specific standards or instructions yet, leave it empty
- Verification must reflect an existing check; if no verification framework exists yet, leave it empty and update it when one exists

Default section order:
- Purpose
- Ownership
- Local Contracts
- Work Guidance
- Verification
- Child DOX Index

## Style

- Keep docs concise, current, and operational
- Document stable contracts, not diary entries
- Put broad rules in parent docs and concrete details in child docs
- Prefer direct bullets with explicit names
- Do not duplicate rules across many files unless each scope needs a local version
- Delete stale notes instead of explaining history
- Trim obvious statements, repeated rules, misplaced detail, and warnings for risks that no longer exist

## Closeout

1. Re-check changed paths against the DOX chain
2. Update nearest owning docs and any affected parents or children
3. Refresh every affected Child DOX Index
4. Remove stale or contradictory text
5. Run existing verification when relevant
6. Report any docs intentionally left unchanged and why
## User Preferences

- The PFB channelizer is physically downstream of an RF ADC only; direct RF-chain-to-PFB links must be rejected.

- ADC DDC decimation is configurable as 1/2/4/8 and NCO tuning is stored as a normalized factor of the ADC input sample rate; legacy ADC state defaults to decimation 2 and NCO +0.25×Fs.
- When the user requests a durable behavior change, record it here or in the relevant child AGENTS.md
- Superpowers plan/spec documents are working materials and must not be committed.

## Release Contract

- `.github/workflows/release.yml` validates tag versions and requires a matching changelog section before running the release matrix or creating a draft.
- `CHANGELOG.md` is the source of truth for GitHub release descriptions; `cliff.toml` is not used by the release workflow.

## Child DOX Index

- [common/AGENTS.md](common/AGENTS.md) — Header-only data model shared by all modules (`SignalNode`, `Spectrum`, `IComponentEngine`, `Group`, `GroupBoundaryPin`, etc.)
- [app/AGENTS.md](app/AGENTS.md) — Application orchestrator (`RfSimulatorApp`, `ComponentRegistry`, `InspectorPanel`)
- [tests/AGENTS.md](tests/AGENTS.md) — Catch2 unit + benchmark tests
- [help/AGENTS.md](help/AGENTS.md) — Help window widget with data-driven quick reference content
- [layout/AGENTS.md](layout/AGENTS.md) — Exe-relative ImGui layout persistence (default + named presets)
- [tutorial/AGENTS.md](tutorial/AGENTS.md) — Guided first-run walkthrough with panel highlighting and exe-relative completion marker

<!-- OPENWIKI:START -->

## OpenWiki

This repository has a generated `openwiki/` evidence index. It is optional just-in-time context, not required startup reading.

- Treat source code and tests as authoritative. A brief's unknowns and review items are verification gaps, not automatic requirements.
- Prefer the narrowest quiet validation that proves the changed behavior. Preserve complete failure output.

The scheduled OpenWiki GitHub Actions workflow refreshes the repository wiki. Do not hand-edit generated OpenWiki pages unless explicitly asked; prefer updating source code/docs and letting OpenWiki regenerate.

<!-- OPENWIKI:END -->
