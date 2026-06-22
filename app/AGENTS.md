# app/AGENTS.md

## Purpose
Application orchestrator layer containing `RfSimulatorApp`, `ComponentRegistry`, and `InspectorPanel`.

## Ownership
- `RfSimulatorApp` — application boot, frame loop, DSP update, UI orchestration, project save/load
- `ComponentRegistry` — polymorphic component lifecycle and type-indexed lookup
- `InspectorPanel` — property editing panel with dirty tracking

## Local Contracts
- `saveProject()` / `loadProject()` / `newProject()` handle full project serialization to `.rfsim` JSON format
- Dirty tracking propagated via `markDirty()` / `onParamChange` / `onLinkChanged` callbacks
- File menu bar in `draw_ui()` handles keyboard shortcuts (`Ctrl+N`, `Ctrl+O`, `Ctrl+S`, `Ctrl+Shift+S`) and unsaved-changes modal
- `load_window_states()` runs on construction to restore persisted window visibility toggles
- Destructor saves window state via `SessionState`

## Work Guidance
- Add new component serialization in both `saveProject()` (dump to JSON) and `loadProject()` (read from JSON + create via `ComponentRegistry`)

## Verification
- Round-trip tests in `tests/test_project_file.cpp`

## Child DOX Index
*(none)*
