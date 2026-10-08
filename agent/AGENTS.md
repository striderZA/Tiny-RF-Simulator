# Agent subsystem — AGENTS.md

## Purpose

Provide the UI-independent protocol and integration boundary for agent clients.

## Ownership

- `protocol/` owns `simulator::agent_protocol`: newline framing, loopback line sockets, shared error/result encoding, and private endpoint-file storage.
- Agent-facing circuit edits belong in the API layer and must go through `EditorCommands`; protocol code does not edit simulator state.
- Agent v1 does not initiate file operations. It may read data files already referenced by a component or library part.

## Local Contracts

- `simulator::agent_protocol` exposes only its own public headers, `nlohmann::json`, and standard C++; it must not include or link simulator or UI dependencies. On Windows it defines `NOMINMAX` and `WIN32_LEAN_AND_MEAN`; socket and existing platform libraries are linked only on Windows.
- The wire identifier is `rfsim-agent/1`. Messages are one JSON value per newline-delimited line, capped at 1 MiB; one trailing CR is stripped. An oversized line is reported once and discarded through its newline before parsing resumes.
  - `AgentListener` binds an ephemeral IPv4 port on `127.0.0.1` only; `connectAgentLoopback()` also targets only that address. Windows initializes Winsock once through a function-local static and sets `SO_EXCLUSIVEADDRUSE` before bind.
  - `AgentChannel` is move-only and owns its socket plus `AgentLineReader`. Reads return `Line`, `Timeout`, `Closed`, `Oversized`, or `Error`, with timeout bounded across the whole wait; writes reject embedded LF and append one LF.
- Error names are `UPPER_SNAKE`. Tool errors use `{epoch, error: {code, message, hint?, op_index?, details?}}`; absent optional fields are omitted and absent epochs are JSON `null`. JSON-RPC agent errors use `-32000` and put the agent error name in `data.code`.
- Non-finite numbers encode as JSON `null`. Within a `catalog_version`, changes are additive; removing or retyping a field or tool requires a version increment.
- Session tokens use 32 OS-CSPRNG bytes encoded as 64 lowercase hexadecimal characters; generation fails closed without fallback entropy. Equal-length tokens are compared across every byte with an XOR accumulator; unequal lengths are rejected.
- Endpoint files are named `agent-endpoint-<install>.json`, where `<install>` is the 16-lowercase-hex FNV-1a of the weakly canonical executable directory (lowercased before UTF-8 encoding on Windows). The POSIX directory and file are current-user-owned with modes 0700 and 0600; Windows uses protected current-user-only DACLs (`D:P(A;OICI;FA;;;<SID>)` for directories, `D:P(A;;FA;;;<SID>)` for files).
- Endpoint JSON has exactly the `rfsim-agent/1` keys and schema; deletion requires the matching GUI token.
- Never expose extension trust grants/revocations, external-tool execution, component-library authoring, or `.rflib` import/export to agents.
- Any listener thread under this subsystem may share only the mutex-protected inbox, outbox, and connection status with the UI thread. It must not touch simulator engines, graph state, or UI objects.

## Work Guidance

Keep protocol code transport-neutral and small. Put simulator integration behind the agent API and app-owned host interfaces; preserve the target's dependency boundary.

## Verification

- `ctest --test-dir build -R 'test_agent_|test_mcp_bridge' --output-on-failure`

## Child DOX Index

No child docs. `protocol/` is owned by this subsystem contract.
