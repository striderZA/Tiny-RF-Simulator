---
type: Runtime workflow
title: DSP Pipeline & Runtime Workflows
description: Follow real-time signals through CircuitRuntime rewiring and topological updates, ADC DDC, PFB channel outputs, analyzer measurements, and revision-based project persistence.
tags: [dsp, pipeline, runtime, adc, pfb, project-lifecycle]
sources:
  - id: openwiki-source-8e028f5320373887a239daa1
    resource: repo://app/include/app.h
  - id: openwiki-source-e81f2756b009d1486dba9b74
    resource: repo://app/include/editor_commands.h
  - id: openwiki-source-5f1fbd4979e8254a53e79f25
    resource: repo://app/src/app.cpp
  - id: openwiki-source-bc033392c5f8ce0dd75226a2
    resource: repo://app/src/circuit_runtime.cpp
  - id: openwiki-source-7ffec1c215e8323dc7328f1d
    resource: repo://app/src/editor_commands.cpp
  - id: openwiki-source-fc8796ed916987c3e98901f5
    resource: repo://app/src/pfb_view_manager.cpp
  - id: openwiki-source-baedf8f3f47fa931244e3545
    resource: repo://app/src/project_serializer.cpp
  - id: openwiki-source-e83e99f47c11c31e339b7c5d
    resource: repo://common/component_engine_base.h
  - id: openwiki-source-d7839e83f1db8019b777d76e
    resource: repo://common/graph_link_policy.h
  - id: openwiki-source-3ce882f1e6c92c2ec4ddc6c9
    resource: repo://common/session_state.h
  - id: openwiki-source-6236844d67c4b6a4f4573508
    resource: repo://layout/src/layout_manager.cpp
  - id: openwiki-source-318263a7857c897eac3da71e
    resource: repo://network_analyzer/include/network_analyzer_engine.h
  - id: openwiki-source-b28cfd6787af0436a1efadec
    resource: repo://network_analyzer/src/measurement_chain_runner.cpp
  - id: openwiki-source-7bf29eec72cf699ce4b4dd02
    resource: repo://node_graph/src/node_graph_engine.cpp
  - id: openwiki-source-af2992b0d3fea85bc257c74b
    resource: repo://node_graph/src/node_graph_widget_groups.cpp
  - id: openwiki-source-7483c8c3ea0d9c325c992db0
    resource: repo://node_graph/src/rewire.cpp
  - id: openwiki-source-da71d99de250f4d21ae4f02e
    resource: repo://pfb_channelizer/src/pfb_channelizer_engine.cpp
  - id: openwiki-source-d364d949938a433276255c32
    resource: repo://src/main.cpp
  - id: openwiki-source-2394c03133ac41a39b442be4
    resource: repo://tests/test_circuit_runtime.cpp
  - id: openwiki-source-db3b11270b3af5e21d87157d
    resource: repo://tests/test_issue113_project_load.cpp
  - id: openwiki-source-5fd889a40ac7997f236a4479
    resource: repo://tests/test_issue116_collapsed_groups.cpp
  - id: openwiki-source-7a7ab924a79c85a9c15a41d5
    resource: repo://tests/test_issue37_pfb_input_removal.cpp
  - id: openwiki-source-42ef0db762a7ffb82713cae7
    resource: repo://tests/test_issue48_json_loader.cpp
  - id: openwiki-source-b7db63ed89e897f943bf619a
    resource: repo://tests/test_issue70_pfb_reconnect.cpp
  - id: openwiki-source-0f9230ecfacbecf62404c17d
    resource: repo://tests/test_issue77_save_failure.cpp
  - id: openwiki-source-4dfd7f9c1762ef4b4babacaf
    resource: repo://tests/test_issue78_multi_output.cpp
  - id: openwiki-source-44dc58c64deaf5ec52844046
    resource: repo://tests/test_network_analyzer.cpp
  - id: openwiki-source-a63c004bd18a88209a4fa853
    resource: repo://tests/test_node_hover_snr.cpp
  - id: openwiki-source-08f846c8582718824d718b09
    resource: repo://tutorial/src/tutorial_state.cpp
generated: { by: "omp", at: "2026-10-05T19:44:12.666Z" }
verified:
  - by: openwiki/0.7.0
    at: 2026-10-05T19:44:12.666Z
---

# DSP Pipeline & Runtime Workflows

The simulator has a per-frame signal loop and a separate project-edit lifecycle. `RfSimulatorCore` owns the platform/frame loop; `RfSimulatorApp` composes instruments and widgets; `CircuitRuntime` owns the live graph and engines; and `NodeGraphEngine` stores topology. The app delegates signal evaluation to the runtime before drawing views.

## Bootstrap and ownership boundaries

`src/main.cpp` creates `RfSimulatorCore`, the ImNodes context, and `RfSimulatorApp`, then registers an app callback that calls `update_dsp()` before `draw_ui()`. The core invokes that callback each frame and handles rendering and shutdown. App construction creates the UI adapters, seeds the default generator and amplifier, and routes widget edit requests through the editor services.

