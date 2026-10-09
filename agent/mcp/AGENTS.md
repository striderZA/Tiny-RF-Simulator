# MCP adapter — AGENTS.md

## Purpose

Provide the UI- and simulator-independent MCP session layer over the agent tool catalog.

## Ownership

- `simulator::mcp_adapter` owns JSON-RPC envelope handling, initialization-era negotiation for 2025-06-18/2025-11-25, stateless 2026-07-28 request metadata, tools discovery/call conversion, and cancellation ID handoff.
- `GuiLink` owns lazy authenticated `rfsim-agent/1` loopback transport for `tools/call`: endpoint lookup, client hello, GUI-token/catalog verification, ordered-argument relay, reply/error conversion, bounded connect/hello/call waits, and failed-channel drop with retry on a later call.
- `McpToolCall` carries ordered JSON arguments to the `GuiLink` boundary; do not convert arguments to unordered JSON before serialization.

## Local Contracts

- Link `simulator::agent_protocol`, `simulator::logging_core`, and `Threads::Threads`; do not include or link simulator, app, UI, or engine modules. `logging_core` supplies `LOG_ERROR` for unexpected bridge call exceptions.
- Select protocol era independently for each request: legacy `initialize` establishes initialization-era support, while modern requests require their own protocol version and client capabilities metadata.
- Modern client information is optional and stateless; an omitted identity is empty for that request. Preserve the initialized legacy identity separately and restore it for later legacy requests after interleaved modern requests.
- If `_meta` is present, it must be an object; reject malformed metadata with `-32602` before dispatching any tool.
- `GuiLink` authenticates `gui_token` with `agentTokensEqual`; malformed reply envelopes or JSON-RPC error fields drop the channel and return `SIMULATOR_UNAVAILABLE`.
- Mapped GUI JSON-RPC tool errors preserve `data.code` and top-level `message` in the common result shape, using `epoch: null` when no epoch is available.
- `runBridge` scans stdio framing incrementally while preserving the 1 MiB limit, CRLF handling, and oversized-line discard through newline. Client identity fields and exception diagnostics are control-escaped in stderr. Unexpected caller exceptions are logged to stderr but return a generic `INTERNAL` result with no exception details exposed to MCP clients.

## Work Guidance

Keep MCP-specific behavior isolated here so protocol replacement does not affect simulator integration.

## Verification

- `ctest --test-dir build -R '^test_mcp_bridge$' --output-on-failure`

## Child DOX Index

No child docs.
