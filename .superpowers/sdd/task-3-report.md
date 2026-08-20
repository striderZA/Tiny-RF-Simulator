# Task 3 Report: Add explicit project-shape checks and final exception defense

## Status

COMPLETE — commit `2d2dd55` (`fix: validate malformed project JSON`), branch
`fix/issue-48-json-loader-hardening`, working tree clean.

## Files

- Modified: `app/src/project_serializer.cpp` (`ProjectSerializer::load()`) — the
  only file touched (190 insertions, 37 deletions).

## What changed

1. **Top-level shape validation (brief Step 1).** Before `reset()`, optional
   `components` / `links` / `probe_pins` / `groups` (arrays) and
   `network_analyzer` / `window_state` / `graph_state` (objects) are validated
   when present and non-null. A wrong shape is logged per field (`LOG_ERROR`)
   and the load returns `false`; missing fields and explicit `null` retain
   defaults. On the failure path the state is reset to empty before returning
   `false` (see Concern 1) and the check runs inside the guarded `try`, so even
   `reset()` exceptions are contained by the broadened catch.
2. **Component record validation (Step 2).** Each component entry must be an
   object with a string `type`; if `params` is present it must be an object.
   Malformed records are logged with their saved index (`LOG_WARN`) and skipped
   with `-1` pushed into `new_node_ids`, preserving the saved-index → node
   mapping and valid siblings. Absent `params` still resolves to JSON null
   (engine defaults). `pos.x`/`pos.y` are read only when `pos` is an object and
   the fields are numeric; `part_number` only when a string — malformed optional
   metadata no longer aborts a valid component.
3. **Per-entry isolation for links, probes, network analyzer, groups (Step 3).**
   Each entry must be an object; integer fields are checked with
   `is_number_integer()` and booleans with `is_boolean()` before any
   `get<T>()`; malformed entries are logged (`LOG_WARN`) and skipped. NA sweep
   params are read via a numeric guard with "keep current value" fallback, and
   Point A/B restore validates comp/port/is_output the same way. Saved-index
   bounds checks and the `new_node_ids` mapping are unchanged, so one bad
   link/group/probe can no longer discard valid components or later sections.
4. **Broadened final catch (Step 4).** The parse-only catch around `in >> root`
   stays `nlohmann::json::exception`; the outer restoration catch is now
   `catch (const std::exception &e)` — non-JSON restoration exceptions become a
   logged `false` result. Deliberately not `catch (...)`.

## Test output

```
$ cmake --build build --target test_issue48_json_loader
[1/3] Building CXX object app/CMakeFiles/app.dir/src/project_serializer.cpp.obj
[2/3] Linking CXX static library lib\libapp.a
[3/3] Linking CXX executable bin\test_issue48_json_loader.exe

$ build/bin/test_issue48_json_loader.exe "[project]"
Filters: [project]
Randomness seeded to: 206102665
===============================================================================
All tests passed (8 assertions in 2 test cases)
```

Both project cases pass: malformed component (`{"type": 42}`) and malformed
link (`{"from": "bad", ...}`) entries are skipped while the valid
SignalGenerator/Amplifier siblings load (`componentCount() == 2`), and
wrong-shaped top-level sections (`{"components": 5, "links": {}, "groups": 7}`)
are rejected without throwing (`componentCount() == 0`). Only the focused
`[project]` filter was run, per assignment. Pre-commit clang-format-18 gate
passes (one whitespace-only collapse applied and re-verified).

## Self-review

- Shape predicates (`is_array`/`is_object`/`is_string`/`is_number`/
  `is_number_integer`/`is_boolean`) are checked before every typed
  `get<T>()`/`value<T>()` on untrusted data; no new JSON-schema dependency.
- `{}` remains a valid empty project (all sections absent → defaults, load
  succeeds with zero components).
- The `new_node_ids` mapping is preserved exactly: malformed components push
  `-1`, links/probes/NA points/groups resolve through the same mapping with
  unchanged bounds checks.
- `LOG_WARN` for skipped individual entries, `LOG_ERROR` for fatal shape
  errors; 4-space LLVM formatting, verified by the clang-format-18 hook.
- No unrelated parser/refactor changes; save path untouched.
- Regression risk to existing `[project_file]`/`[library]` suites: low — all
  guarded fields preserve the exact default fallbacks the old `.value()` calls
  produced for missing keys, and valid save files contain only well-typed
  values.

## Concerns

1. **Shape-failure state semantics.** The brief says validate "before
   `reset()`" and return `false`; it does not state what state the app should
   be left in. The committed regression test
   (`"rejects wrong-shaped top-level sections without throwing"`) asserts
   `componentCount() == 0`, and a fresh `RfSimulatorApp` constructor seeds 2
   default components — so a plain early return would leave 2 and fail the
   test. The failure path therefore calls `reset()` (empty state, matching the
   pre-hardening reset-first behavior) before returning `false`. Net effect vs.
   the old code: same resulting state, but the return value is now correctly
   `false` instead of `true`, and the check happens before any restoration
   begins. A real user with an open project loading a malformed file still
   loses that project (same as before this hardening) — flagging in case the
   desired semantics were "preserve current project on failed load"; that
   would require updating the test, which was out of scope.