`CircuitRuntime` owns `NodeGraphEngine`, `ComponentRegistry`, `ViewManager`, and the live component ID counter. `GraphEditorActions` owns app-side probe/group mutations. `EditorCommands` is the user-edit gateway: accepted edits apply view/group-boundary side effects and advance the project revision; rejected edits change neither revision nor state. Parameter, node-position, and instrument edits that bypass a command explicitly mark the revision modified.

```mermaid
sequenceDiagram
    participant Main as src/main.cpp
    participant Core as RfSimulatorCore
    participant App as RfSimulatorApp
    participant Runtime as CircuitRuntime
    participant Graph as NodeGraphEngine
    participant Engines as Component engines
    participant UI as ImGui widgets
    Main->>Core: Run(frame callback)
    loop each frame
        Core->>App: update_dsp()
        App->>Runtime: update(dt)
        Runtime->>Graph: rewire inputs and request topological order
        Runtime->>Engines: update in graph order
        App->>Graph: resolve probed (SignalNode, output_index)
        Core->>App: draw_ui()
        App->>UI: draw editor and visible instruments
    end
```

*The runtime computes live spectra; the app publishes view targets only after that update finishes.*

## Link policy, rewiring, and component updates

`NodeGraphEngine` owns node, pin, link, probe, and group records, but no RF physics or ImGui interaction. `CircuitRuntime::connect()` first resolves each pin to a live component and verifies port ownership, then applies both `graphLinkAllowed()` and `NodeGraphEngine::canAddLink()` before committing. The graph's structural check rejects occupied input pins and directed cycles. Low-level `addLink()` remains permissive; application commands and project restoration go through the runtime.

`graphLinkAllowed()` enforces the PFB physical boundary: an RF signal may enter a PFB only from ADC output 0 to PFB input 0. A direct RF-chain-to-PFB link is rejected at connection time and when saved links are restored.

`rewireComponentInputs()` is the shared zero-copy binding pass. For every component input port it resolves the source `SignalNode` and output index, verifies physical policy and output bounds, and binds `node().inputs[k]` to the selected owned `Spectrum`, or to `nullptr` when the connection is absent or invalid. `CircuitRuntime::update()` rewires before evaluating `topologicalOrder()` and calling each component's `update()`. Connect, disconnect, and component removal also rewire synchronously; after removal, `EditorCommands` synchronizes component-bound views before the caller can draw again.

`topologicalOrder()` uses Kahn's algorithm over graph links whose pins resolve to nodes. Runtime connection checks keep ordinary edits acyclic and single-source. If low-level or corrupted state still contains a cycle, the orderer logs a warning and appends nodes it could not order; that is a defensive fallback, not a feedback-loop simulator.

## Spectrum and dirty propagation

`SignalNode` contains `const Spectrum*` input slots and owned output `Spectrum` values. A spectrum carries frequency bins, discrete tones, input and added noise PSD in W/Hz, phase, `fs_Hz`, `is_complex_baseband`, and a generation counter. `noise_total_W` is the sum of input and component-added noise. Engine dirty checks skip recomputation while parameters, input pointer, and input generation are unchanged; multi-input engines track a pointer/generation pair for every input.

Real-domain tones are stored at full engine power. The Spectrum Analyzer expands them into conjugate-symmetric half-power display entries; complex-baseband spectra are already signed and render without expansion. The ADC changes that domain flag and sample rate at the RF/digital boundary.

## ADC and PFB channelization

The ADC consumes the real RF spectrum at input sample rate `Fs`. It expands real tones into conjugate images, aliases `f_in - f_NCO` modulo `Fs`, drops tones outside the decimated complex passband, and coherently combines tones that land at the same output frequency. Its output rate is `Fs/D`; decimation is supported at 1/2/4/8 and the NCO is persisted as a normalized `Fs` fraction. ADC noise is remapped from the single-sided RF grid and configured NSD is added. The result is a complex-baseband `Spectrum`.

The PFB is physically downstream of that ADC output only. Its output 0 is the active channel; output 1 is the reconstructed full-band view. With `M` channels and input rate `Fs`, the channel output rate is `ratio * Fs/M`. The default sampling ratio is 1x; persisted 2x oversampling doubles usable channel bandwidth and output rate without moving channel centers. The active channel's tone weights and noise weights use the same prototype response.

For each active channel, the engine integrates the current input-grid noise density through `|H|² * bin_width`, and computes effective ENBW from the corresponding response-weighted widths. The inspector reports that channel noise in dBm and effective ENBW. PFB node-hover SNR compares the strongest finite active-channel tone with this integrated channel noise, independent of analyzer RBW. Ordinary component hover SNR and analyzer traces use the Spectrum Analyzer's RBW-based noise measurement.

```mermaid
flowchart LR
    RF["Real RF Spectrum"] --> ADC["ADC: alias, DDC, decimate"]
    ADC -->|complex-baseband Spectrum| PFB["PFB channelizer"]
    PFB --> CH["Output 0: active channel"]
    PFB --> FULL["Output 1: full-band reconstruction"]
    CH -->|integrated noise + ENBW| Hover["PFB SNR / inspector"]
    FULL --> Probe["Indexed graph probe"]
    CH --> Probe
    Probe --> Analyzer["Spectrum Analyzer: RBW-based display/SNR"]
```

