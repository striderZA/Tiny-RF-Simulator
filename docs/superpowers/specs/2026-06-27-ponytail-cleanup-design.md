# Ponytail Cleanup — Design Spec

**Date:** 2026-06-27
**Status:** Approved (pending review of this document)
**Origin:** `ponytail-audit` (whole-repo over-engineering scan)
**Goal:** Address 13 over-engineering findings from the audit, in 4 sequenced installments. No behavior changes.

---

## 1. Goal

Remove ~1,330 lines of dead code, redundant abstractions, and unneeded boilerplate identified by `ponytail-audit`. Each installment is independently buildable, testable, and revertible. No new features, no API renames visible to users — internal simplifications only.

## 2. Scope

### In scope (4 installments)

1. **Installment 1 — Low-hanging fruit (pure deletions)**
2. **Installment 2 — Foundation cleanup (caching triad, IComponentEngine, ComponentRegistry)**
3. **Installment 3 — Housekeeping (SessionState inline, nonlinear_model .cpp split, group infra trim)**
4. **Installment 4 — InspectorPanel collapse (9 per-type draw methods → 1 generic loop)**

### Out of scope

- Correctness, security, or performance bugs.
- Renaming public APIs.
- Adding new functionality.
- Replacing dependencies (every dep is actually used for its core purpose — imgui, implot, glfw, kissfft, stb).

## 3. Sequencing rationale

The installments are ordered by **risk** (lowest first) and **revertibility** (each is one git commit). Lower-numbered installments do not require any other installment to land. Higher-numbered installments may touch slightly more code but each is still self-contained.

| # | Installment | Approx. lines saved | Files touched | Risk |
|---|---|---|---|---|
| 1 | Low-hanging fruit | ~480 | ~10 (mostly deletions) | None — pure deletions of dead/redundant code |
| 2 | Foundation cleanup | ~160 | ~20 (all engine .h/.cpp + app) | Low-medium — touches every engine but mechanically |
| 3 | Housekeeping | ~160 | ~6 | Low-medium — small interface changes, faster compiles |
| 4 | InspectorPanel collapse | ~300 | ~10 (panel + 8 engine headers) | Medium-high — largest diff, manual UI verification |

**Estimated total:** ~1,100–1,330 lines removed (the audit's 1,330 estimate; conservative end of the range is what we'll measure against).

## 4. Installment 1: Low-hanging fruit

### Changes

- **IconRegistry → header-only free function**
  - Delete: `icon_registry/src/icon_registry.cpp`, `icon_registry/src/texture_loader.h`, `icon_registry/src/texture_loader.cpp`
  - Collapse: `icon_registry/include/icon_registry.h` → ~15 lines: `static ImTextureID loadPNG(const char*)`, `static void freePNG(ImTextureID)`, plus a `std::unordered_map<std::string, ImTextureID>` used directly in `NodeGraphWidget` (the only consumer).
  - Update `node_graph_widget.cpp` to call `loadPNG()` / `freePNG()` directly.

- **RfSimulatorCore → free function**
  - Delete: `core/include/core.h`, `core/src/core.cpp`
  - Move the GLFW/ImGui main loop into `src/main.cpp` as `static bool runSimulator(const std::function<void()>& onGui)`. Drop the `RfSimulatorCore` class entirely.

- **coax_presets trim**
  - Drop all 5 zero-filled entries (MT 210/230/265/300/480) from `kCoaxCablePresets`. Keep only MT 340 (now at index 0). The other cables will be re-added when their datasheets are sourced.
  - Update the default `m_preset_index = 0` in `CoaxCableEngine`.
  - Update `tests/test_coax_cable_presets.cpp` to assert 1 entry.

- **SpectrumAnalyzerEngine cache → deleted**
  - Delete the 5-line `mutable` RBW cache (`m_cache_spectrum`, `m_cache_spec_gen`, `m_cache_rbw`, `m_cache_bin_width`, `m_cache_rbw_power_W`) in `spectrum_analyzer_engine.h` and `spectrum_analyzer_engine.cpp`. Re-run `applyRBW` on every `renderSpectrum` call. 1024 bins is trivial.

- **utils.h double-clamp removed**
  - In `core/include/utils.h`, delete the "clamp external writes" blocks (BEFORE the widget call) from both `inputDouble` and `inputFrequency`. Keep the post-widget clamp; that one guards user input.
  - The `LOG_WARN` calls in the deleted block were unreachable from the only callers (engines set these values via guarded setters, no other writes).

- **ViewManager `nodes_span()` deleted**
  - Drop the `nodes_span()` method. Only `nodes()` is called.

### Files touched

