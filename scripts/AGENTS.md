# Scripts

## Purpose
- Own release, changelog, and format scripts and their operational contracts.

## Ownership
- `scripts/release.sh`
- `scripts/release-notes.sh`
- `scripts/format.sh`
- `scripts/format-dirs.sh`
- `scripts/install-clang-format.sh`

## Local Contracts
- `scripts/format-dirs.sh` is the single definition of the clang-format file set, used by `scripts/format.sh`, `.githooks/pre-commit`, and the release workflow's format job.
- `scripts/format.sh --all` checks the tracked C++ files under `scripts/format-dirs.sh` (`git ls-files`, not `find`), the same view a clean checkout gets in the release format job. Untracked files are not checked. An empty `--all` scan exits 1, and a listed file missing from the working tree fails check mode.
- `scripts/release-notes.sh` is the single changelog-section extractor used by `release.yml` and `scripts/release.sh`; it rejects a missing section or one without a bullet.
- `scripts/release.sh` prepares the branch, version bump, gates, and commit. `--tag` creates and pushes the annotated tag on `master` only.
- Releases default to a `release/vX.Y.Z` branch merged through a pull request. `scripts/release.sh --on-master` is the sanctioned direct-to-master alternative. The annotated `vX.Y.Z` tag is created only on `master`, after the version bump has landed.
- `CHANGELOG.md` must contain a version section with at least one bullet, or `scripts/release.sh` refuses to prepare.
- `release.yml`'s `validate-version` job requires the tag version to match `CMakeLists.txt` and a matching usable changelog section.
- `scripts/release.sh` replaces only the version token on the `project (RfSimulator VERSION …)` line of `CMakeLists.txt` and keeps its line ending. It fails unless exactly one line changed and the CR count is unchanged. After prepare, `git diff HEAD~1 --stat -- CMakeLists.txt` shows one insertion and one deletion; anything else means stop and fix the edit before pushing.
- `CHANGELOG.md` is the source of truth for release descriptions.
- The Linux tarball `rf-simulator-linux-x86_64.tar.gz` has exactly two root-level regular-file members: `rf-simulator-linux` (copied from `tiny-rf-simulator`) and `rf-sim-mcp`. Do not place them inside an `rf-simulator-linux/` directory.
- On Windows, run scripts with Git for Windows `sh`; WSL's `bash` on `PATH` cannot run them. Use `bash` on Linux and macOS.
- A failed tag is retagged as described in `CONTRIBUTING.md` ("Failed release tag"); a published release's tag is never moved.
- `release.yml` declares top-level `permissions: contents: read`; only the `release` job has `contents: write`.
- `strict-build` and `package` both run `.github/actions/build-and-test`, which holds each configuration's toolchain install, configure, build, test, and the Windows MinGW TEST_CASE floor check. `MINGW_TEST_CASE_FLOOR` is set in `release.yml`.

## Work Guidance

## Verification
- On a tag or `workflow_dispatch`, `release.yml` runs formatting, sanitizer, strict-build, package, and version/changelog validation. Linux tests run under Xvfb with `ctest -E "Benchmark"`, including `test_ui`; Windows legs (strict-build and package) and ASan exclude `Benchmark|test_ui`.
- On a tag, a zero patch component (`vX.Y.0`, `vX.0.0`) makes `classify-release` select three strict-build legs: Linux GCC 14 Debug, Linux Clang 18 Debug, and Windows MinGW-w64 Debug. Other patch tags select only Linux GCC 14 Debug. Release configurations build only in `package`.
- The package job builds with `CMAKE_BUILD_TYPE=Release` and runs CTest against that configuration. Debug builds are validation-only and are never shipped.
- The Windows package job fails when `libgcc_s_seh-1.dll`, `libstdc++-6.dll`, or `libwinpthread-1.dll` is missing from `/mingw64/bin`.
- `release.yml` also runs on `workflow_dispatch`: `version` (X.Y.Z, must match `CMakeLists.txt` and `CHANGELOG.md`) and `matrix` (`patch`: Linux GCC 14 Debug only; `minor`: full matrix). For dispatch, `matrix` alone picks the legs, regardless of the version's patch number. A dispatch run validates and packages the same artifacts and skips only the draft release step, so workflow changes can be tested on a branch without publishing.
- There is no pull-request CI trigger. `docs.yml` runs `mkdocs build --strict` for pushes to `master` touching manual/build inputs, published releases, or manual dispatch.
- Formatting check: `bash scripts/format.sh --check --all`.

## Child DOX Index
- None.
