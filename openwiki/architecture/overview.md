---
type: Architecture Overview
title: Architecture Overview
description: Explains the RF Simulator's platform, UI-free circuit runtime, topology, DSP engines, editor commands, persistence, instrument integrations, and the agent boundary through which MCP clients drive the circuit.
tags: [architecture, runtime, dsp, persistence, extensions, instruments, agent]
verified:
  - by: openwiki/0.7.0
    at: 2026-10-10T18:57:17.727Z
sources:
  - id: openwiki-source-4dd766881eeb1848d297e025
    resource: repo://agent/AGENTS.md
  - id: openwiki-source-b406d12d0f86c2f3bc435e82
    resource: repo://agent/api/include/agent_host.h
  - id: openwiki-source-d37ecc0aeb17155d7b54881a
    resource: repo://agent/api/src/tool_measure.cpp
  - id: openwiki-source-594ad266217b9f37cd806391
    resource: repo://agent/api/src/tool_receiver.cpp
  - id: openwiki-source-11d3b4337d1de5e03c6cb2d1
    resource: repo://app/include/app_agent_host.h
  - id: openwiki-source-8c3f2a1fe9422d9010bcc799
    resource: repo://app/include/circuit_runtime.h
  - id: openwiki-source-eddd217b5fc424e198cfdd6b
    resource: repo://app/include/component_registry.h
  - id: openwiki-source-a6f41f170cb54f5b8f38bf47
    resource: repo://app/include/component_type_registry.h
  - id: openwiki-source-e81f2756b009d1486dba9b74
    resource: repo://app/include/editor_commands.h
  - id: openwiki-source-5f1fbd4979e8254a53e79f25
    resource: repo://app/src/app.cpp
  - id: openwiki-source-bc033392c5f8ce0dd75226a2
    resource: repo://app/src/circuit_runtime.cpp
  - id: openwiki-source-dd0234525c20fcc7f7d85a35
    resource: repo://app/src/component_library.cpp
  - id: openwiki-source-7ffec1c215e8323dc7328f1d
    resource: repo://app/src/editor_commands.cpp
  - id: openwiki-source-f8c30b6d300fb033e11282e7
    resource: repo://app/src/extension_manager.cpp
  - id: openwiki-source-ee53296641ba30982216ac57
    resource: repo://app/src/extension_manifest.cpp
  - id: openwiki-source-af265b413af09fb5dd0461cc
    resource: repo://app/src/external_tool_runner.cpp
  - id: openwiki-source-baedf8f3f47fa931244e3545
    resource: repo://app/src/project_serializer.cpp
  - id: openwiki-source-76cf00e5eb6f86db297b4153
    resource: repo://app/src/receiver_performance_measurement.cpp
  - id: openwiki-source-e7932f8366579c2ce8c1865d
    resource: repo://app/src/test_flow_widget.cpp
  - id: openwiki-source-ca6cb4b1a14fd7969dfae3ec
    resource: repo://CHANGELOG.md
  - id: openwiki-source-d7839e83f1db8019b777d76e
    resource: repo://common/graph_link_policy.h
  - id: openwiki-source-3ce882f1e6c92c2ec4ddc6c9
    resource: repo://common/session_state.h
  - id: openwiki-source-137cf4932d136d5c05b4e508
    resource: repo://common/signal_node.h
  - id: openwiki-source-87a8648c20764eeabb54d18e
    resource: repo://core/src/core.cpp
  - id: openwiki-source-c4e217301712039a3e937bb2
    resource: repo://layout/include/layout_manager.h
  - id: openwiki-source-318263a7857c897eac3da71e
    resource: repo://network_analyzer/include/network_analyzer_engine.h
  - id: openwiki-source-9a250414ad94d8c41379ca9b
    resource: repo://network_analyzer/src/network_analyzer_engine.cpp
  - id: openwiki-source-7483c8c3ea0d9c325c992db0
    resource: repo://node_graph/src/rewire.cpp
  - id: openwiki-source-d364d949938a433276255c32
    resource: repo://src/main.cpp
  - id: openwiki-source-1f397e27ef8b0dc1f4f49d51
    resource: repo://test_flow/include/flow_boundary.h
  - id: openwiki-source-3d3e3b78daf6b95b69159369
    resource: repo://test_flow/include/flow_runner.h
  - id: openwiki-source-7cf82ec60f61d2c0858950cf
    resource: repo://test_flow/src/flow_boundary.cpp
  - id: openwiki-source-1425d7c9bb71c8e6d4145122
    resource: repo://test_flow/src/flow_runner.cpp
  - id: openwiki-source-0f5985dbf469bdfd8b400c2d
    resource: repo://tests/test_editor_commands.cpp
  - id: openwiki-source-798feeba9e1cc5b47be50e7a
    resource: repo://tests/test_flow_boundary.cpp
  - id: openwiki-source-e2cedad6ad87cba9b8bef4fb
    resource: repo://tests/test_issue45_extension_trust.cpp
  - id: openwiki-source-42ef0db762a7ffb82713cae7
    resource: repo://tests/test_issue48_json_loader.cpp
  - id: openwiki-source-8568eb9d2755f18e4ab13407
    resource: repo://tests/test_path_containment.cpp
  - id: openwiki-source-59642b0a96e98716b082cc11
    resource: repo://tests/test_test_flow_widget.cpp
  - id: openwiki-source-176b6b1095bfaf01d84f5034
    resource: repo://tutorial/include/tutorial_state.h