- `icon_registry/include/icon_registry.h` (rewritten)
- `node_graph/src/node_graph_widget.cpp` (call sites)
- `node_graph/include/node_graph_widget.h` (member type change if `IconRegistry` is removed)
- `src/main.cpp` (gains the `runSimulator` function)
- `core/include/core.h`, `core/src/core.cpp` (deleted)
- `coax/include/coax_presets.h` (trimmed)
- `coax/src/coax_cable_engine.cpp` (default index)
- `tests/test_coax_cable_presets.cpp` (asserts 1 entry)
- `spectrum_analyzer/include/spectrum_analyzer_engine.h` (cache removed)
- `spectrum_analyzer/src/spectrum_analyzer_engine.cpp` (cache removed)
- `core/include/utils.h` (clamp removed)
- `common/view_manager.h` (accessor removed)
- `app/src/app.cpp` (if `nodes_span()` is used)
- `app/src/component_registry.cpp` (if `nodes_span()` is used)
- `core/CMakeLists.txt` (delete target)
- `icon_registry/CMakeLists.txt` (delete or rewrite)
- Top-level `CMakeLists.txt` (remove `add_subdirectory(core)`)
- `tests/CMakeLists.txt` (remove `core` from test linkage)

### Verification

- `cmake --build build` succeeds
- `ctest --test-dir build --output-on-failure` passes (73 tests)
- Manual: app starts, shows the same window layout, coax preset dropdown shows only MT 340

## 5. Installment 2: Foundation cleanup

### Changes

- **Caching triad → `CachedInput` helper in IComponentEngine**
  - In `common/component_interface.h`, add a small `struct CachedInput { const Spectrum* ptr = nullptr; uint64_t generation = 0; };` as a protected member of the engine base (or as a free struct engines compose).
  - Each engine's `update()` becomes: `if (m_cache.ptr == input && m_cache.generation == input->generation && !m_dirty) return;`
  - Delete the 3-line triad from every engine's header. Saves 3 lines × 8 engines = 24 lines plus the duplicated pattern.

- **IComponentEngine trim**
  - Drop the `inputPinId(int port)` and `outputPinId(int port)` overloads (both default to forwarding to port 0). Only `SParamEngine` actually overrides these, and it can own the multi-pin logic locally.
  - **Keep** the `numInputPins() / numOutputPins()` defaults (returning 1). These are still called from `app/src/app.cpp` line 99 (`int N = comp->numInputPins();`) and providing them in 7 engines is more boilerplate than the defaults themselves.
  - After the trim, the interface has single-pin single-output only. Multi-pin support lives in `SParamEngine` as a local concern, not in the base interface.

- **ComponentRegistry simplification**
  - Drop `m_type_index` and `byType<T>()`.
  - Replace with a tiny `getPFBs()` method that filters the flat vector: `std::vector<PFBChannelizerEngine*> getPFBs() const` (or just an iteration in the caller).
  - Drop `m_all_view` (rebuild it lazily or iterate `m_components` directly via the `unique_ptr` vector).
  - The template `add<T, Args...>` is fine; no change there.

### Files touched

- `common/component_interface.h` (gains `CachedInput`, loses multi-pin defaults)
- Every engine `.h` and `.cpp` (8 engines — `s_param` keeps its multi-pin API, others delete the 3 caching fields)
- `app/include/component_registry.h` (drops `m_type_index`, `byType`)
- `app/src/component_registry.cpp` (drops the rebuild)
- `app/src/app.cpp` (the two `byType<PFB>` callsites switch to `getPFBs()`)

### Verification

- `cmake --build build` succeeds
- `ctest --test-dir build --output-on-failure` passes
- Manual: app starts, PFB widgets show spectrum, drag any component, verify DSP updates.

## 6. Installment 3: Housekeeping

### Changes

- **SessionState → inline in app**
  - Delete `common/session_state.h` and the Win32 INI machinery.
  - In `app/include/app.h`, declare 4 bool members that need persistence (`m_show_log`, `m_show_spectrum`, `m_show_properties`, `m_show_node_editor`).
  - In `app/src/app.cpp`, replace the `SessionState::loadBool()` / `saveBool()` calls with a small inline helper that reads/writes a single key from a plain text file. ~15 lines.
  - The per-PFB IQ / grid window booleans (currently saved by id) are still saved — use the same inline helper.
  - Delete `tests/test_session_state.cpp` (SessionState no longer exists).

- **nonlinear_model.h → .cpp split**
  - Move `process()`, `recomputeCoefficients()`, and the `detail::*` math helpers from `nonlinear_model.h` to a new `common/nonlinear_model.cpp`.
  - Leave the class declaration, the inline getter/setter stubs, and the `Result` struct in the header.
  - Update `common/CMakeLists.txt` to compile the new `.cpp`.
  - Recompilation savings: amplifier, coax, s_param no longer pull the math in every rebuild.

- **Group infrastructure trim**
  - Drop the cascade in `NodeGraphEngine::removeNode` that auto-removes groups with `<2` members and rebuilds boundary pins for all surviving groups.
  - In `NodeGraphEngine::addGroup`, do not auto-trigger `rebuildGroupBoundaryPins()` — let the widget pull it lazily.
  - In `setGroupCollapsed`, do not clear `boundary_pins` on expand (the comment about "stale synthetic pins" can be handled by always checking membership before rendering).

### Files touched

