---
type: Domain Concept
title: Measurement Chains
description: How the Network Analyzer, the Receiver Requirements output-power and IIP3 measurements, and the agent network_analyzer_sweep and receiver_measure tools discover the circuit between two output ports, which topologies are accepted or refused, and how that circuit runs on isolated clones.
tags: [network-analyzer, receiver, measurement, topology, agent, rf-simulator]
sources:
  - id: openwiki-source-4dd766881eeb1848d297e025
    resource: repo://agent/AGENTS.md
  - id: openwiki-source-d37ecc0aeb17155d7b54881a
    resource: repo://agent/api/src/tool_measure.cpp
  - id: openwiki-source-594ad266217b9f37cd806391
    resource: repo://agent/api/src/tool_receiver.cpp
  - id: openwiki-source-ea4d2883fe61311539ee1809
    resource: repo://agent/protocol/src/agent_catalog.cpp
  - id: openwiki-source-8e028f5320373887a239daa1
    resource: repo://app/include/app.h
  - id: openwiki-source-8f5abf4941e49fa24791bf7d
    resource: repo://app/include/receiver_performance_measurement.h
  - id: openwiki-source-5f1fbd4979e8254a53e79f25
    resource: repo://app/src/app.cpp
  - id: openwiki-source-76cf00e5eb6f86db297b4153
    resource: repo://app/src/receiver_performance_measurement.cpp
  - id: openwiki-source-b37ed39bae6e7f5fea295b77
    resource: repo://network_analyzer/include/measurement_chain_runner.h
  - id: openwiki-source-b28cfd6787af0436a1efadec
    resource: repo://network_analyzer/src/measurement_chain_runner.cpp
  - id: openwiki-source-9a250414ad94d8c41379ca9b
    resource: repo://network_analyzer/src/network_analyzer_engine.cpp
  - id: openwiki-source-d8865e23d4aeb9130e4e47be
    resource: repo://tests/test_agent_api.cpp
  - id: openwiki-source-44dc58c64deaf5ec52844046
    resource: repo://tests/test_network_analyzer.cpp
  - id: openwiki-source-82e3ad99f40a09c6e07d781b
    resource: repo://tests/test_receiver_performance_measurements.cpp
generated: { by: "omp", at: "2026-10-10T19:01:57.003Z" }
verified:
  - by: openwiki/0.7.0
    at: 2026-10-10T19:01:57.003Z
---

# Measurement Chains

Three consumers measure the circuit between two output ports: the Network Analyzer panel, the Receiver Requirements output-power and IIP3 measurements, and the agent `network_analyzer_sweep` and `receiver_measure` tools. They share one discovery function, `findMeasurementChainPath()`, and one execution primitive, `IsolatedChainRunner`, so the panel and the agent describe the same circuit. The agent's `measure_port` tool does not use this path; it reads a live output port after recomputing the runtime.

## Vocabulary

- **Point A** is an output pin. Its component's output is replaced by a synthetic tone-comb stimulus, so Point A's component is the measurement source and is never cloned.
- **Point B** is an output pin. Its exact output port is the response that is read.
- **The measured circuit** is every component on a forward path from Point A's component to Point B's component. Each edge records the exact output and input port indices it connects.

## Discovery rules

`findMeasurementChainPath()` returns the measured circuit or nothing. It refuses when:

- Point B is unreachable from Point A, or both pins belong to one component.
- The circuit has a cycle, or Point B's component is not the last component in topological order.
- An input is fed from outside the circuit, such as a second live source.
- An input pin has two links.
- Point A's component feeds the circuit through more than one output, because the shared stimulus would ignore how that component divides its signal, such as the throw of a 1:2 switch.
- A component with several inputs is not an RF SPDT 2:1 switch (`rf_switch_spdt_2to1`), so combiners are never measured.

Branches that cannot reach Point B are pruned, and duplicate links between the same pins collapse to one edge. A switched filter bank whose branches rejoin at a 2:1 switch is accepted: the selected throw passes at insertion loss and the other throw at isolation. The tests also pin the rejected cases: a combiner, a combiner rejoining a fan-out, a switch throw fed from outside the circuit, two links into one input, and a cycle.

## Isolated execution

`IsolatedChainRunner::prepare()` asks the host for a scratch pass, creates a clone of every component after Point A by type and ID, copies each live component's state into its clone, and wires each edge by pointing the clone's input at the source clone's output. Preparation fails unless at least one edge carries the stimulus and Point B's output port exists. `run()` points the stimulus inputs at the supplied spectrum, updates the clones in topological order, and returns Point B's output from the last clone. That returned spectrum belongs to a scratch clone, so it is invalidated by the next `run()` call or by destroying the runner.

The application supplies the scratch host. `RfSimulatorApp::NaHost` starts each pass with an `NaScratch` that owns its own `NodeGraphEngine` and creates clones by registry type. A measurement never reads the live simulation for signal purposes and never writes to it.

## Consumers and reason codes

- **Network Analyzer panel.** With no chain, the gain and noise-figure arrays are NaN. The engine reuses its last result while the path signature and sweep settings are unchanged.
- **`network_analyzer_sweep`.** When the sweep has no valid points, the tool returns `NO_MEASUREMENT` with `details.reason` set to `NO_PATH` when no chain exists or the chain has fewer than two components, and to `NO_VALID_POINTS` otherwise.
- **`receiver_measure`.** With no chain it returns `NO_MEASUREMENT` with reason `NO_PATH`. Each call advances the sweep by at most 4 ms, so callers repeat the call while `in_progress` is true.
- **Receiver Requirements.** The engine re-discovers the chain on each update. A completed request repeated unchanged returns its cached results early. An unchanged request still in progress resumes its existing runner. A changed request resets the measurements to unavailable; when a chain exists and the runner can be prepared, it prepares a new `IsolatedChainRunner`, and otherwise the request completes with unavailable results. Each update stops at a 4 ms wall-clock budget or at an optional run cap (`setMaxRunsPerUpdate`), so a sweep resumes across frames.

For analyzer SNR and PFB hover SNR, see [DSP Pipeline & Runtime Workflows](../workflows/dsp-pipeline.md).

## Where to change this

Discovery rule changes belong in `network_analyzer/src/measurement_chain_runner.cpp`, with matching cases in `tests/test_network_analyzer.cpp` (`[network_analyzer][runner]` and `[issue169]`). Receiver sweep changes belong in `app/src/receiver_performance_measurement.cpp`, covered by `tests/test_receiver_performance_measurements.cpp` (`[receiver_measurements]`). Changes to the agent reason codes must match the catalog's error schema and the `network_analyzer_sweep` and `receiver_measure` cases in `tests/test_agent_api.cpp`.
