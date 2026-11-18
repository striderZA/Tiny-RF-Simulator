# docs — AGENTS.md

## Purpose

User-facing documentation base: a flat folder of plain-Markdown pages browsed directly on GitHub. `docs/README.md` is the index. There is no site generator and nothing is published; `docs/resources/` holds engineering references, not user pages.

## Ownership

- `docs/*.md`: user pages, one topic per file, linked from `docs/README.md`.
- `docs/README.md`: the index. Every top-level page must be linked from it.
- `docs/resources/`: engineering references and design specs. Link targets, not user pages.
- `scripts/check-docs-links.py` and `.github/workflows/docs.yml`: the broken-link gate (owned by [scripts/AGENTS.md](../scripts/AGENTS.md)).

## Local Contracts

- Audience is the simulator user, not contributors. Build internals, architecture, and contribution rules live in `CONTRIBUTING.md` and `openwiki/`; link to them, never restate them.
- Every page is strictly vanilla Markdown: no YAML frontmatter, no generator-specific syntax (Material admonitions, `attr_list`, `superfences` tabs). Tables, fenced code blocks, and ATX headings are fine. This keeps a generator wrappable later without touching content.
- Pages link with relative `.md` paths only, within `docs/` or to repo files by `../` path. No required frontmatter, no nav files.
- Pages state behavior verified against the source, tests, or `CHANGELOG.md`. Check labels, menu paths, and behavior against the running app before merging, and cross-check the latest `CHANGELOG.md` section.
- A new page needs a link from `docs/README.md` in the same change; the gate fails otherwise.

## Work Guidance

- Update the matching page in the same change as a user-visible behavior change (menus, panels, parameters, behavior, file formats, release-archive contents).
- Illustrate a user-facing flow with a fenced `mermaid` code block where a diagram beats prose. Keep it simple and readable in GitHub's light and dark themes, and lead into it with a short sentence so the page still works where the diagram does not render.
- Keep absorbed page voice: short imperative sentences, exact control labels in **bold**, real menu paths.

## Verification

- `python scripts/check-docs-links.py docs` must exit 0. It resolves relative link targets, checks `#anchor` fragments against GitHub heading slugs, and requires every top-level page to be linked from `docs/README.md`.

## Child DOX Index

None. `docs/resources/` holds engineering references with no contract of their own.
