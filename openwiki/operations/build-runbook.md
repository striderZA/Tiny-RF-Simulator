---
type: Runbook
title: Build & Operations
description: Reliable contributor and release-operator procedures for configuring, building, testing, formatting, installing, packaging, and troubleshooting Tiny RF Simulator. Covers pinned dependency fetching, platform constraints, executable-relative runtime data, CI gates, and release publication.
tags: [build, testing, packaging, ci, operations, release]
sources:
  - id: openwiki-source-6983d4a49fc6ae63a12ff946
    resource: repo://.githooks/commit-msg
  - id: openwiki-source-bf5be0c9253ed1d07b502e10
    resource: repo://.githooks/pre-commit
  - id: openwiki-source-baf30c604828cfde90a8ab63
    resource: repo://.githooks/pre-push
  - id: openwiki-source-4d1d392666be6dfdd7a91a2e
    resource: repo://.github/workflows/release.yml
  - id: openwiki-source-8037e2358a2c4f9b2c722a11
    resource: repo://AGENTS.md
  - id: openwiki-source-5f1fbd4979e8254a53e79f25
    resource: repo://app/src/app.cpp
  - id: openwiki-source-f8c30b6d300fb033e11282e7
    resource: repo://app/src/extension_manager.cpp
  - id: openwiki-source-baedf8f3f47fa931244e3545
    resource: repo://app/src/project_serializer.cpp
  - id: openwiki-source-d44494ef3e497fea81240ef8
    resource: repo://CMakeLists.txt
  - id: openwiki-source-3ce882f1e6c92c2ec4ddc6c9
    resource: repo://common/session_state.h
  - id: openwiki-source-f317ee207e1653d2033c81a4
    resource: repo://CONTRIBUTING.md
  - id: openwiki-source-6236844d67c4b6a4f4573508
    resource: repo://layout/src/layout_manager.cpp
  - id: openwiki-source-c5119c072dddff32b56e1e2e
    resource: repo://scripts/format.sh
  - id: openwiki-source-76478c25db28b99104e23105
    resource: repo://scripts/release.sh
  - id: openwiki-source-84d4847b3b591b7a766682c8
    resource: repo://scripts/test-githooks.sh
  - id: openwiki-source-5063b6aa8934c32dd8a94ee1
    resource: repo://tests/AGENTS.md
  - id: openwiki-source-fa68239bf614d837d7e5522c
    resource: repo://tests/CMakeLists.txt
  - id: openwiki-source-08f846c8582718824d718b09
    resource: repo://tutorial/src/tutorial_state.cpp
generated: { by: "omp", at: "2026-10-06T04:13:15.031Z" }
verified:
  - by: openwiki/0.7.0
    at: 2026-10-06T04:13:15.031Z
---

# Build & Operations

The project is a C++20 CMake build. The authoritative application version is the `project (RfSimulator VERSION ...)` value in `CMakeLists.txt` (currently `0.27.0`), not an older changelog entry. Build artifacts go to `build/bin`, libraries to `build/lib`, and FetchContent sources to `build/_deps`.

## Build lifecycle

```mermaid
flowchart TD
    A["Choose compiler and build directory"] --> B["Configure with CMake"]
    B --> C["Fetch pinned dependencies"]
    C --> D["Build targets"]
    D --> E["Run CTest and focused checks"]
    E --> F{"Release tag?"}
    F -- "No" --> G["Format and review changes"]
    F -- "Yes" --> H["Validate version and changelog"]
    H --> I["Build validation matrix and ASan"]
    I --> J["Package Linux and Windows artifacts"]
    J --> K["Publish draft GitHub release"]
```

This shows the repository's local build path and the tag-triggered release path; the release workflow requires every validation job to succeed before publication.

## Prerequisites and platform rules