generated: { by: "omp", at: "2026-10-10T18:57:17.727Z" }
---

# Architecture Overview

RF Simulator separates platform lifetime, circuit topology, DSP execution, editor policy, agent access, and user-facing tools. The executable and core own the window and frame loop; `CircuitRuntime` owns the live graph and components; common headers define signal contracts; `RfSimulatorApp` composes UI, project persistence, libraries, instruments, and extensions; and the `agent/` subsystem gives external MCP clients a bounded tool surface through `IAgentHost`.

## Runtime ownership

```mermaid
sequenceDiagram
    participant Main as main.cpp
    participant Core as RfSimulatorCore
    participant App as RfSimulatorApp
    participant Agent as AgentServer
    participant Runtime as CircuitRuntime
    participant Graph as NodeGraphEngine
    participant Engines as Component engines
    participant UI as ImGui widgets
    Main->>Core: Run app callback
    Core->>Core: initialize platform and UI contexts
    loop each frame
        Core->>App: update_dsp()
        App->>Agent: pump queued agent calls
        App->>Runtime: update(dt)
        Runtime->>Graph: rewire inputs and get topological order
        Runtime->>Engines: update in graph order
        App->>Graph: resolve probe output ports
        Core->>App: draw_ui()
        App->>UI: draw editor and instruments
        Core->>Core: render and swap buffers
    end
```

*The app pumps agent calls and delegates circuit evaluation to the UI-independent runtime before drawing views.*

`src/main.cpp` creates `RfSimulatorCore`, the ImNodes context, and `RfSimulatorApp`, starts the agent server if its persisted opt-in is set, and supplies a callback that runs `update_dsp()` before `draw_ui()`. The core initializes GLFW, the OpenGL 2 ImGui backend, ImGui, and ImPlot. It owns event polling, docking, viewport restoration, and rendering, and it shuts the platform resources down after the loop.

`CircuitRuntime` owns `NodeGraphEngine`, `ViewManager`, `ComponentRegistry`, and the component-ID counter. It creates and removes engines, validates connections, rewires inputs after topology changes, and updates live engines in graph order. Its epoch advances only when the circuit is cleared and reset, and agent results carry that epoch so clients can detect stale state. It includes no ImGui headers, so runtime lifecycle and link-policy behavior can be tested without constructing the application UI.

`RfSimulatorApp` is the composition root for `CircuitRuntime`, `GraphEditorActions`, `EditorCommands`, project serialization, component libraries, view widgets, instruments, and the agent stack (`AppAgentHost`, `AgentApi`, and `AgentServer`). `GraphEditorActions` adapts probe, group, and selection mutations to the graph. `NodeGraphWidget` reads a const graph and sends edit requests through its action callbacks; it does not own graph mutation policy.

