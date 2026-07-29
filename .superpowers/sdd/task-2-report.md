# Task 2 Report: Discovery and extension status tracking

## What I implemented
- Added `ExtensionManager` in `app/include/extension_manager.h` and `app/src/extension_manager.cpp`.
- Discovery now scans the three planned roots:
  - built-in: `PROJECT_SOURCE_DIR/extensions`
  - global: `~/.rf-sim/extensions` via `USERPROFILE` on Windows or `HOME` elsewhere
  - project-local: `<project_root>/rf-sim-extensions`
- Each extension folder is discovered via its `plugin.json` manifest.
- Invalid manifests are preserved as visible `ExtensionRecord` entries with `status == ExtensionStatusKind::Invalid`, their `issues`, and a manifest path.
- Valid manifests are classified into `dataPacks()` and `externalTools()` based on manifest kind.
- Added compatibility-aware status tracking: manifests with `compat.min_app_version` above the current app version are marked `ExtensionStatusKind::Incompatible`.
- Updated `app/CMakeLists.txt` so the new manager source is compiled into `simulator::app`.
- Updated `app/AGENTS.md` with the ExtensionManager ownership/status note.
- Added focused discovery tests in `tests/test_extensions.cpp` for:
  - a valid project-local data pack
  - a malformed manifest that remains visible as an invalid record

## What I tested and results
- `cmake --build build --target test_extensions`
  - Passed.
- `ctest --test-dir build -R test_extensions --output-on-failure`
  - Passed: 1/1 test executable succeeded.

## Files changed
- `app/include/extension_manager.h`
- `app/src/extension_manager.cpp`
- `app/CMakeLists.txt`
- `app/AGENTS.md`
- `tests/test_extensions.cpp`
- `.superpowers/sdd/task-2-report.md`

## Self-review findings
- Discovery is deterministic within each root by sorting manifest paths before loading.
- Invalid manifests stay visible in `all()` instead of being dropped.
- Classification helpers return parsed manifests only; invalid entries are excluded because they have no manifest object.
- Compatibility status is derived from `compat.min_app_version` against `APP_VERSION`.

## Issues or concerns
- No blocking issues remaining.
