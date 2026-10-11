# Agent skills — AGENTS.md

## Purpose

Repo-local skills that teach coding agents to use this repository's own interfaces. Each skill is a folder whose `SKILL.md` an agent loads on demand.

## Ownership

- Each `<skill-name>/` folder owns its `SKILL.md` and any reference file beside it.
- A skill's facts come from the code it describes. For `using-rf-sim-mcp`, that is `agent/` (catalog, API, bridge), which `agent/AGENTS.md` governs.

## Local Contracts

- The folder name matches the frontmatter `name`: lowercase, hyphenated, verb-first (`using-rf-sim-mcp`).
- `description` starts with "Use when", is third person, and lists triggers and symptoms only. It never summarizes the workflow.
- `SKILL.md` stays under 500 words. Heavy detail (error tables, field lists, option matrices) goes in a `reference.md` beside it, linked by relative path. Do not use `@` links, which force-load files.
- Skills state only behavior verified against code or tests. When that code changes, update the skill in the same change.

## Work Guidance

- Create or revise skills with the `writing-skills` workflow: run a blind baseline without the skill, then re-test with it, using the scenarios that matter.

## Verification

- No automated check reads skill text. The agent suite documented in `agent/AGENTS.md` (`ctest --test-dir build -R 'test_agent_|test_mcp_bridge' --output-on-failure`) covers the behavior that `using-rf-sim-mcp` describes.

## Child DOX Index

No child AGENTS.md. Skill directory:

- [using-rf-sim-mcp/](using-rf-sim-mcp/) — rf-sim MCP usage. `SKILL.md` holds the workflow and rules; `reference.md` holds error codes, field lists, and tool details.