`EditorCommands` is the single user-level edit boundary. An accepted component, topology, or group edit performs its required app side effects, such as refreshing group boundaries and component-bound views, and then advances the project revision. A rejected edit changes neither the revision nor the project state. Dirty state is derived by comparing that revision with the last clean revision, which a successful save, load, or New records. Parameter edits, node moves, and instrument-state edits that do not pass through a command explicitly mark the project modified. Probe changes and group selection retain their non-revision semantics.

## Engine/widget separation and signal flow

A component normally has a pure C++ engine and an optional ImGui widget:

```text
component/
  include/*_engine.h       DSP and serialization contract
  src/*_engine.cpp         no UI dependency
  include/*_widget.h       optional UI API
  src/*_widget.cpp         ImGui rendering and controls
```

Engines implement `IComponentEngine`, own a `SignalNode`, and read `node().inputs[k]` while writing owned `node().outputs[k]` spectra. `SignalNode::inputs` contains `const Spectrum*` pointers, so removing an engine must synchronously rewire surviving inputs before the runtime returns to code that may update or render consumers.

`NodeGraphEngine` stores nodes, links, pins, probes, groups, and graph IDs. It does not own RF link policy: `CircuitRuntime::connect()` resolves both pins, applies `graphLinkAllowed()` and the graph's duplicate-input/cycle checks, then commits and rewires. The shared `rewireComponentInputs()` pass resolves each component input to the connected source node and output port, applies the same physical policy, and stores either the matching output pointer or `nullptr`. Removal, disconnection, and every frame's runtime update use that pass.

`Spectrum` carries a frequency grid, discrete tones, input and added noise-density vectors, phase, sample rate, complex-baseband state, and a generation counter. Engine dirty caches use the input pointer and generation together with an explicit parameter-dirty flag. Multi-output nodes keep output-port identity through links and probes; a selected probe is resolved as a `(SignalNode*, output_index)` pair rather than assumed to be output 0.

```mermaid
flowchart TD
    A["NodeGraphEngine links"] --> B["CircuitRuntime and shared rewire pass"]
    B --> C["SignalNode input pointers"]
    C --> D["topological update"]
    D --> E["component Spectrum outputs"]
    E --> F["downstream engines or selected probes"]
    F --> G["spectrum and component views"]
```

*Topology chooses signal pointers; engines compute generation-tagged spectra; views consume selected outputs.*

## Component ownership and dispatch

`ComponentRegistry` owns live engine instances and registers their signal nodes with `ViewManager`. It indexes engines by graph-node ID and C++ type, exposes a stable component view for orchestration, and rolls back graph/view/index registration if construction fails. `CircuitRuntime` delegates component lifetime to this registry and rewires surviving engines after removal.

`ComponentTypeRegistry` is separate from the instance registry. Its descriptors provide canonical type keys, project-file names, labels, node kinds, factories, inspector drawers, parameter metadata, and S-parameter capability. Canvas insertion, duplication, persistence reconstruction, inspector dispatch, component-library validation/instantiation, and node-label mapping use this shared table. The type registry defines what a component type means; `ComponentRegistry` owns which instances currently exist.

`ComponentLibrary` stores reusable definitions rather than live engines. It scans built-in, global, and project-local roots, validates definition data, resolves referenced assets relative to each definition, and asks the runtime/type registry to instantiate a selected definition. A successful insertion is adopted by `EditorCommands` so it participates in normal view synchronization and dirty tracking.

## Instruments and measurement integrations

The Network Analyzer is a singleton floating instrument, not an `IComponentEngine` or graph node. It takes Point A and Point B output pins, finds every component on the forward path between them with `findMeasurementChainPath()`, and runs that chain on private clones (`IsolatedChainRunner`) driven by a synthetic tone-comb stimulus. The live simulation is never read for signal purposes or written. The engine recomputes only when the path signature or sweep settings change. Switched filter-bank acceptance and the rejected topologies are described in [Measurement Chains](../domains/measurement-chains.md).

