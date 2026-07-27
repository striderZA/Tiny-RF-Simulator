# layout/AGENTS.md

## Purpose
Owns ImGui window-layout persistence: an exe-relative default layout file
plus named, user-managed layout presets.

## Ownership
- `LayoutManager` — exe-directory detection, default-layout path/existence,
  and named-preset save/load/rename/delete/list

## Local Contracts
- All paths are relative to the running executable's directory (via
  `GetModuleFileNameA` / `_NSGetExecutablePath` / `/proc/self/exe`), never
  the current working directory — falls back to `std::filesystem::current_path()`
  if exe-path detection fails on a given platform.
- Default layout: `<exe_dir>/rf_simulator_layout.ini`. Fully owned by
  ImGui's own `IniFilename` autosave (see `core/src/core.cpp`); no
  `LayoutManager` method needs to be called for it to stay current.
- Named presets: `<exe_dir>/layouts/<sanitized-name>.ini`, written only on
  explicit user action (`View > Layouts > Save As...` in `app/`).
- `sanitizeName` is mandatory on any user-supplied preset name before it
  touches the filesystem.
- `saveDefaultLayout`/`loadDefaultLayout`/`saveNamedLayout`/`loadNamedLayout`
  call `ImGui::Save/LoadIniSettingsFromDisk` and require a live
  `ImGuiContext` — every other method is pure `std::filesystem` logic.

## Work Guidance
- Keep the ImGui-context-dependent methods thin; put any new path/filesystem
  logic in ImGui-context-free methods so it stays unit-testable under
  `tests/` (which must not depend on ImGui/GLFW).

## Verification
- `ctest --test-dir build -R LayoutManager --output-on-failure` (pure-filesystem
  unit tests in `tests/test_layout_manager.cpp`)
- `build/bin/test_ui` (runs the full ImGui Test Engine suite, including
  `layout_save_as_creates_file`/`layout_manage_delete_removes_file`, which
  exercise the ImGui-context-dependent save/delete through the `app/` UI)

## Child DOX Index
*(none)*
