# Files

- [Agent Tool-Call Lifecycle](agent-tool-calls.md) - End-to-end trace of one agent tool call through MCP JSON-RPC framing, the GuiLink relay, the GUI AgentServer inbox, the UI-thread pump with modal parking, AgentApi dispatch, circuit edits and checkpoints, epoch-aware results, and reply size limits. Use it to debug a call that stalls, returns BUSY or STALE_EPOCH, or fails with INTERNAL.
- [DSP Pipeline & Runtime Workflows](dsp-pipeline.md) - Follow real-time signals through CircuitRuntime rewiring and topological updates, ADC DDC, PFB channel outputs, analyzer measurements, component-ID and project-epoch guards, and revision-based project persistence.
- [Test Flow Workflow](test-flow.md) - Defines the JSON Test Flow sweep contract and traces authoring, preflight, the shared RunFlowWithinBoundary seam used by the panel and the test_flow_run agent tool, once-per-run restoration, failure latching, and JSON results.
