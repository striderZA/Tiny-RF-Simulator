# Agent folder — AGENTS.md

## Purpose

Coding-agent material for this repository. `skills/` holds the content meant for version control; everything else under `.agents/` is session state and stays ignored.

## Ownership

- This file owns the `.agents/` boundary and its `.gitignore` exception.
- `skills/` owns the repo-local skills; `skills/AGENTS.md` owns their standards and index.

## Local Contracts

- `.gitignore` ignores `.agents/*` and re-includes `.agents/skills/` and `.agents/AGENTS.md`. Git cannot re-include a file under an ignored parent, so any new tracked entry directly under `.agents/` needs its own `!` line.

## Work Guidance

- Follow `skills/AGENTS.md` for skill files.

## Verification

- Check ignore rules with `git check-ignore -q <path>`: exit 1 means the path is not ignored and can be added; exit 0 means it is ignored. Do not use `-v` for this, since it prints negated matches with exit 0. `git status --short --untracked-files=all` lists what git would add.
- `ctest --test-dir build -R 'test_agent_|test_mcp_bridge' --output-on-failure` covers the agent behavior the MCP skill describes.

## Child DOX Index

- [skills/AGENTS.md](skills/AGENTS.md) — Repo-local agent skills: file standards and the skill index
