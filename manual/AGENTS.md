# manual — AGENTS.md

## Purpose

User-facing MkDocs manual for RF Simulator. Pages under `manual/pages/` form the published site. `mkdocs.yml` builds it, and `.github/workflows/docs.yml` deploys it to GitHub Pages.

## Ownership

- `manual/pages/`: published Markdown. Nothing outside this tree becomes a page.
- `mkdocs.yml` nav and `manual/required-pages.txt`: the published page list. Change them together.
- `requirements-docs.txt`: the exact MkDocs and Material pins.
- `manual/AGENTS.md`: this contract. It sits outside `docs_dir`, so it is never published.

## Local Contracts

- `docs_dir` is `manual/pages`. Pages link only within that tree. Link repo files, such as `CHANGELOG.md` and `CONTRIBUTING.md`, by their GitHub URLs.
- Publish guard: a release or dispatch run fails before upload when a path in `manual/required-pages.txt` is missing under `manual/pages/`. Master pushes build only and never publish.
- Every docs build sets `NO_MKDOCS_2_WARNING=true`. Pass means exit 0 with no WARNING or ERROR records and no raw stderr.
- Pins are exact. Change them only in a deliberate PR that reruns the strict build and checks every page.
- The Agent (MCP) page describes the `rf-sim-mcp` bridge and the Agent panel. Check it against the release archive and the running app when either changes.

## Work Guidance

- Write for people who run the app. Leave out build internals and architecture.
- Check each page's labels, menu paths, and behavior against the running app before merging. Cross-check each page against the latest CHANGELOG section. Record one verification line per page in the PR.

## Verification

- `NO_MKDOCS_2_WARNING=true mkdocs build --strict` must exit 0. PowerShell: `$env:NO_MKDOCS_2_WARNING="true"; mkdocs build --strict`.
- Under `site/`: no `AGENTS*` file, and no path containing `superpowers` or `.githooks`.
- `git check-ignore -v site/index.html` matches. `git check-ignore -v manual/pages/index.md` does not.

## Child DOX Index

None.