- CMake 3.20 or newer, Ninja, Git, and a C++20 compiler are required. OpenGL development libraries and GLFW's platform dependencies are required on Linux/macOS.
- Windows supports **MinGW-w64 g++**, not MSVC `cl.exe`. The supported CI environment is MSYS2 `MINGW64`, with Git, GCC, CMake, Ninja, pkg-config, and Python installed.
- Linux release CI uses GCC 14 and also validates Clang 18 on minor/major tags. CI disables Wayland with `-DGLFW_BUILD_WAYLAND=OFF`; use that option on Linux when X11 compatibility is needed.
- Enable hooks once per clone if you want commit-time checks:

```bash
git config core.hooksPath .githooks
```

`clang-format-18` is required by the formatting script and hook. If it is unavailable, run `scripts/install-clang-format.sh`; it installs a cached copy that `scripts/format.sh` can find.

## Configure and build

Required first configuration (the compiler flags are especially important on Windows):

```bash
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc
cmake --build build
```

For an optimized build:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc
cmake --build build
```

The first configure may take about 60–90 seconds while dependencies are cloned. Subsequent builds reuse `build/_deps` and the CMake cache. `CMAKE_EXPORT_COMPILE_COMMANDS` is enabled by the root project, so `build/compile_commands.json` is available to clangd.

### When to reconfigure or clean

- Source edits normally need only `cmake --build build`.
- Re-run `cmake -B build` after adding or removing sources, targets, subdirectories, or CMake options.
- Delete `build/` and configure again when switching compilers or generators, after a broken/incompatible dependency cache, or when the cache contains stale platform options. On Unix:

```bash
rm -rf build
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc
cmake --build build
```

Do not clean merely because a normal source change was made; cleaning discards fetched sources and the configure cache.

## FetchContent and reproducibility

The root `CMakeLists.txt` fetches dependencies into the active build tree. The important immutable references are:

| Dependency | Repository | Reference |
|---|---|---|
| imgui | `ocornut/imgui` | `7e1b65d26d52e9dd199d889c148c72184de647b4` |
| implot | `epezent/implot` | `7eeb9168d2e5e6b14e266d8782ecf7e649dfc3a4` |
| GLFW | `glfw/glfw` | `92dcf4ce74f2e2554a98fea09be7c705c17daa5a` |
| Catch2 | `catchorg/Catch2` | `v3.4.0` |
| imgui_test_engine | `ocornut/imgui_test_engine` | `fdc1cb0930fa9bc2bef1d884bf2c10b570d29170` |
| imnodes | `Nelarius/imnodes` | `eb36902c892548ef94f88f51ad7e7c9c7058a71c` |
| portable-file-dialogs | `samhocevar/portable-file-dialogs` | `c12ea8c9a727f5320a2b4570aee863bbede2a204` |
| kissFFT | `mborgerding/kissfft` | `131.1.0` |
| nlohmann/json | `nlohmann/json` | `v3.11.3` |

`imgui`, `implot`, GLFW, `imgui_test_engine`, and `imnodes` use immutable commit pins; Catch2, kissFFT, and nlohmann/json use the listed tags. portable-file-dialogs and kissFFT are populated as source-only dependencies because their upstream build setup is not used here. Change pins only deliberately and reconfigure a clean build when validating such a change.

## Running the application

The executable is emitted as follows:

```text
build/bin/tiny-rf-simulator
build/bin/tiny-rf-simulator.exe   # Windows
```

Runtime assets use executable-relative paths for installed operation:

- `component_data/` and `extensions/` are installed beside the executable. Build-tree runs use source-tree fallbacks; component-data lookup prefers the installed executable-relative root when present.
- `layout/` stores the default `rf_simulator_layout.ini` and named layouts under `<exe_dir>/layouts/`.
- tutorial completion uses `<exe_dir>/.tutorial_completed`.
- `SessionState` persists window preferences to `<exe_dir>/app.ini` on Windows only; it is a no-op on non-Windows platforms. Project files separately store a subset of window flags.
- extension discovery also considers user and project-local roots. Source-tree built-ins take precedence over executable-relative, global, and project-local duplicates; project-local external tools remain trust-gated.

Moving an installed executable without its adjacent `bin/component_data` and `bin/extensions` directories breaks shipped-data lookup. On Windows, app-level tests share `app.ini`; layout and tutorial tests also share executable-relative files.

## Tests and isolation

Run the complete CTest registration with failure output:

```bash
ctest --test-dir build --output-on-failure
```

Useful focused commands (optional):

```bash
build/bin/tests [bench]
build/bin/tests [sparam]
build/bin/tests [filter]
build/bin/tests [edge]
ctest --test-dir build -R 'test_test_flow_widget|test_issue87_flow' --output-on-failure
```

The main `tests` executable contains the core Catch2 sources; many newer or platform-sensitive cases are standalone executables registered by `tests/CMakeLists.txt`. MinGW-w64 can silently drop `TEST_CASE`s beyond a compiler-specific registration ceiling. CI separately requires `build/bin/tests.exe --list-tests` to report at least the workflow-defined `MINGW_TEST_CASE_FLOOR` (currently 223). Put new coverage in a standalone target when the main binary approaches its ceiling, rather than assuming a silently dropped `TEST_CASE` ran.

UI tests are optional locally and require a display:

```bash
build/bin/test_ui
xvfb-run --auto-servernum ctest --test-dir build --output-on-failure -E Benchmark
```

CTest can run discovered cases in parallel, but do not run two CTest invocations against the same build tree concurrently. Some app-level tests share Windows `app.ini`; layout and tutorial tests share `<exe_dir>/layouts` and `.tutorial_completed`; extension tests mutate the source `extensions/` root. Relevant targets use `RUN_SERIAL` within one CTest invocation, but that does not serialize separate CTest invocations. Start with a clean `extensions/` directory. Scratch fixtures must be process-unique when adding tests because `ctest -jN` launches separate processes. `test_ui` is excluded from CI's headless Windows path; Linux CI uses Xvfb. Benchmarks are excluded from release CI with `-E "Benchmark"`.

## Format and local gates

The checked file set is shared by CI and the pre-commit hook through `scripts/format-dirs.sh`:

```bash
bash scripts/format.sh --check       # required before submitting
bash scripts/format.sh --check --all # optional full CI-scanned set
bash scripts/format.sh               # reformat changed files
```

Run the local release-equivalent gates before a PR or release preparation:

```bash
bash scripts/format.sh --check
cmake --build build
ctest --test-dir build --output-on-failure
```

### Git hook gates

Enable the repository's local hooks once per clone:

```bash
git config core.hooksPath .githooks
```

The pre-commit hook checks staged C++ with clang-format 18 using the shared directory list in `scripts/format-dirs.sh`. The commit-msg hook enforces the documented `<type>[(scope)][!]: <summary>` subject format under 70 characters; GitHub-side squash merges bypass the local hook, so PR titles should follow the same format. The pre-push hook blocks non-fast-forward updates to existing remote refs. For an intentional one-off rewrite, use `RFSIM_ALLOW_FORCE_PUSH=1 git push <same arguments>`; `git push --no-verify` skips all hooks. Run `bash scripts/test-githooks.sh` after editing anything under `.githooks/`.


## Install and package

The install tree places the executable and runtime payloads together:

```bash
cmake --install build --prefix /path/to/install
```

It installs `tiny-rf-simulator`, `component_data/`, and `extensions/` under `bin/`; `README.md`, `LICENSE`, and `openwiki/` go under `share/doc/rf-simulator` (excluding `.git` and `.last-update.json`). CPack is configured as `TGZ` on non-Windows and `ZIP` on Windows, with names based on `rf-simulator-<version>-<system>-<processor>`:

```bash
cpack --config build/CPackConfig.cmake
```

A package must retain `component_data/` and `extensions/` beside the executable (under `<prefix>/bin/` in the install tree). The simple release-workflow archives are separate binary distributions: Linux packages the optimized executable as `rf-simulator-linux-x86_64.tar.gz`; Windows packages the executable plus MinGW runtime DLLs as `rf-simulator-windows-x86_64.zip`.

## CI and release operations

Pull requests run **no automated CI pipeline**. Run the local format, build, and test gates before requesting review. Automated CI starts only when a release tag is pushed:

1. `classify-release` accepts semantic `X.Y.Z` tags. Patch tags (`Z > 0`) get Linux GCC 14 Debug strict validation; `vX.Y.0` and `vX.0.0` get the four-leg Linux GCC Debug/Release, Linux Clang 18, and Windows MinGW-w64 matrix.
2. `validate-version` matches the tag to `CMakeLists.txt` and uses `scripts/release-notes.sh` to require a non-empty matching changelog section.
3. Every tag runs clang-format 18 over the full checked set, Linux AddressSanitizer, and the tag-class strict-build matrix. Linux UI tests run under Xvfb; headless Windows validation excludes `test_ui`. Windows checks the workflow-defined `MINGW_TEST_CASE_FLOOR` (currently 223).
4. Every tag builds and tests the optimized Release Linux and Windows package configurations. Those are the shipped builds; Debug is validation-only.
5. After all required jobs pass, the workflow creates a **draft** GitHub release using the matching changelog section.

Prepare releases only after adding a `## [X.Y.Z] - YYYY-MM-DD` section with at least one bullet to `CHANGELOG.md`. The usual path prepares a `release/vX.Y.Z` branch for review, then tags the merged version on `master`:

```bash
# POSIX shells
bash scripts/release.sh X.Y.Z
bash scripts/release.sh X.Y.Z --dry-run
bash scripts/release.sh X.Y.Z --on-master
bash scripts/release.sh X.Y.Z --tag

# Windows (Git for Windows MSYS)
sh scripts/release.sh X.Y.Z
sh scripts/release.sh X.Y.Z --dry-run
sh scripts/release.sh X.Y.Z --on-master
sh scripts/release.sh X.Y.Z --tag
```

Prepare mode requires `master` and a clean tree apart from the changelog edit; by default it creates a release branch, bumps `CMakeLists.txt`, runs format/build/test gates, and commits. `--on-master` is the sanctioned direct-to-master alternative; `--skip-gates` skips preparation gates and requires a manual run afterward. Tag mode requires a clean `master` at the matching version, creates and pushes an annotated tag, and must run only after the version bump has landed.

On Windows, run the release script as `sh scripts/release.sh …` from Git for Windows MSYS. Because its `sed -i` can normalize mixed line endings in `CMakeLists.txt`, verify `git diff HEAD~1 --stat -- CMakeLists.txt` shows only the expected two-line version change before pushing. If it does not, restore `CMakeLists.txt` from `HEAD~1`, reapply the version bump byte-preservingly, and amend the preparation commit.

