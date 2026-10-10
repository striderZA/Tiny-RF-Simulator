---
type: Architecture
title: Agent Interface Architecture
description: Layers, dependency rules, trust boundaries, and security contracts of the rfsim-agent subsystem, which lets an MCP client read and edit a running RF Simulator through rf-sim-mcp. Read this before changing agent transport, protocol, or tool boundaries.
tags: [agent, mcp, architecture, protocol, security, rf-simulator]
verified:
  - by: openwiki/0.7.0
    at: 2026-10-10T17:27:48.662Z
sources:
  - id: openwiki-source-4dd766881eeb1848d297e025
    resource: repo://agent/AGENTS.md
  - id: openwiki-source-39e4bbac2a1d3ca7cc95bd2b
    resource: repo://agent/api/CMakeLists.txt
  - id: openwiki-source-b406d12d0f86c2f3bc435e82
    resource: repo://agent/api/include/agent_host.h
  - id: openwiki-source-cb224f0d93a904220be7cf22
    resource: repo://agent/api/src/agent_server.cpp
  - id: openwiki-source-397ec13cde90874f660679b1
    resource: repo://agent/mcp/AGENTS.md
  - id: openwiki-source-3fa73b0f154188231bdb99e6
    resource: repo://agent/mcp/CMakeLists.txt
  - id: openwiki-source-3b88510b7b2b163db68c57bc
    resource: repo://agent/mcp/src/mcp_session.cpp
  - id: openwiki-source-671643534997226ff2506cf3
    resource: repo://agent/protocol/CMakeLists.txt
  - id: openwiki-source-3a5e3c31d11cb0a3a9428c12
    resource: repo://agent/protocol/include/agent_catalog.h
  - id: openwiki-source-ab1b628cf8814f0e781b181a
    resource: repo://agent/protocol/include/agent_wire.h
  - id: openwiki-source-ea4d2883fe61311539ee1809
    resource: repo://agent/protocol/src/agent_catalog.cpp
  - id: openwiki-source-5cccc848d29020d63b22e842
    resource: repo://agent/protocol/src/agent_endpoint.cpp
  - id: openwiki-source-a22e52612dd2493037e131ad
    resource: repo://agent/protocol/src/agent_socket.cpp
  - id: openwiki-source-748740b94cb058f83f991fb4
    resource: repo://agent/protocol/src/agent_token.cpp
  - id: openwiki-source-566e993351b4fc9de152907f
    resource: repo://app/AGENTS.md
  - id: openwiki-source-11d3b4337d1de5e03c6cb2d1
    resource: repo://app/include/app_agent_host.h
  - id: openwiki-source-8e028f5320373887a239daa1
    resource: repo://app/include/app.h
  - id: openwiki-source-5f1fbd4979e8254a53e79f25
    resource: repo://app/src/app.cpp
  - id: openwiki-source-d364d949938a433276255c32
    resource: repo://src/main.cpp
generated: { by: "omp", at: "2026-10-10T17:27:48.662Z" }
---

# Agent Interface Architecture

The `agent/` subsystem lets an MCP client inspect and edit the circuit in a running RF Simulator window and run bounded measurements. It has four layers with one-way dependencies. The GUI process owns all simulator state. The `rf-sim-mcp` bridge owns none: it translates MCP JSON-RPC over stdio into `rfsim-agent/1` lines on a loopback socket.

<!-- openwiki: mermaid parse failed and this diagram was converted to a text fence so it does not break rendering. Fix the diagram source and restore the mermaid fence. Parser error: Heuristic: an unescaped angle bracket inside a label breaks rendering; rephrase the label. -->
```text
flowchart LR
    Client["MCP client"] -->|"JSON-RPC over stdio"| Bridge["rf-sim-mcp<br/>mcp_adapter: McpSession, GuiLink"]
    Bridge -->|"rfsim-agent/1 over 127.0.0.1"| Server["AgentServer<br/>listener thread, pump()"]
    Server --> Api["AgentApi tools<br/>simulator::agent_api"]
    Api -->|"IAgentHost"| Host["AppAgentHost<br/>app"]
    Host --> Editor["EditorCommands, CircuitRuntime"]
    Protocol["simulator::agent_protocol<br/>framing, sockets, tokens, endpoint, catalog"] -.-> Bridge
    Protocol -.-> Server
```

## Layers

| Layer | Target and location | Owns | Must not |
|---|---|---|---|
| Protocol | `simulator::agent_protocol`, `agent/protocol/` | Newline-delimited framing, loopback sockets, tokens, endpoint file, error envelope, tool catalog | Include or link simulator or UI code |
| MCP adapter and bridge | `simulator::mcp_adapter`, `rf-sim-mcp`, `agent/mcp/` | JSON-RPC session, protocol eras, `GuiLink` relay with lazy connect | Touch simulator state, or link engine, app, or UI modules |
| Agent API and server | `simulator::agent_api`, `agent/api/` | Tool implementations, `AgentServer`, argument validation, epochs | Link `simulator::app`, ImGui, ImPlot, ImNodes, or GLFW |
| App host | `AppAgentHost`, `AgentPanelWidget`, `RfSimulatorApp` in `app/` | Checkpoints, node placement, modal detection, activity log, panel | Expose its internals to the API beyond `IAgentHost` |

