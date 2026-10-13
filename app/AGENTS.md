# app/AGENTS.md

## Purpose
Application orchestration and UI for the simulator, including the app-bound project serializer, editor services, component library, instruments, extensions, and agent panel.

## Ownership
- `RfSimulatorApp` is the application orchestrator and owns UI, app state, and widgets; `CircuitRuntime` is UI-free and owns the live graph, components, IDs, topology operations, rewiring, and DSP updates.
- `GraphEditorActions` adapts probe, group, selection, part-number, and group-boundary mutations. `EditorCommands` is the UI-free user-edit entry point; `ProjectSerializer` is app-bound and restores project state through the runtime and editor actions.
- `ComponentRegistry` owns runtime components; `ComponentTypeRegistry` defines component creation, serialization metadata, and inspector dispatch. `simulator::editor_services` is the UI-free boundary for editor services, component library, receiver measurement, and shared output-SNR policy.
- `InspectorPanel` edits component parameters through `EditorCommands`; component-bound views are owned and synchronized by the app.
- `ExtensionManager`, `ExtensionTrustStore`, and `ExternalToolRunner` own extension discovery, trust records, and external-tool execution respectively.
- `TestFlowWidget` is app-owned; flow validation and execution use the UI-free `test_flow` boundary. Network Analyzer and receiver measurement use app-provided scratch graphs; `PowerMeterEngine` measures output power independently of UI.
- `PFBViewManager` owns the per-PFB IQ Plot and Channelizer Grid widgets and their visibility. `clear()` is reserved for bulk engine destruction and runs first in `ProjectSerializer::reset()`, before the engines are destroyed.
- `NetworkAnalyzerEngine` and `NetworkAnalyzerWidget` are a singleton instrument owned by `RfSimulatorApp`. Each measurement runs in a private scratch graph (`NaScratch`, `IsolatedChainRunner`); the reusable `MeasurementChainPath` stays in `network_analyzer_engine`.
- `ReceiverPerformanceMeasurementEngine` is UI-free and caches output-power and IIP3 measurements over the shared `MeasurementChainPath`. The config and request overloads measure identically.
- `PfbCalculatorWidget` binds M, K, beta, and the rejection target to the selected PFB through the engine setters. All response math comes from `PfbFilterDesign` in `pfb_channelizer/`, so the tool and the engine cannot drift.
- `AppAgentHost` (the app-side `IAgentHost`) owns checkpoints, the activity ring, node placement, and modal detection. `AgentPanelWidget` renders the Agent view and holds only callbacks and the transient selected checkpoint id.
- `ComponentFormModel` and `ComponentFormWidget` are the pure-logic and ImGui pair for the New/Edit Component form.