## Troubleshooting

| Symptom | Action |
|---|---|
| Windows configure/build fails with MSVC | Use an MSYS2 MinGW64 shell and `g++`; remove `build/` before switching. |
| FetchContent is slow or headers are missing | Let the first configure finish; inspect `build/_deps`; reconfigure or clean only if the dependency cache is incomplete. |
| Link errors after adding files | Add the source to its module `CMakeLists.txt`, then reconfigure. |
| Linux GLFW/Wayland configuration trouble | Reconfigure with `-DGLFW_BUILD_WAYLAND=OFF` and install the X11/OpenGL development packages. |
| UI test hangs or fails headlessly | Use `xvfb-run` on Linux; do not expect `test_ui` to run on Windows CI. |
| Tests disappear on MinGW | Check `build/bin/tests.exe --list-tests`; move new cases to a standalone target if the count is below the workflow's `MINGW_TEST_CASE_FLOOR` (currently 223). |
| Parallel tests fail around shared app state or extensions | On Windows, app-level tests share `app.ini`; layouts and tutorial tests share executable-relative state and extension tests mutate the source `extensions/` root. Clean leftovers, run one CTest invocation, honor `RUN_SERIAL`, and never overlap separate CTest runs on one build tree. |
| DSP output becomes NaN | Trace zero or invalid frequency through logarithmic gain calculations; guard inputs before `log10`/square-root paths and add a zero-frequency regression test. |
| Installed app cannot find examples or extensions | Verify the executable directory contains `component_data/` and `extensions/` (`<prefix>/bin/component_data/` and `<prefix>/bin/extensions/` for an install tree), rather than relying on the source-tree fallback. |