The receiver output-power and IIP3 measurement uses the same discovery, and the agent `network_analyzer_sweep` and `receiver_measure` tools resolve their chains through it. The agent `measure_port` tool does not run chain discovery; it reads one live output endpoint after recomputing the runtime.

The Receiver Requirements panel is an app-level measurement consumer rather than a graph component. While it is visible, `RfSimulatorApp` updates the Network Analyzer once per frame, feeds the receiver-performance engine the Point A and Point B pins and the sweep, evaluates gain, noise-figure, output-power, and IIP3 limits, and draws the result in the panel. Requirement state is saved in the project file.

## Agent interface boundary

The `agent/` subsystem is a UI-free protocol and API layer. Its API reaches application behavior only through `IAgentHost`, which `AppAgentHost` implements, and the app pumps queued agent calls at the start of each frame. Agent circuit edits go through `EditorCommands`, and the `test_flow_run` tool runs through the same run boundary as the Test Flow panel. The layers, dependency rules, and security contracts are in [Agent Interface Architecture](agent-interface.md); the per-call path is in [Agent Tool-Call Lifecycle](../workflows/agent-tool-calls.md).

## Project persistence and lifecycle

`ProjectSerializer` owns `.rfsim` save, load, and reset behavior. The project stores component types and serialized parameters, editor positions, links and probes as component-index/port references, groups, a subset of window state, graph counters, Network Analyzer settings, and Receiver Requirements state. Raw pin IDs are not the persistent link address: loading rebuilds components in file order and resolves saved component indexes to their new graph nodes and pins.

```mermaid
flowchart TD
    A["Open project"] --> B["Read, size, and section-shape checks"]
    B -->|invalid section shape| C["Reset to empty state and report failure"]
    B -->|valid project| D["Reset existing circuit and views"]
    D --> E["Create components in saved order"]
    E --> F["Keep saved-index mapping and roll back malformed records"]
    F --> G["Restore links through CircuitRuntime"]
    G --> H["Restore probes, instruments, and groups"]
    H --> I["Rebuild derived group boundaries"]
```

The loader rejects files larger than 64 MiB and validates top-level structure before restoration. A wrong-shaped top-level section resets the circuit and reports failure, while invalid guarded fields such as project window flags or `graph_state.next_component_id` are rejected before reset so they cannot destroy an already-open project. Malformed component records are skipped without compacting the saved-index map; if deserialization fails after an engine was created, that engine is removed and the corresponding index remains invalid so later links and probes cannot drift onto a different component.

Saving serializes into a sibling `.tmp` file, checks write, flush, and close, and renames it over the target only after success. S-parameter paths in projects are confined to the project directory and saved relatively when contained; component-library data files are confined to their definition directory. Missing or rejected RF data does not escape these roots.

Editor revision state distinguishes project edits from UI interaction. A successful save, load, or New records the clean revision. `.rfsim` stores four project window flags, while `SessionState` separately stores broader window visibility preferences in `app.ini` on Windows (it is a no-op elsewhere). `LayoutManager` owns the exe-relative ImGui layout and named layouts, and `TutorialState` owns its exe-relative completion marker. These are distinct from circuit and project state.

## Paths and extension trust boundaries

Paths read from projects and library definitions are untrusted. Project S-parameter references must remain under the project directory; library `data_files` must remain under the library JSON's directory. Component-authoring copies also derive safe data-file names from sanitized metadata. These checks are enforced at the boundary that interprets each path.

Extensions are external integrations, not in-process component plugins. `ExtensionManager` discovers manifests in built-in, global, and project-local roots; invalid or incompatible manifests remain visible with validation issues. Manifest IDs and declared data paths are constrained to safe segments and the owning extension root. `ExternalToolRunner` executes a tool only after an explicit user action through a JSON request/result exchange; missing, oversized, or invalid results are failures rather than partial output.

