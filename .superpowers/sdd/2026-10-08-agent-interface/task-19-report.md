# Task 19 — `runBridge` and `rf-sim-mcp`

## Changed files

- `agent/mcp/include/mcp_bridge.h` — declared `BridgeOptions` and `runBridge`.
- `agent/mcp/src/mcp_bridge.cpp` — added the stdio bridge, separate reader/session/caller threads, ordered calls, cancellation suppression, per-call identity reconnects, incremental framing, escaped log fields, and generic caller-exception handling.
- `agent/mcp/src/mcp_bridge_detail.h` — provides the private generic `INTERNAL` result for unexpected bridge exceptions.
- `agent/mcp/src/main.cpp` — added CLI options, project-version output, executable-directory setup, and Windows binary stdin/stdout mode.
- `agent/mcp/CMakeLists.txt` — compiled the bridge into `mcp_adapter`; added the UI-/simulator-free executable, project version definition, and install rule.
- `agent/AGENTS.md` — documented bridge/CLI ownership and transport behavior.
- `tests/test_mcp_bridge.cpp` — added the bridge integration cases from the tests-only stage, plus oversized-line runtime/discard recovery, stderr control escaping, and generic `INTERNAL` result coverage.
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

## Fix round 1 — framing performance and stderr hardening

### Changes

- The reader still blocks through `in.get()`, but now calls `AgentLineReader::next()` only when it reaches a newline or has enough bytes to determine the 1 MiB limit was exceeded. Once oversized, it discards through newline without rescanning or retaining the discarded bytes. The regression checks both the bounded runtime and parsing a valid modern request after the oversized line.
- Client identity log fields escape backslashes, CR/LF/tab, and remaining C0/DEL bytes. A dedicated stderr regression verifies injected control characters stay on one log line.
- Both caller exception branches log diagnostics to stderr and return the private generic `{epoch: null, error: {code: INTERNAL, message: Internal bridge error.}}` result; exception text is never returned to the MCP client. There is no realistic deterministic exception trigger in `GuiLink` without adding an injection seam to the public bridge API, so coverage tests the private result helper and source review verifies both catch branches.

### TDD and verification

- `cmake --build build --target test_mcp_bridge` — passed after adding regressions, before production fixes.
- `cmd.exe /c "build\\bin\\test_mcp_bridge.exe [bridge] --durations yes"` against the old implementation — expected RED: 2 failures. The runtime guard failed at `18874123200 ns` (`18.875 s`, greater than the `10 s` bound); the identity-log assertions found an extra newline (second newline at byte 55). The reviewer’s earlier baseline measurement was `18.685 s`.
- `cmd.exe /c "build\\bin\\test_mcp_bridge.exe [bridge] --durations yes"` after fixes — passed: 9 cases, 109 assertions; oversized-line case `0.070 s`.
- `cmake --build build --target test_mcp_bridge rf-sim-mcp` — passed.
- `ctest --test-dir build -R '^test_mcp_bridge$' --output-on-failure` — passed: 1/1 tests, 0 failures (`1.38 s`).
- `sh scripts/format.sh --check agent/mcp/src/mcp_bridge.cpp agent/mcp/src/mcp_bridge_detail.h tests/test_mcp_bridge.cpp` — passed: all 3 files clean under clang-format 18.
- `git diff --check` — passed with no output.

The 1 MiB oversized-line case improved from `18.875 s` to `0.070 s` in the same test executable. The full suite was not run.

### DOX correction

- `tests/AGENTS.md` now inventories the oversized-line runtime/discard-recovery, control-safe stderr identity, and generic `INTERNAL` result cases.
- `agent/mcp/AGENTS.md` now records incremental stdio framing guarantees and the caller-exception/logging contract. `agent/AGENTS.md` also retains the bridge-level framing and stderr contract.
- Docs-only; no build or tests were rerun for this correction.

## Fix round 2 — logger boundary

### Changes

- Both unexpected caller exception branches now call `LOG_ERROR` with the tool name and diagnostic while retaining the escaped `err` stream diagnostic. Both still return the private generic `INTERNAL` result; exception text is not included in the MCP result.
- `agent/mcp/AGENTS.md` and the active approved spec now agree: `mcp_adapter` depends on `agent_protocol` and `Threads::Threads`, with private `logging_core`; `rf-sim-mcp` has no simulator engine/app dependency, and `logging_core` is diagnostics-only. The approved spec remains ignored and uncommitted.
- No realistic deterministic caller-exception test exists without an injection seam: `GuiLink` handles transport and JSON failure paths internally. The existing private-result helper test covers generic client-facing shape; source review verified both catch branches call `LOG_ERROR`, preserve stderr diagnostics, and return that helper.

### Verification

- `cmake --build build --target test_mcp_bridge rf-sim-mcp` — passed.
- `ctest --test-dir build -R '^test_mcp_bridge$' --output-on-failure` — passed: 1/1 tests, 0 failures (`1.77 s`).
- `sh scripts/format.sh --check agent/mcp/src/mcp_bridge.cpp` — passed: file clean under clang-format 18.
- `git diff --check` — passed with no output.
- Full suite not run.
