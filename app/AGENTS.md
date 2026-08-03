# app/AGENTS.md

## Purpose
Application orchestrator layer containing `RfSimulatorApp`, `ComponentRegistry`, and `InspectorPanel`.

## Ownership
- `RfSimulatorApp` — application boot, frame loop, DSP update, UI orchestration, project save/load
- `ComponentRegistry` — polymorphic component lifecycle and type-indexed lookup
- `InspectorPanel` — property editing panel with dirty tracking
- `ComponentTypeRegistry` — data-driven type schema table (field lists + factories) used by `ComponentLibrary::instantiate()`/`validate()` and the component authoring form
- `ComponentFormModel` / `ComponentFormWidget` — pure-logic + ImGui rendering pair for the New/Edit Component form
- `ExtensionManager` — extension manifest discovery and status tracking across built-in/global/project-local roots
- `ExternalToolRunner` — structured request/result execution for approved external tools
## Local Contracts
- `saveProject()` / `loadProject()` / `newProject()` handle full project serialization to `.rfsim` JSON format
- Dirty tracking propagated via `markDirty()` / `onParamChange` / `onLinkChanged` callbacks
- File menu bar in `draw_ui()` handles keyboard shortcuts (`Ctrl+N`, `Ctrl+O`, `Ctrl+S`, `Ctrl+Shift+S`) and unsaved-changes modal; Help menu provides F1-toggled help window
- View menu's `Layouts` submenu (Save As.../Load/Manage...) drives `LayoutManager` (see `layout/AGENTS.md`) for named window-layout presets; the exe-relative default layout is auto-managed by ImGui itself via `IniFilename`, set in `core/src/core.cpp`
- Library browser's "New Component..."/"Edit" flow writes schema-v2 JSON via `ComponentFormModel::buildDefinition()`; new entries save to a user-chosen root (`rf-sim-libraries/` project-local or `~/.rf-sim/libraries/` global); built-in `component_data/library/` entries are read-only (no Edit button) and never a save target; edits always overwrite the original `source_path` (no rename-on-identity-change)
- Extension discovery stays inside `app/` and scans built-in, global, and project-local roots; invalid manifests remain visible via `ExtensionManager::all()`
- External tools run only through `ExternalToolRunner` on explicit user action; the runner writes a JSON request file, waits for the tool to finish, and treats a missing/invalid result file as failure
- `runExternalTool()` serializes the clicked action label as `action_label`, clears any stale prior `result.json` before launch, refreshes discovery after a successful run, preserves the tool-provided result message in `m_extension_result_message`, and logs success/failure details through the app log
- `externalToolActions()` is the single policy for launchable tool actions: declared `menus[]` entries pass through unchanged; tools with no menus get one synthetic `"tools"` action using `manifest.name`
- `refreshExtensions()` rescans built-in, global, and project-local roots for the current project path and rebuilds the component library before the browser/UI reads from it
- `draw_ui()` and `drawExtensionsPanel()` both dispatch through `externalToolActions()`; the Tools menu renders only actions whose `location == "tools"`, while the Extensions panel shows every declared action (or a single fallback Run button)
- App-level integration tests may use `testExtensionManager()` and `testExtensionResultMessage()` with an ImGui/ImPlot/ImNodes fixture
- `load_window_states()` runs on construction to restore persisted window visibility toggles
- Destructor saves window state via `SessionState`

## Work Guidance
- Add new component serialization in both `saveProject()` (dump to JSON) and `loadProject()` (read from JSON + create via `ComponentRegistry`)

## Verification
- Round-trip tests in `tests/test_project_file.cpp`

## Child DOX Index
*(none)*