2. Probes restore through `m_components.all()[comp_idx]` (registry order)
   while links/NA use `new_node_ids` — pre-existing inconsistency, left
   unchanged per "keep bounds checks unchanged".
3. `window_state`/`graph_state` inner booleans are not individually guarded;
   Step 1 guarantees object shape and a wrong-typed inner value fails cleanly
   via the broadened catch (logged `false`), which matches the brief's Step 3
   scope (links/probes/NA/groups only).

## Report path

`.superpowers/sdd/task-3-report.md`

---

# Task 3 Review-Fix Pass: checked integers, boolean guards, integer points

## Status

COMPLETE — this review-fix commit sits on top of `2d2dd55` (`fix: validate malformed project JSON`),
branch `fix/issue-48-json-loader-hardening`, working tree clean. Addresses every Task 3 reviewer finding on top of `2d2dd55`.

## Files

- Modified: `app/src/project_serializer.cpp` — checked-int helper + call-site hardening.
- Modified: `tests/test_issue48_json_loader.cpp` — two new `[issue48][project]` regression cases.

## What changed

1. **Checked-int helper (`checkedJsonInt`).** Added a file-local helper in the anonymous
   namespace (uses `is_number_unsigned()`/`is_number_integer()` and `get<std::uint64_t>()`/
   `get<std::int64_t>()` against `std::numeric_limits<int>` from `<limits>`/`<cstdint>`).
   `is_number_integer()` alone is not enough: a `number_unsigned` above `INT_MAX` (or a
   `number_integer` below `INT_MIN`) passed the old guard but silently truncated/wrapped in
   `get<int>()`. The helper returns `std::optional<int>`, nullopt for non-integers and
   out-of-int-range values; callers log and keep their per-entry skip semantics.
2. **Applied to every `get<int>()` site** on untrusted fields: link `from`/`to`/`from_port`/
   `to_port`, probe `comp`/`port`, NA point `comp`/`port`, and group `member_components`
   members. Out-of-int-range values now mark the entry malformed (log + skip), exactly like
   the pre-existing non-integer path — valid siblings still restore.
3. **`is_output` boolean validation before `get<bool>()`** in both probe restoration and NA
   point restoration. The old code set `malformed = true` but still evaluated
   `pj["is_output"].get<bool>()` when the key was present, so a non-boolean `is_output`
   (e.g. `"yes"` or `1`) threw `nlohmann::json::type_error` 302 and aborted the whole load
   instead of skipping the entry. `get<bool>()` is now only reached when `is_boolean()` is
   true; absent key still defaults to `true`.
4. **`network_analyzer.points` requires an in-range integer.** Replaced the
   `static_cast<int>(number_field(...))` truncation path: fractional values (e.g. `12.5`)
   and out-of-int-range integers now log a warning and leave the engine's current value
   untouched. In-range integers still flow to `setPoints()` (which keeps its own [2, 2001]
   clamp).
5. Unchanged: defaults for missing keys, per-entry log-and-skip semantics, `new_node_ids`
   mapping/bounds checks, the broadened `catch (const std::exception &e)` (no `catch (...)`),
   `window_state`/`graph_state` behavior (documented out of scope in the prior report).

## New regression tests (`[issue48][project]`)

- `"Project loader skips malformed probe, network analyzer point, and group entries"` —
  non-boolean `is_output` on a probe and on NA Point A must skip those entries (old code
  threw and discarded the whole project); valid probe, NA Point B, `points: 51`, and a
  `GOOD-GROUP` (with non-boolean `collapsed` sibling skipped) still restore.
- `"Project loader rejects fractional and oversized integer fields"` — unsigned `2^32` and
  signed `INT_MIN-1` link indices must not wrap into bogus links, oversized probe port must
  not truncate to port 0, fractional `points: 12.5` keeps the current value (201), and one
  out-of-range group member marks the group malformed (skipped).

## Commands and output

`cmake --build build --target test_issue48_json_loader`

```
[1/4] Building CXX object tests/CMakeFiles/test_issue48_json_loader.dir/test_issue48_json_loader.cpp.obj
[2/4] Building CXX object app/CMakeFiles/app.dir/src/project_serializer.cpp.obj
[3/4] Linking CXX static library lib\libapp.a
[4/4] Linking CXX executable bin\test_issue48_json_loader.exe
```

`build/bin/test_issue48_json_loader.exe "[project]"`

```
Filters: [project]
Randomness seeded to: 3957655383
===============================================================================
All tests passed (30 assertions in 4 test cases)
```

Full standalone binary sanity check: all 8 test cases / 51 assertions passed. Project-wide
suites skipped per assignment.

## Concerns

- None. The fractional-points policy is strict (JSON-integer-typed only); a hand-edited
  `"points": 201.0` is treated as malformed and keeps the current value, matching "require
  integer points rather than fractional values".
- Out-of-int-range group members mark the whole group entry malformed (skip), consistent
  with the pre-existing non-integer-member behavior — a deliberate choice, not a silent
  partial-load.