```mermaid
flowchart TD
    Input["Project, library, or extension data"] --> Validate["Shape, type, trust, and containment checks"]
    Validate -->|rejected| Report["Log, skip, or neutralize at owning boundary"]
    Validate -->|accepted| Owner["Serializer, library, or extension manager"]
    Owner --> Tool["External process only after explicit user action"]
    Tool --> Result{"Valid result?"}
    Result -->|no| Fail["Operation fails"]
    Result -->|yes| Consume["Consume structured result"]
```

## Recent architectural progression

The release history explains why circuit ownership and tools have separate seams. Version 0.26.0 extracted `CircuitRuntime` and `GraphEditorActions` from the application-facing workflow; version 0.27.0 extended the Receiver Requirements integration with output-power and IIP3 measurement criteria. `EditorCommands` now centralizes user-level edit side effects and revision-based dirty tracking. The agent interface sits beside these layers rather than inside them, reusing `EditorCommands` and `RunFlowWithinBoundary()` instead of duplicating their behavior.

## Focused change and test surfaces

For DSP changes, start with the engine and its tests, then verify signal pointer routing, generation invalidation, multi-output pin selection, and the widget's binding contract. For topology changes, inspect `NodeGraphEngine`, `graphLinkAllowed()`, the shared rewire pass, cycle and input checks, and the UI-free `CircuitRuntime` tests. Editor edit-state work belongs in `EditorCommands` tests, including rejected edits, view synchronization, and revision behavior.

For persistence changes, use save and load round trips and malformed-project tests, including component rollback and saved-index mappings, path containment, checked integer fields, and atomic-save failure. Network Analyzer changes belong with exact-path and isolated-clone tests; Receiver Requirements changes span the UI-free measurement and evaluation tests and project-persistence tests. Extension changes should use manifest, discovery, trust, containment, and external-tool tests; do not make a plugin a back door into the engine registry.

Agent changes belong with the `test_agent_*` and `test_mcp_bridge` suites. Run `ctest --test-dir build -R 'test_agent_|test_mcp_bridge' --output-on-failure`, and see the [Testing Guide](../testing/guidance.md).

The Test Flow panel and the `test_flow_run` agent tool both call `RunFlowWithinBoundary()`, which snapshots every engine, runs the sweep, restores each snapshot independently, and rewires. `test_flow_boundary` covers that boundary, and `test_test_flow_widget` covers the panel's row limit and restore-failure latch.

## Source map

| Area | Primary sources |
|---|---|
| Bootstrap and frame lifecycle | `src/main.cpp`, `core/include/core.h`, `core/src/core.cpp` |
| Circuit topology and DSP runtime | `app/include/circuit_runtime.h`, `app/src/circuit_runtime.cpp`, `node_graph/include/node_graph_engine.h`, `node_graph/src/rewire.cpp` |
| Editor commands and graph actions | `app/include/editor_commands.h`, `app/src/editor_commands.cpp`, `app/src/graph_editor_actions.cpp` |
| App orchestration and UI | `app/include/app.h`, `app/src/app.cpp` |
| Agent interface | `agent/AGENTS.md`, `agent/api/include/agent_host.h`, `app/include/app_agent_host.h`, `app/src/app_agent_host.cpp` |
| Component registry and dispatch | `app/include/component_registry.h`, `app/include/component_type_registry.h` |
| Persistence and path containment | `app/include/project_serializer.h`, `app/src/project_serializer.cpp` |
| Common signal contracts | `common/component_interface.h`, `common/spectrum.h`, `common/signal_node.h` |
| Libraries and data | `app/include/component_library.h`, `app/src/component_library.cpp`, `component_data/` |
| Extensions | `app/include/extension_manifest.h`, `app/src/extension_manifest.cpp`, `app/src/extension_manager.cpp`, `app/src/external_tool_runner.cpp` |
| Instruments and measurement tools | `network_analyzer/`, `app/include/receiver_requirements.h`, `app/src/receiver_requirements.cpp`, `app/src/receiver_performance_measurement.cpp`, `spectrum_analyzer/`, `power_meter/`, `test_flow/` |
| Session, layout, and tutorial state | `common/session_state.h`, `layout/include/layout_manager.h`, `tutorial/include/tutorial_state.h` |
