# Contributing to RF Simulator

Thanks for your interest in contributing! This guide covers everything you need to develop, test, and submit changes.

## Development Setup

### Prerequisites

| Dependency | Minimum | Notes |
|------------|---------|-------|
| **Compiler** | C++20 | GCC 14, Clang 18 (CI); MinGW-w64 (Windows) |
| **CMake** | ≥ 3.20 | [cmake.org/download](https://cmake.org/download) |
| **Ninja** | ≥ 1.10 | [github.com/ninja-build/ninja](https://github.com/ninja-build/ninja) |
| **OpenGL** | 2.1+ | System-provided on all platforms |
| **Git** | — | Required for CMake FetchContent |

> **Windows:** MSVC (cl.exe) is not supported. Use MinGW-w64 g++.
> If switching compilers on Windows, delete `build/` first to clear cached compiler detection.

### Clone & Build

```bash
git clone https://github.com/striderZA/Tiny-RF-Simulator.git
cd Tiny-RF-Simulator
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc
cmake --build build
```

The first build takes 60-90s while FetchContent clones dependencies. Subsequent builds are fast.

**Where dependency sources live:** all FetchContent dependencies (imgui, implot, catch2, imgui_test_engine, …) are fetched into `build/_deps/<name>-src/` inside the active build directory. A fresh git worktree has **no** `build/` yet — run `cmake -B build -G Ninja` once (~90 s) to fetch them there, or browse the canonical checkout's copy. Never search the filesystem for headers like `imgui_internal.h` — a `find /`-style scan hangs far past any agent timeout on Windows. Read `CMakeLists.txt`'s `FetchContent_Declare` block for pinned versions.

**One-time setup — enable the local git hooks** (`.githooks/pre-commit` blocks commits that would fail the release workflow's `format` job; `.githooks/commit-msg` enforces the commit message format below):

```bash
git config core.hooksPath .githooks
```

If `clang-format-18` isn't available via your system package manager (e.g. Windows, older macOS), install a pinned copy: `scripts/install-clang-format.sh` (requires Python/pip; installs to `~/.cache/clang-format-18`, auto-detected by the hook and `scripts/format.sh`).

> **See also:** [Build & Operations guide](openwiki/operations/build-runbook.md) for release builds, clean builds, CI/CD, and troubleshooting.

## Testing

```bash
ctest --test-dir build --output-on-failure
```

Test sources live in `tests/` (Catch2 unit + benchmark suites) and `test_engine/` (ImGui UI tests). The main `tests` binary compiles `TEST_SOURCES` from `tests/CMakeLists.txt` (plus `test_session_state.cpp` on Windows); coverage that must run on every platform — or would push past the MinGW-w64 registration ceiling — goes in standalone executables registered there via `add_standalone_test` (`tests/CMakeLists.txt` is the authoritative inventory). The release workflow runs these under Xvfb for UI tests and with AddressSanitizer for memory safety.

For per-engine dirty/clean benchmarks:

```bash
build/bin/tests [bench]       # Linux/macOS
build/bin/tests.exe [bench]   # Windows
```

> **See also:** [Testing Guide](openwiki/testing/guidance.md) for test structure, patterns, benchmarks, and UI tests.

## User documentation

The user manual is built with MkDocs from `manual/pages/` and published to GitHub Pages. Its contract is in [manual/AGENTS.md](manual/AGENTS.md).

- **When to update:** any user-visible change: menus, panels, parameters, behavior, file formats, or what the release archives contain. Update the matching page in the same PR. A new published page needs a `mkdocs.yml` nav entry and a line in `manual/required-pages.txt`.
- **Preview:** `pip install -r requirements-docs.txt`, then `NO_MKDOCS_2_WARNING=true mkdocs serve` (PowerShell: `$env:NO_MKDOCS_2_WARNING="true"; mkdocs serve`).
- **Gate:** `NO_MKDOCS_2_WARNING=true mkdocs build --strict` must exit 0 with no WARNING or ERROR records.
- **Publishing:** publishing a GitHub release triggers `.github/workflows/docs.yml` as a separate workflow. The release stays published if the docs run fails, and the site keeps its previous version. After you fix the problem, dispatch `docs.yml` with the fixed ref to publish again.
- README demo videos must be GitHub `user-attachments` URLs in a raw `<video src="…" controls></video>` tag: GitHub renders no player for a repository-hosted `.mp4` (the tag is stripped from the rendered page) or for a Markdown `![](attachment-url)` reference (it renders as a broken image). Video sources such as `assets/*.mkv` stay local and are gitignored; GitHub accepts `.mp4`/`.mov`/`.webm` attachments only, capped at 10 MB for videos on a free-plan repository.
- **Commit subjects:** `docs(manual): ...` for pages, `ci(docs): ...` for the workflow.

## CI Pipeline

Pull requests run **no automated pipeline**. Release validation runs when a release tag is pushed (`.github/workflows/release.yml`). Every release tag, patch included, runs:

- **Format check** — clang-format 18 enforces code style across all source modules
- **AddressSanitizer** — Linux Debug build and tests with ASan (UI tests excluded)
- **Packaging builds** — optimized Release Linux GCC 14 and Windows MinGW-w64 binaries, the artifacts attached to the GitHub release. The `package` job is the only place these Release configurations are built, and it tests the build it ships (UI tests excluded on Windows). Build, test, and the MinGW floor check are shared through `.github/actions/build-and-test`.

On top of that, the `strict-build` validation matrix is chosen by tag class:

- **Patch release tags** (`vX.Y.Z` where `Z > 0`) build and test **Linux GCC 14 Debug** only (Xvfb for UI tests).
- **Minor and major release tags** (`vX.Y.0`, including `vX.0.0`) add **Linux Clang 18 Debug** and **Windows MinGW-w64 Debug** builds to the Linux GCC 14 Debug leg. The Windows leg runs unit tests only; UI tests need a display and run on Linux.

Pushes to master that change the manual or its build files also run a strict docs build in `.github/workflows/docs.yml`. That run never publishes; publishing is covered under [Release Process](#release-process).

Because nothing is checked automatically before merge, run the local gates on every branch before opening a PR:

```bash
bash scripts/format.sh --check   # CI-equivalent formatting (also enforced by the pre-commit hook)
cmake --build build
ctest --test-dir build --output-on-failure
NO_MKDOCS_2_WARNING=true mkdocs build --strict   # user manual; needs requirements-docs.txt installed
```

### Release Process

Releases are prepared on a `release/vX.Y.Z` branch by default, and the tag is created only on `master`, after the version bump has landed. `--on-master` prepares directly on `master` instead. `scripts/release.sh` runs these steps.

Run it with `bash` on Linux and macOS. On Windows, run it with `sh` instead, from Git for Windows: WSL's `bash` on `PATH` cannot run the script.

```bash
bash scripts/release.sh X.Y.Z    # Linux and macOS
sh scripts/release.sh X.Y.Z      # Windows
```

1. Write the `## [X.Y.Z] - YYYY-MM-DD` section (with at least one bullet) at the top of `CHANGELOG.md`.
2. Prepare the release. The script runs these stages in order and changes nothing until the checks pass:
   - **Checks:** the version is newer than `CMakeLists.txt`; neither the tag nor `release/vX.Y.Z` exists; the working tree holds nothing but `CHANGELOG.md` (untracked files count, and are listed); the changelog section exists.
   - **Configure:** if `build/CMakeCache.txt` is missing, runs `cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc` (about 90 s the first time). `--skip-gates` skips this stage. A failure stops here.
   - **Bump:** only the version token on the `project` line of `CMakeLists.txt` changes. The script checks that exactly one line changed and that its line ending is unchanged.
   - **Gates** on the bumped tree: `scripts/format.sh --check --all`, `cmake --build build`, and `ctest --test-dir build`.
   - **Commit:** creates `release/vX.Y.Z` (skipped with `--on-master`) and commits `CHANGELOG.md` and `CMakeLists.txt` as `chore: prepare release vX.Y.Z`.

   `--dry-run` prints the plan without changing anything. `--skip-gates` skips configure and the gates, so the tag workflow becomes the first full check, after the tag is pushed.

   If prepare stops, its message names the state:
   - A refused check or a failed configure changes nothing. A failed gate restores `CMakeLists.txt`. Either way you stay on `master`; fix the cause and re-run.
   - A failed commit is rolled back: `CMakeLists.txt` is restored and the release branch is removed. `CHANGELOG.md` is left as you had it, staged or not.
   - A commit that already landed is kept, even if the script stops right after it: nothing is rolled back, and the message names the commit.
   - A bump left uncommitted by a killed process is reported with the command that discards it. Run that command, then re-run.
   - Re-running after a successful prepare reports where the release is committed (`release/vX.Y.Z` or `master`) and the next step.
3. If prepare created a release branch, push it and open a PR against `master` (the script prints the command), then merge after review. With `--on-master`, push `master` directly.
4. Once the version bump is on `master`, tag it: `sh scripts/release.sh X.Y.Z --tag` on Windows, `bash` elsewhere. It verifies the version matches `CMakeLists.txt`, cuts the annotated `vX.Y.Z` tag, and pushes it.
5. The release workflow validates the tag against `CMakeLists.txt` and the changelog section, runs the format check, the AddressSanitizer build, and the tag-class build/test matrix, and packages Linux and Windows binaries.
6. The workflow extracts the matching `CHANGELOG.md` section as the draft release description. `CHANGELOG.md` is the release-note source of truth.
7. Review and publish the generated GitHub draft after the workflow succeeds.

After prepare, check that `git diff HEAD~1 --stat -- CMakeLists.txt` shows one insertion and one deletion: the replaced version line.

Publishing the GitHub release also triggers [.github/workflows/docs.yml](.github/workflows/docs.yml), a separate workflow that deploys the user manual if it succeeds. The release does not depend on it; see [User documentation](#user-documentation).

### Failed release tag

The release workflow starts when `vX.Y.Z` is pushed. When a run fails, retag the same version, as long as that release has not been published:

1. Read the failing job's log, and cancel the run if it is still going.
2. Fix the cause on `master` through a PR. The version stays `X.Y.Z`.
3. Delete the draft release if the run created one: `gh release delete vX.Y.Z --yes`.
4. Delete the tag on the remote and locally: `git push origin :refs/tags/vX.Y.Z`, then `git tag -d vX.Y.Z`.
5. Once the fix is on `master`, tag again: `sh scripts/release.sh X.Y.Z --tag` on Windows, `bash` elsewhere.

Never move the tag of a published release. Ship the fix as the next patch release (`X.Y.(Z+1)`) instead.

## Architecture (Quick Reference)

- **C++20** modular library.
- **Engine + Widget pattern:** each module has an `*_engine` (pure DSP, no UI deps) and an optional `*_widget` (ImGui UI). The engine owns a `SignalNode {input, output Spectrum, view_enabled}`.
- **Only widget files** may `#include <imgui.h>` / `<implot.h>`.
- **CMake targets** use `simulator::*` aliases (e.g. `simulator::signal_generator_engine`).
- **Circuit runtime and wiring:** `CircuitRuntime` owns live graph/component topology, validates link commands, rewires `Spectrum` inputs, and updates components in topological order. `RfSimulatorApp` composes it with the UI, `GraphEditorActions` owns probe/group editor state, and `EditorCommands` is the single entry point for user-level edits — it applies each edit's side effects and owns the revision-derived dirty state.
- For full architecture details, see the [Architecture Overview](openwiki/architecture/overview.md).

## Code Style

- **Format:** LLVM-based via [`.clang-format`](.clang-format) (4-space indent, 100 cols, `PointerAlignment: Right`).
  Run `bash scripts/format.sh` to reformat changed files, or `bash scripts/format.sh --check` to dry-run (CI-equivalent). The checked directory set lives once in `scripts/format-dirs.sh` (shared with the pre-commit hook and the release workflow). CI will reject PRs with formatting violations.
  Enable `git config core.hooksPath .githooks` once per clone to catch this automatically on `git commit` — see Clone & Build above.
  The pre-commit hook runs the format check only when staged C++ files fall in the checked directories.
- **DSP engine helpers** for ImGui inputs: `utils::inputDouble(label, ref, min, max)` and `utils::inputFrequency(label, freq_Hz, ...)`. Their limits bound user edits only: a value already outside them is shown as is (flagged, logged once) and kept until the user edits the field, so pass a local copy of the engine value and write it back only when the helper returns `true`.
- **Spectrum tone struct:** `{double freq_Hz, power_dBm, phase_deg}`.
- **Test float comparisons** with `Catch::Approx` from `<catch2/catch_approx.hpp>`.
- **Portability:** `uint64_t` requires explicit `#include <cstdint>` (g++ does not provide it transitively).

> **See also:** [Build & Operations guide](openwiki/operations/build-runbook.md#code-style) for full code style conventions.

## Git Workflow

### Branching

- **Feature branches** for all new work. Never commit directly to `master`.
- Branch naming:
  - `feat/<short-description>` for new features
  - `fix/<short-description>` for bug fixes
  - `docs/<short-description>` for documentation
  - `release/vX.Y.Z` for release preparation (see Release Process)
- Branch from `master`, delete the feature branch after merging.

### Commits

- **Atomic commits:** one logical change per commit (feature, fix, refactor, or docs).
- **Commit early, commit often** — a commit should represent a working state.
- **Verify the build and tests pass** before committing.
- **Subject format:** `<type>[(<scope>)][!]: <imperative summary>`, under 70 characters. `type` is one of `build`, `chore`, `ci`, `docs`, `feat`, `fix`, `perf`, `refactor`, `revert`, `style`, `test`; `scope` is optional lowercase (e.g. `test(pfb): cover grid invariance`); `!` marks a breaking change. A body is optional — leave a blank line after the subject and wrap it at 72 characters.
- **Enforced locally** by [`.githooks/commit-msg`](.githooks/commit-msg) on every `git commit`. Git-generated `Merge …`, `Revert …`, `fixup! …`, and `squash! …` subjects are exempt, as is a merge in progress (`git merge`). Keep it sensible with `git commit --no-verify` only when the rule genuinely does not apply.
- **Test the hooks** with `bash scripts/test-githooks.sh` after editing anything under `.githooks/`.
- Keep hook files at mode `100755` (`git update-index --chmod=+x <path>`).
- Commits created by GitHub's squash merge never run local hooks, so **write the PR title in the same format**.

### Pushing

- **Force-pushing published history is blocked locally.** [`.githooks/pre-push`](.githooks/pre-push) rejects any push that would update an existing remote ref with a non-fast-forward commit, because that history is what open pull requests, reviews, and other clones point at. Creating a branch, deleting a branch, and fast-forward pushes pass unchanged.
- **When the rewrite is intended** — typically your own unmerged branch after a `rebase` or `commit --amend` — allow it for that one push: `RFSIM_ALLOW_FORCE_PUSH=1 git push <same arguments>`. `git push --no-verify` also skips the check, along with every other hook.
- Ancestry that cannot be decided locally (a shallow or partial clone) is allowed with a notice rather than guessed at.
- Only pushes made from this clone are affected: the GitHub UI, the API, and other machines do not run the hook.

### Pull Requests

- Use the [PR template](.github/PULL_REQUEST_TEMPLATE.md).
- No CI runs on pull requests — verify build, tests, and formatting locally before requesting review (see CI Pipeline).
- Release workflow changes: run **Release** on the branch with `workflow_dispatch` (inputs: `version` as X.Y.Z, and `matrix` as `patch` or `minor`). The run validates and packages the artifacts but never publishes.
- Squash-merge or rebase-merge to keep history clean.

## Where to Get Help

- **Bug reports:** [open a bug issue](../../issues/new?template=bug_report.md)
- **Feature requests:** [open a feature issue](../../issues/new?template=feature_request.md)
- **Build questions:** check [README.md](README.md) first, then open an issue.

## License

By contributing, you agree that your contributions will be licensed under the [Apache-2.0 License](LICENSE).
