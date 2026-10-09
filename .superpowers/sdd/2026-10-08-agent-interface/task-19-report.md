# Task 19 — `runBridge` and `rf-sim-mcp`

## Changed files

- `agent/mcp/include/mcp_bridge.h` — declared `BridgeOptions` and `runBridge`.
- `agent/mcp/src/mcp_bridge.cpp` — added the stdio bridge, separate reader/session/caller threads, ordered call queue, cancellation reply suppression, and per-call GUI identity reconnection.
- `agent/mcp/src/main.cpp` — added CLI options, project-version output, executable-directory setup, and Windows binary stdin/stdout mode.
- `agent/mcp/CMakeLists.txt` — compiled the bridge into `mcp_adapter`; added the UI-/simulator-free executable, project version definition, and install rule.
- `agent/AGENTS.md` — documented bridge/CLI ownership and transport behavior.
- `tests/test_mcp_bridge.cpp` — included the six `[bridge]` integration cases and per-request identity regression from the tests-only stage; fixed the Catch2 `||` assertion syntax and formatted the changed test source without changing assertion behavior.
- `tests/AGENTS.md` — updated the standalone bridge-test inventory.

## Design choices

- `AgentLineReader` runs on the reader thread and reports oversized input through `McpSession::onOversizedLine()`. All session mutation, immediate reply formatting, cancellation bookkeeping, and completion reply formatting remain on the session thread.
- A single caller thread consumes calls in input order. Each queued task owns the `McpSession::clientInfo()` snapshot; a changed `(name, version)` pair replaces `GuiLink`, including a transition to empty modern identity. Cancellation marks the pending request and suppresses its reply only; the GUI call is still completed.
- All MCP replies pass through one serialized writer and are flushed per message. The reader's EOF event closes call intake, after which the caller drains queued calls and the session waits for their results before returning.
- `--endpoint` is passed through unchanged. Without an override, the bridge uses `defaultAgentEndpointDirectory()`, `agentInstallKey(exe_dir)`, and `agentEndpointFileName()`. `tools/list` is processed by the adapter without requiring GUI availability.
- The CLI supports `--endpoint`, `--version`, and `--help`; unknown/missing arguments exit 2. The executable links only `simulator::mcp_adapter` and its existing protocol/thread dependencies.

## Verification

- `cmake --build build --target test_mcp_bridge rf-sim-mcp` — passed.
- `ctest --test-dir build -R '^test_mcp_bridge$' --output-on-failure` — passed: 1/1 tests, 0 failures.
- `cmd.exe /c "build\bin\rf-sim-mcp.exe --version"` — printed `rf-sim-mcp 0.27.0`.
- `sh scripts/format.sh --check agent/mcp/include/mcp_bridge.h agent/mcp/src/mcp_bridge.cpp agent/mcp/src/main.cpp tests/test_mcp_bridge.cpp` — passed: all 4 files clean under clang-format 18.
- `git diff --check` — passed with no output.

The full test suite was not run, as requested.

## Remaining concerns

- No known Task 19 behavior issue remains. Verification was focused on `test_mcp_bridge`; the full suite and a POSIX build were not run.
- When default endpoint-directory derivation fails, GUI calls receive the normal unavailable result; local MCP discovery/listing remains independent of this failure.

## Implementation commit

`2b85a2942ef322274345bd304bd59b074dfd43ab` — `feat(agent): add the rf-sim-mcp stdio bridge`