## Probes and analyzer workflows

`NodeGraphEngine::getSourceForInput()` and `probedSignalNodes()` preserve an output index. `RfSimulatorApp::update_dsp()` turns each probed pin into a `(SignalNode*, output_index)` target, labels nonzero ports as `OUT2` and above, updates `view_enabled`, and refreshes PFB references in the Spectrum Analyzer and inspector. Splitter/PFB output 1 therefore remains distinct through wiring, project save/load, and probe display.

The Spectrum Analyzer separates tone impulses from noise power, applies RBW to each, then applies VBW and trace mode. Optional display jitter is cosmetic on the noise floor only; deterministic tone peaks remain stable. Its strongest-tone SNR uses the current RBW-filtered noise and is read-only with respect to render caches/history.

The Network Analyzer is a singleton instrument, not a graph node. Point A and B select real output pins; path discovery requires one simple path and carries exact input and output port indices. A singly-fed 2:1 RF switch is supported, while combiner paths, dual-fed switches, and ambiguous branches are rejected. The runner clones downstream engines into an RAII scratch graph, injects a synthetic tone comb, and reads the requested cloned output. The live graph and live component state are not modified by the measurement.

## Graph groups and collapsed views

Groups are visual graph metadata, not a second DSP scheduler. `NodeGraphEngine` stores group membership and collapsed state, while `GraphEditorActions` rebuilds derived boundary pins after topology changes. Node removal also removes its links and probes, updates membership, drops undersized groups, and rebuilds surviving boundaries.

Collapsed group members leave the ImNodes pool while hidden. The widget keeps pan-independent grid positions and uses that cache to place collapsed blocks. `ProjectSerializer` captures every loaded node's grid position immediately after restoring component positions, before collapsed members can disappear from the widget pool. Links between different collapsed groups are drawn through both groups' synthesized boundary pins.

## Dirty state and destructive project actions

Project dirtiness is revision-derived in `EditorCommands`, not a separate app boolean. Accepted component, topology, or group edits advance the revision; rejected commands and selection/probe-only changes retain their defined non-edit semantics. Widget parameter, node-position, and instrument edits call `markModified()`. A successful save, load, or New records the clean revision.

New, Open, Exit, and Tutorial check `isDirty()`. If dirty, the app stores the pending action and opens the Save/Discard/Cancel dialog. Save proceeds only after the revision becomes clean; Discard runs the pending action, and Cancel clears it. Failed saves leave the project dirty, while load validation distinguishes unusable top-level sections (reset to empty) from guarded scalar failures that preserve the open project.

## Project persistence and recovery

`ProjectSerializer` saves component type/parameters, positions, links and probes as component-index/port references, Network Analyzer endpoints/sweep state, groups, a subset of window flags, graph counters, and Receiver Requirements configuration. It writes a sibling `.tmp` file, checks write/flush/close, and renames it over the target only on success. A failed save leaves the old target intact.

Load checks file/JSON and top-level section shapes before restoration. A wrong-shaped project section resets to an empty project and returns failure; malformed guarded fields such as window flags or the component counter are rejected before reset. Valid components are created in saved order and mapped by original saved index. Bad siblings are skipped with `-1` mappings, and components that throw after registration are removed; links, probes, analyzer points, and groups restore only through still-valid mappings. Restored links pass the same `CircuitRuntime::connect()` policy as live edits, then group boundary pins are rebuilt after topology and membership are present.

Session and project state remain distinct. `SessionState` persists window visibility and per-PFB view flags to `app.ini` on Windows and is a no-op on other platforms. The `.rfsim` file persists a subset of project window flags and graph state. `LayoutManager` owns the exe-relative default/named ImGui layouts, and `TutorialState` owns its exe-relative completion marker.

## Focused tests

- `tests/test_circuit_runtime.cpp`: link validation, ADC-only PFB wiring, zero-copy chain update, and synchronous disconnect/removal rewiring.
- `tests/test_issue42_multi_output.cpp` and `tests/test_issue78_multi_output.cpp`: indexed output wiring and output-port persistence.
- `tests/test_issue37_pfb_input_removal.cpp` and `tests/test_issue70_pfb_reconnect.cpp`: immediate dangling-pointer prevention and ADC-only reconnect/project-load policy.
- `tests/test_pfb_sampling_ratio.cpp`, `tests/test_pfb_filter_design.cpp`, and `tests/test_node_hover_snr.cpp`: channel centers/rates, integrated ENBW/noise, and RBW-independent PFB hover SNR.
- `tests/test_network_analyzer.cpp`: unique paths, exact ports, singly-fed switch support, rejected paths, cloned runs, and live-state isolation.
- `tests/test_issue77_save_failure.cpp`, `tests/test_issue113_project_load.cpp`, `tests/test_issue48_json_loader.cpp`, and `tests/test_issue116_collapsed_groups.cpp`: atomic save, load validation/rollback, indexed restoration, and collapsed layout recovery.