## Dependency rules

- `simulator::agent_protocol` publicly exposes only its own headers and `nlohmann_json`. Its Windows system libraries (bcrypt, advapi32, user32, ws2_32) are private, and it links no simulator or UI target.
- `simulator::agent_api` links the protocol library, UI-free editor services, the network-analyzer, spectrum-analyzer, and power-meter engines, the test-flow and Touchstone libraries, and Threads. It does not link `simulator::app` or any UI library.
- `simulator::mcp_adapter` exposes only the protocol library and Threads publicly; logging stays private. `rf-sim-mcp` links the adapter privately and installs into `bin`.
- `rf-sim-mcp` answers MCP discovery and `tools/list` without a reachable GUI, and accepts `--endpoint`, `--version`, and `--help`.
- The API reaches application behavior only through `IAgentHost`, a seven-method interface. Circuit edits still route through `EditorCommands`.
- `AppAgentHost` implements `IAgentHost`, and `RfSimulatorApp` owns the API, the server, and the server opt-in as members.

## Startup and threading

- `src/main.cpp` constructs `RfSimulatorApp`, calls `startAgentServerIfEnabled()`, and then runs the frame loop.
- The server is off by default. The persisted `[Agent] ServerEnabled` key decides startup, and enabling or disabling the server writes that key.
- Each frame, `RfSimulatorApp::update_dsp()` calls `AgentServer::pump()` before the circuit runtime updates.
- The GUI listener thread handles framing and validated envelopes only. It shares just a mutex-protected inbox, outbox, connection status, and pump-refreshed epoch snapshot with the UI thread. `pump()` runs on the UI thread, checks modal state, and invokes the executor in FIFO order.
- The bridge reads stdin on a reader thread, runs `McpSession` on a session thread, and dispatches queued calls serially in arrival order on one caller thread.

## Trust boundary

- **Loopback only.** The GUI listener binds the IPv4 loopback address on an ephemeral port. `AgentListener::accept` closes any accepted peer that is not on loopback.
- **One client.** While a client is connected, a second connection receives a `BUSY` reply ("another agent is connected") and is closed.
- **Handshake.** The bridge's hello must present the bridge token, checked with `agentTokensEqual`. The GUI token is a separately generated token, regenerated until it differs from the bridge token.
- **Tokens.** `generateAgentToken()` draws 32 bytes from the OS CSPRNG (`BCryptGenRandom` on Windows, `getrandom` on Linux) and returns 64 lowercase hex characters. Without a usable source it returns no token rather than weaker entropy.
- **Comparison.** `agentTokensEqual` returns false for unequal lengths. Otherwise it ORs per-byte XOR differences across the whole value before deciding.
- **Endpoint file.** The GUI writes `agent-endpoint-<install>.json` in the endpoint directory from `defaultAgentEndpointDirectory()`. `<install>` is the 16-hex FNV-1a of the weakly canonical executable directory, so each install gets its own file.
- **Endpoint permissions.** On POSIX the endpoint directory must be owned by the current user with mode 0700 and no group or other bits. Endpoint temp files are created 0600 with `O_EXCL` and `O_NOFOLLOW`. Writes and token-gated removals hold an exclusive `flock` on the directory. On Windows, current-user-only DACLs are applied from SDDL strings.
- **Inheritance.** The listener and each accepted channel are close-on-exec, so tools the GUI launches cannot inherit them.
- **Hostile input.** A line over 1 MiB is reported once and discarded through its newline. JSON nested deeper than 64 containers is refused by an iterative check before any recursive copy or dump.

## Catalog and compatibility

- `kAgentCatalogVersion` is 1. The catalog defines exactly ten v1 tools: `component_types`, `library_search`, `circuit_get`, `component_get`, `circuit_edit`, `measure_port`, `network_analyzer_sweep`, `data_file_read`, `test_flow_run`, and `receiver_measure`.
- Within one catalog version, changes are additive. Removing or retyping a field or tool requires a version increment.
- No catalog tool grants extension trust, runs external tools, authors component-library entries, imports or exports `.rflib` files, or opens, saves, or names a file. `data_file_read` takes no paths and never writes, and agents may read only data files a component or library part already references.
- The bridge verifies the GUI token and catalog version before relaying a call. A malformed reply or JSON-RPC error drops the channel, and a later call reconnects.
- The MCP session selects a protocol era per request: legacy initialization-era versions `2025-06-18` and `2025-11-25`, or the stateless `2026-07-28` era, which carries its own version and capability metadata.

## Related pages

- [Agent Tool-Call Lifecycle](../workflows/agent-tool-calls.md) traces one call from the client to the reply and lists each tool's contract.
- [Connecting an Agent](../operations/agent-connection.md) covers enabling the server and configuring an MCP client.
- [Architecture Overview](overview.md) places the agent layer in the whole application.
- [Testing Guide](../testing/guidance.md) lists the `test_agent_*` and `test_mcp_bridge` suites.
