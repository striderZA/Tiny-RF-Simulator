# JSON Loader Hardening — Design

## Context

Issue #48 reports that syntactically valid JSON with unexpected types can escape the project and component-library loading boundaries. The current branch contains partial hardening from `e96af29`: project loading has an outer JSON-exception boundary, component deserialization is isolated, and component-library parsing catches JSON exceptions. Existing project and library tests pass, but library negative coverage is absent and typed extraction remains the first line of defense.

## Decisions

- Treat malformed JSON structure as untrusted input at every load/discovery boundary.
- Keep `{}` as a valid empty project.
- Reject malformed top-level project collections gracefully without allowing an exception to escape the loader.
- Skip malformed individual project components while preserving saved-index mapping for links, probes, and groups.
- Skip malformed component-library definitions, while retaining valid definitions discovered from other files.
- For an otherwise valid library definition, ignore malformed optional `data_files` entries rather than discarding the whole definition.
- Log malformed input with its source path and field context; do not expose exceptions to the UI frame loop.
- Do not introduce a generic schema framework for these two focused loaders.

## Design

### ProjectSerializer

Add explicit object/array/type checks around the project root collections and component records before typed extraction. Preserve the existing per-component `-1` mapping behavior when a component is skipped. Keep the full load body guarded as a final defense, catching standard exceptions at the load boundary and returning failure without propagating to `RfSimulatorApp`.

Top-level wrong-shaped sections fail the load gracefully after the project has been reset; malformed component records are isolated and do not prevent valid sibling components from loading. Existing optional-section defaults remain unchanged for absent fields.

### ComponentLibrary

Validate the parsed document and required fields before extracting strings, parameters, and optional values. A definition with invalid required fields is skipped. Optional scalar fields are accepted only when they have the expected type; malformed optional fields are ignored with a warning. `data_files` is processed only when it is an array; each entry must be an object with string `type` and `path`, otherwise that entry is skipped.

Make directory scanning resilient across the entire root check and recursive iteration. A failure for one file or subtree is logged and does not abort discovery of later valid files.

## Testing

Extend focused Catch2 coverage with:

1. Project JSON whose top-level collections and component fields have wrong types; verify no crash and valid sibling components remain available where applicable.
2. Component-library definitions with wrong-typed required fields; verify the definition is skipped.
3. Component-library definitions with wrong-typed optional fields and malformed `data_files` entries; verify the valid definition loads and malformed optional entries are ignored.
4. A scan directory containing malformed and valid JSON files; verify valid definitions are still discovered.

Run the existing project-file and component-library test filters, then build/run the modified test target directly as required by the local MinGW registration ceiling.

## Non-goals

- No changes to extension-manifest schema behavior, component-engine deserialization semantics, or UI error presentation.
- No generic JSON-schema dependency or validation framework.
- No unrelated filesystem or parser refactoring.

## Acceptance criteria

- No malformed-but-valid project or library JSON case can propagate a JSON/type exception into the application frame loop.
- Valid sibling project components and valid library files continue loading when another entry is malformed.
- New regression tests fail against the un-hardened behavior and pass after the fix.
- Existing focused project and library tests remain green.
