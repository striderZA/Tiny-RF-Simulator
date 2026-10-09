# MCP adapter — AGENTS.md

## Purpose

Provide the UI- and simulator-independent MCP session layer over the agent tool catalog.

## Ownership

- `simulator::mcp_adapter` owns JSON-RPC envelope handling, initialization-era negotiation for 2025-06-18/2025-11-25, stateless 2026-07-28 request metadata, tool discovery/call conversion, and cancellation ID handoff.
- `McpToolCall` carries ordered JSON arguments to the `agent_api` boundary; do not convert arguments to unordered JSON before API validation.
- The bridge adapter formats results and errors but does not execute simulator tools or own transport I/O.

## Local Contracts

- Link only `simulator::agent_protocol` and `Threads::Threads`; do not include simulator, app, or UI headers.
- Select protocol era independently for each request: legacy `initialize` establishes initialization-era support, while modern requests require their own protocol version and client capabilities metadata.
- Modern client information is optional and stateless; an omitted identity is empty for that request. Preserve the initialized legacy identity separately and restore it for later legacy requests after interleaved modern requests.
- If `_meta` is present, it must be an object; reject malformed metadata with `-32602` before dispatching any tool.

## Work Guidance

Keep MCP-specific behavior isolated here so protocol replacement does not affect simulator integration.

## Verification

- `ctest --test-dir build -R '^test_mcp_bridge$' --output-on-failure`

## Child DOX Index

No child docs.