- `common/session_state.h` (deleted)
- `app/include/app.h` (gains 4 bools)
- `app/src/app.cpp` (gains the inline persistence helper, drops `SessionState` includes)
- `app/include/component_registry.h` (no SessionState references — verify)
- `tests/test_session_state.cpp` (deleted)
- `tests/CMakeLists.txt` (drops the test file)
- `common/nonlinear_model.h` (becomes a slimmer header)
- `common/nonlinear_model.cpp` (new)
- `common/CMakeLists.txt` (compiles the new .cpp)
- `node_graph/src/node_graph_engine.cpp` (group trim)
- `node_graph/include/node_graph_engine.h` (if signatures change)

### Verification

- `cmake --build build` succeeds
- `ctest --test-dir build --output-on-failure` passes (72 tests now — session state test removed)
- Manual: app starts, window positions and visibility state remember across restart (this was the only thing SessionState was used for).

## 7. Installment 4: InspectorPanel collapse

### Approach

Replace the 9 per-type `drawXxxProperties()` methods (each ~40-70 lines of ImGui widgets) with a single generic loop driven by a parameter descriptor per engine.

### Changes

- **Property descriptor**
  - Add a small `struct PropertyDesc` in `common/component_interface.h` (or a new `common/property_grid.h`):
    ```cpp
    struct PropertyDesc {
        const char* label;
        enum class Kind { Double, Int, Bool, Enum } kind;
        union {
            struct { double* ptr; double min; double max; const char* fmt; } dbl;
            struct { int* ptr; int min; int max; } integer;
            struct { bool* ptr; } boolean;
            struct { int* ptr; const char* const* items; int n; } choice;
        };
    };
    ```
    And a free function: `void drawProperty(const PropertyDesc& d);`

- **Each engine exposes its properties**
  - In each engine `.h`, add a `std::vector<PropertyDesc> properties() const;` method that returns the descriptor list.
  - Example for amplifier: gain, NF, OIP2 (if nonlinear enabled), OIP3 (if nonlinear enabled).
  - The 9 `drawXxxProperties()` methods in `inspector_panel.cpp` collapse to one `drawPropertyList(PropertyDesc&)` call.

- **Special-case widgets** (PFB channel selector, coax preset dropdown, s-param file path) become `Kind::Enum` or get a small extension (`Kind::Path`, etc.).

### Files touched

- `common/component_interface.h` (or new `common/property_grid.h` with `PropertyDesc` + `drawProperty`)
- Each engine `.h` (8 engines — add `properties()` method, ~10 lines each)
- `app/include/inspector_panel.h` (loses 9 method declarations, gains 1)
- `app/src/inspector_panel.cpp` (loses ~500 lines of per-type methods, gains ~30 lines of generic loop + dispatch)

### Verification

- `cmake --build build` succeeds
- `ctest --test-dir build --output-on-failure` passes
- Manual visual confirmation that each engine's property panel still shows all the same controls (gain slider, NF slider, OIP2/OIP3, Lo freq, conv gain, length, preset, etc.). Best to compare side-by-side with the pre-cleanup version (use a git stash before starting, then re-run the app and compare).

## 8. Risk register

| Risk | Mitigation |
|---|---|
| Installment 2 breaks engine dirty-flag logic | Existing DSP tests (`test_adc`, `test_amplifier`, etc.) exercise the `update()` path. Run `ctest` after. |
| Installment 3 SessionState deletion breaks window persistence | Manual verification across an app restart. |
| Installment 4 InspectorPanel looks different | Side-by-side visual check before/after. Take a screenshot pre-cleanup. |
| nonlinear_model.cpp split causes link error | CMake handles the new `.cpp` automatically. If a `inline` was needed for header-only behavior, it can move to a free function. |

## 9. Per-installment workflow

For each installment, the implementation flow is:

1. Branch from `master`: `refactor/ponytail-cleanup-N-<short-name>`
2. Apply the changes in this spec.
3. `clang-format -i <changed files>`.
4. `cmake --build build` — must succeed.
5. `ctest --test-dir build --output-on-failure` — must pass.
6. Manual verification per installment.
7. `git commit -m "refactor: <installment name>"`
8. (Optional) Open PR for review.

## 10. Out-of-scope follow-ups (deferred)

- Replacing `kissfft` with `std::valarray` or built-in FFT — not a simplification, would change correctness.
- Removing `imnodes` in favor of `ImGui::Begin`/`End` windowing — different abstraction, not a simplification.
- Consolidating the per-engine `addTone/removeTone/updateTone` triplet (only used by signal_generator) — too narrow, would just move code.
- Replacing `GetPrivateProfileStringA` with a real config library — moot once SessionState is gone.

## 11. Success criteria

- All 4 installments landed on `master` (or PRs merged).
- `ctest --test-dir build --output-on-failure` passes after each installment.
- `wc -l` across the source tree drops by ≥1,100 lines (target: 1,330).
- No behavior change verified by manual smoke test (app starts, components render, DSP works, window state remembers).
- No new dependencies added, none removed.
