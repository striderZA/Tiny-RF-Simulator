---
type: "Reference"
title: "Connecting an Agent"
openwiki_generated: true
sources:
  - id: openwiki-source-4dd766881eeb1848d297e025
    resource: repo://agent/AGENTS.md
  - id: openwiki-source-b44b8ebde50a9ef409837fba
    resource: repo://agent/api/include/agent_server.h
  - id: openwiki-source-cb224f0d93a904220be7cf22
    resource: repo://agent/api/src/agent_server.cpp
  - id: openwiki-source-197afc5ab7e589db0ce8d4b1
    resource: repo://agent/mcp/include/gui_link.h
  - id: openwiki-source-7922575a10bdc381fde50022
    resource: repo://agent/mcp/src/gui_link.cpp
  - id: openwiki-source-50babbac90e7fa61b5043d34
    resource: repo://agent/mcp/src/main.cpp
  - id: openwiki-source-109ef5962c4be826d7759ce5
    resource: repo://agent/mcp/src/mcp_bridge.cpp
  - id: openwiki-source-5cccc848d29020d63b22e842
    resource: repo://agent/protocol/src/agent_endpoint.cpp
  - id: openwiki-source-a22e52612dd2493037e131ad
    resource: repo://agent/protocol/src/agent_socket.cpp
  - id: openwiki-source-748740b94cb058f83f991fb4
    resource: repo://agent/protocol/src/agent_token.cpp
  - id: openwiki-source-9467eec4bb12b80bf9739aa7
    resource: repo://app/src/agent_panel_widget.cpp
  - id: openwiki-source-5f1fbd4979e8254a53e79f25
    resource: repo://app/src/app.cpp
  - id: openwiki-source-65d144a9bd5b3da69b76d204
    resource: repo://common/AGENTS.md
  - id: openwiki-source-3ce882f1e6c92c2ec4ddc6c9
    resource: repo://common/session_state.h
  - id: openwiki-source-23775c3de52f3ab95a13cb8b
    resource: repo://README.md
generated: { by: "omp", at: "2026-10-10T18:57:17.727Z" }
verified:
  - by: openwiki/0.7.0
    at: 2026-10-10T18:57:17.727Z
---


# Connecting an Agent

An MCP client starts the `rf-sim-mcp` bridge, which relays tool calls to the running RF Simulator GUI over a loopback socket. Use this page to enable the server, configure the client, understand how the bridge finds the GUI, and diagnose failures. The protocol and tool contracts are in [Agent interface architecture](../architecture/agent-interface.md) and [Agent tool calls](../workflows/agent-tool-calls.md).

## Enable the Agent Server

1. Start RF Simulator and open **View > Agent**.
2. Tick **Agent Server**. The server is off by default. On Windows the opt-in is saved as `ServerEnabled` in the `[Agent]` section of `app.ini`.
3. Read the Status line. It shows one of five states; see [Diagnose failures](#diagnose-failures).

On Windows the saved state is kept in `app.ini` beside the executable. Elsewhere the opt-in does not survive a restart, so enable the server again after each launch.

## Configure the client

Add this generic MCP server configuration to the client:

```json
{
  "mcpServers": {
    "rf-sim": {
      "command": "<install dir>/rf-sim-mcp"
    }
  }
}
```

For Claude Code, run:

```sh
claude mcp add rf-sim -- "<install dir>/rf-sim-mcp"
```

Replace `<install dir>` with the directory containing the simulator executables. On Windows, use `rf-sim-mcp.exe` in the JSON `command` and the Claude Code command. Agents cannot open, save, or name files.

## Endpoint discovery and permissions

- By default `rf-sim-mcp` reads `agent-endpoint-<key>.json` from the endpoint directory. On Windows the directory is `%LOCALAPPDATA%\rf-sim\agent`; elsewhere it is `$XDG_RUNTIME_DIR/rf-sim` when that variable is set, otherwise `~/.rf-sim/run`. The key is the FNV-1a 64-bit hex digest of the canonical executable directory, lower-cased on Windows.
- The endpoint file must be a private regular file owned by the current user, inside a directory private to that user.
- The server writes its endpoint at start and, on stop, removes it only while the file still holds the GUI token the server owns.
- The bridge's hello must present the bridge token, which the server checks with `agentTokensEqual`.
- The GUI listener binds only the IPv4 loopback address on an ephemeral port. Accepted peers that are not loopback are dropped, and `connectAgentLoopback` targets only loopback.
- The README states that the bridge protects the endpoint from other local accounts and web pages, but not from software running as the same user or as an administrator.

## rf-sim-mcp options

- `--endpoint <file>` reads the GUI endpoint from the given file instead of the default discovery.
- `--version` prints the bridge version to stdout; `--help` prints usage to stdout.
- Any other argument, or `--endpoint` without a path, prints usage to stderr and exits with status 2.

## Restart behavior

- Each server start generates the session tokens, keeps the GUI token distinct from the bridge token, and writes a new endpoint file.
- Each GuiLink connect reads the endpoint file, so a bridge that reconnects after a server restart uses the endpoint the restarted server wrote.
- On Windows, a saved opt-in is retried at the next launch. A failed start keeps the opt-in set and shows the error in the Agent window.

## Diagnose failures

The Agent window Status line shows one of these states: `Off`, `Waiting for a client`, `Connected: <client name> <client version>`, `Busy: calls are waiting`, or `Error: <reason>`. A startup failure appears as `Error` with the startup message.

| Symptom | Meaning | Action |
| --- | --- | --- |
| `SIMULATOR_UNAVAILABLE` | The endpoint file cannot be read, the connect failed, or the GUI reply was malformed | Confirm RF Simulator is running with the Agent Server enabled and that this install's endpoint file exists |
| stderr `RF Simulator unreachable: connect loopback failed with Winsock error 10061` | A refused loopback connect; the OS cause is reported | Start the GUI and enable the Agent Server |
| stderr `RF Simulator unreachable: connect loopback timed out` | No OS cause was reported within the 3000 ms connect timeout | Check that the GUI process is running and the endpoint file is current |
| `VERSION_MISMATCH` | The endpoint catalog version differs from the bridge's | Run the bridge and the GUI from the same build |
| `BUSY` with a dialog message | A modal dialog stayed open for 30 s; calls park while it is open | Ask the user to close the dialog, then retry |
| `BUSY` with `another agent is connected` | A second client connected while one was active; it is disconnected | Disconnect the other client |

## Manual acceptance

Automated coverage runs with `ctest --test-dir build -R 'test_agent_|test_mcp_bridge' --output-on-failure`. Real-client interoperability, minimized-window behavior, and server-restart persistence are not covered by automated tests and remain manual acceptance items.

## Related pages

- [Agent interface architecture](../architecture/agent-interface.md)
- [Architecture overview](../architecture/overview.md)
- [Build runbook](build-runbook.md)
- [Quickstart](../quickstart.md)
- [Testing guidance](../testing/guidance.md)
- [Agent tool calls](../workflows/agent-tool-calls.md)