## Local Contracts
- `RfSimulatorApp::saveProject()`, `loadProject()`, and `newProject()` delegate to `ProjectSerializer::save()`, `load()`, and `reset()`. A failed load that resets project state clears the save target.
- `ProjectSerializer::save()` is atomic: write a sibling temporary file and rename it over the target; never remove the target before rename.
- Dirty state is derived from the `EditorCommands` revision. Edits outside `EditorCommands` call `markDirty()`; accepted commands advance the revision, rejected commands do not.
- User-level edits use `EditorCommands`; component-bound views synchronize on component changes. `EditorCommands::setComponentParams()` validates ordered JSON against `ParameterField::state_fields`; top-level and array-element object members are validated in request insertion order, and the first invalid member is returned. Path parameters and read-only, unknown, and type-invalid values are rejected before mutation; enum labels or valid integral indices map to stored indices; arrays replace whole. Accepted edits deserialize within a snapshot/rollback boundary; engine normalization of requested values is rolled back and reported.
- Persisted component parameters and Test Flow conditions use engine `serialize()` keys and encodings. `ParameterField::key` names authoring parameters; `state_fields` names serialized state. Do not rename or re-encode persisted keys without an explicit versioned migration.
- ADC DDC decimation is 1/2/4/8; NCO tuning is stored as a normalized factor of the ADC input sample rate. Legacy ADC state defaults to decimation 2 and NCO +0.25×Fs.
- S-parameter engines fall back to legacy `sparam_path` only when `sparam_filepath` is absent; `sparam_filepath` wins when both exist. A missing `sparam_fwd_idx` selects S21 for a loaded file, as `setSParamFilepath()` does (index 0 when no file is loaded); an explicit index is kept. See `tests/test_sparam_legacy_key.cpp`.
- The Spectrum Analyzer noise jitter is cosmetic on the noise floor only; deterministic signal tones are never perturbed.
- `sanitizePathSegment()` and `dataFileCopyDestination()` keep component authoring filenames contained; reject destinations that are not bare filenames. `ComponentLibrary` validates definitions against `ComponentTypeRegistry`; ideal filters require `fc_low_Hz` for LPF/HPF and both ordered cutoffs for BPF/BSF. `.rflib` import validates the whole package before writes; component identity conflicts already present in the global library root are skipped and reported individually, and non-conflicting entries install as a staged set under the global user library root.
- Extension discovery scans built-in source-tree, executable-relative built-in, global, then project-local roots. The first manifest for an ID wins; later duplicates are `Shadowed`, including when the winner is invalid or incompatible, and are excluded from data packs and external tools.
- Project-local untrusted external tools are absent from the Tools menu and cannot run before approval. External tools run only after the trust gate and under the canonical temporary root `temp/rf-sim-extension-run`.
- `RfSimulatorApp::update_dsp()` pumps `AgentServer` on the UI thread before `CircuitRuntime::update(0.0)`; the listener thread never touches app models or ImGui.
- The agent server is opt-in and off at construction; `main()` calls `startAgentServerIfEnabled()` after app construction. `SessionState` persists `[Agent] ServerEnabled` (default false), independently of the `WindowState/Agent` visibility. Enabling stores the opt-in and starts the listener at the endpoint file after `ensurePrivateDirectory()`; disabling stores false and stops the listener. A startup failure keeps the opt-in for retry. `AgentServer` removes its endpoint only while the file still carries its own GUI token.
- `findMeasurementChainPath()` is shared by Network Analyzer and receiver output-power/IIP3 measurements and follows exact linked ports to Point B's selected output, including switched branches rejoining at an RF SPDT 2:1 switch. Combiners, merges fed from outside the measured circuit, and a Point A component that feeds through more than one output port report no data.
- `TestFlowWidget` and the agent `test_flow_run` tool use `RunFlowWithinBoundary()`. A sweep above `kMaxRunRows` is refused before snapshotting.
- `ComponentTypeRegistry` is the component registration path; each registered type must have a matching `NodeKind`/symbol and `InspectorPanel` drawer. Keep registry, creation, serialization, and inspector dispatch consistent.
- Numeric inspector drawers edit a local value and write it back only when changed; preserve untouched fields passed together to a setter. *(The field contract is in the root AGENTS.md.)*
- Receiver requirements are optional project data in the top-level `receiver_requirements` object. Missing data in legacy files means Not configured; malformed objects, fields, or limits are retained as Invalid configuration with a diagnostic and do not abort other project restoration.
- App-level tests must `setStorePath()` to a unique temp file before approving or running a project-local tool; otherwise they write the developer's real `<exe_dir>/extension_trust.json`.
- `ComponentLibrary::loadFile()` / `scan()` continue discovery across malformed files: definitions with valid required fields are retained with malformed optional metadata defaulted. Definitions with missing or wrong-typed required fields, or invalid type-registry parameters, are logged and dropped.
- Component authoring refuses symlink S-parameter destinations and existing files unless the edited definition already references that local asset and no other loaded definition does; new entries never overwrite existing assets. Picked assets and component JSON are staged before commit; if definition replacement fails, the asset update is rolled back, and a rollback failure is shown in the form error.
- App-level integration tests may use `testExtensionManager()` and `testExtensionResultMessage()` with an ImGui, ImPlot, and ImNodes fixture.
- `computeOutputSnr()` in `simulator::editor_services` owns the shared output measurement rule: PFB output 0 uses active-channel SNR, ENBW, and integrated channel noise; every other available output uses the analyzer's current RBW. `RfSimulatorApp` adapts output 0's optional SNR into node-hover data, and the widget only renders that value.
- Window visibility persists through `SessionState` and `WindowState` keys (for example `WindowState/Agent` and `WindowState/ReceiverRequirements`); Test Flow selection, load state, and the last result are session-only and never persisted.
- The PFB properties inspector reports the active channel's integrated noise power in dBm and its effective noise bandwidth in MHz. This channel-level readout is distinct from the two Spectrum outputs (W/Hz) and their analyzer-RBW-integrated display.
- Project replacement notifications are part of the agent epoch contract: New clears checkpoints and notes `NewProject`; tutorial start notes `Tutorial` after its `newProject()` reset; `loadProject()` clears and notes `OpenedProject` only after success or `lastLoadReset()`, leaving checkpoints untouched after an intact failure.
- `ProjectSerializer::toJson()` / `fromJson()` own in-memory serialization and restoration. `save()` calls `toJson({project_dir, true})`, adds the project name, then retains atomic file output; `load()` retains file reading, the size cap, and parse recovery before calling `fromJson(root, {project_dir, true}, path)`. Without `sparam_root`, S-parameter paths remain verbatim and are not resolved or contained; `window_state=false` omits or ignores window flags for checkpoint snapshots.
- `exitApplication()` stops the agent server before `std::exit(0)`, which skips destructors. Ordinary shutdown stops it in the `RfSimulatorApp` destructor.
- `AppAgentHost` keeps the newest 20 checkpoints and the newest 50 activity entries. Every `recordActivity()` call logs exactly one `Agent:` INFO line, including when the new entry evicts the oldest.
- `AppAgentHost::placeComponents()` honors explicit positions. Automatic placement never moves existing nodes.
- `revertAgentCheckpoint(id)` accepts only a live checkpoint; a missing or stale id is logged, not silently ignored.
- Every IIP3 fundamental and IM3 frequency in `ReceiverPerformanceMeasurementEngine` must stay within the inclusive `MIN_FREQ..MAX_FREQ` bounds.
- `ReceiverRequirementsWidget` stages a draft. Apply validates and commits the entire draft and keeps the view open; Cancel restores the applied snapshot.
- `RfSimulatorApp` declares `ReceiverRequirementsState` before `m_serializer`, so the state outlives the serializer. It declares `AppAgentHost`, `AgentApi`, and `AgentServer` after their dependencies, in that order, so reverse destruction stops the server before its API and host.

## Work Guidance
- Add component behavior through `ComponentTypeRegistry`, the `node_graph` `NodeKind`/symbol, and the matching `InspectorPanel` drawer.
- Put new user-level project edits in `EditorCommands`; synchronize component-bound views through the component-change path, not component-type branches at individual create/remove/load sites.

## Verification
- `tests/test_component_dispatch.cpp` — component registry and inspector drawer consistency.
- `tests/test_component_authoring.cpp` — component authoring and ideal-filter cutoff validation.
- `tests/test_issue182_bounded_inputs.cpp` — bounded numeric input display and edit behavior.
- `tests/test_test_flow_widget.cpp` — Test Flow panel snapshot/restore, validation, and row-limit behavior.
- `tests/test_issue87_flow.cpp` — UI-free flow helper and flow-file behavior.
- `tests/test_sparam_legacy_key.cpp` — legacy S-parameter key fallback and precedence.

## Child DOX Index
*(none)*
