# Ponytail Cleanup 2 — Foundation Cleanup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Consolidate the duplicated `m_cached_input_ptr` + `m_cached_input_generation` + `m_dirty` triad shared by 8 engines into a single helper struct on `IComponentEngine`. Trim the unused multi-pin overloads from `IComponentEngine`. Simplify `ComponentRegistry` by removing the `type_index` machinery (only `byType<PFB>()` was used). Net ~160 lines saved, no behavior changes.

**Architecture:** The engines currently each declare 3 fields with the same pattern (`m_cached_input_ptr`, `m_cached_input_generation`, `m_dirty`) and `update()` reads/writes them identically. After this plan, the engine base holds a single `CachedInput cache;` and provides a helper method `isCacheValid(const Spectrum* input)` that all engines use. The `IComponentEngine` interface loses its multi-pin port overloads (only `SParamEngine` overrides them; let it own the logic). `ComponentRegistry` loses `m_type_index` and `byType<T>()`; replaces them with a `getPFBs()` accessor that filters the flat vector.

**Tech Stack:** C++20, Catch2 v3.4.0. No new dependencies.

**Specification:** `docs/superpowers/specs/2026-06-27-ponytail-cleanup-design.md` §5.

## Global Constraints

- **C++20** standard. No new language features.
- **Code style**: 4-space indent, 100-col width, `PointerAlignment: Right` per `.clang-format`. Run `clang-format -i <file>` before committing.
- **No behavior changes** — all existing tests must pass after every task.
- **One commit per task** — independently revertible.
- **Branch from `master`**: `refactor/ponytail-cleanup-02-foundation`.

## File Structure

```
common/component_interface.h                # modified — add CachedInput, drop multi-pin overloads
common/include/group.h                      # (no change)
common/signal_node.h                        # (no change)

amplifier/include/amplifier_engine.h        # modified — replace 3 fields with CachedInput
amplifier/src/amplifier_engine.cpp          # modified — use cache.isValid()
adc/include/adc_engine.h                    # modified — replace 3 fields with CachedInput
adc/src/adc_engine.cpp                      # modified
coax/include/coax_cable_engine.h            # modified
coax/src/coax_cable_engine.cpp              # modified
mixer/include/mixer_engine.h                # modified
mixer/src/mixer_engine.cpp                  # modified
splitter/include/splitter_engine.h          # modified
splitter/src/splitter_engine.cpp            # modified
ideal_filter/include/ideal_filter_engine.h  # modified
ideal_filter/src/ideal_filter_engine.cpp    # modified
pfb_channelizer/include/pfb_channelizer_engine.h  # modified
pfb_channelizer/src/pfb_channelizer_engine.cpp    # modified
s_parametric_component/include/s_param_engine.h   # modified (multi-pin API stays)
s_parametric_component/src/s_param_engine.cpp     # modified
signal_generator/include/signal_generator_engine.h # modified (no input pin → no cache needed)

app/include/component_registry.h            # modified — drop m_type_index, byType
app/src/component_registry.cpp              # modified
app/src/app.cpp                             # modified — byType<PFB>() → getPFBs()
```

---

## Task 1: Add `CachedInput` to IComponentEngine

**Files:**
- Modify: `common/component_interface.h`

**Interfaces:**
- Consumes: `<cstdint>` (for `uint64_t`).
- Produces: `struct CachedInput { const Spectrum* ptr = nullptr; uint64_t generation = 0; bool dirty = true; }` as a protected nested type of `IComponentEngine`. Each engine member becomes `CachedInput cache;` instead of 3 separate fields.

- [ ] **Step 1: Add `CachedInput` struct to the header**

In `common/component_interface.h`, after the existing interface methods, add a protected section:

```cpp
protected:
    struct CachedInput {
        const Spectrum* ptr = nullptr;
        uint64_t generation = 0;
        bool dirty = true;
    };
    CachedInput cache;
```

Add `#include <cstdint>` if not already present.

- [ ] **Step 2: Add a `isCacheValid` helper method**

In the public section of `IComponentEngine`, add:

```cpp
bool isCacheValid(const Spectrum* input) const {
    return !cache.dirty && input != nullptr
        && cache.ptr == input
        && cache.generation == input->generation;
}
```

This is what each engine's `update()` will check before recomputing.

- [ ] **Step 3: Build to verify the helper compiles in isolation**

```bash
cmake --build build
```

Expected: BUILD SUCCEEDED. (No engine uses `CachedInput` yet, but the struct should compile cleanly.)

- [ ] **Step 4: Commit**

```bash
git add common/component_interface.h
git commit -m "refactor: add CachedInput helper to IComponentEngine"
```

---

## Task 2: Drop multi-pin overloads from IComponentEngine

**Files:**
- Modify: `common/component_interface.h`

**Interfaces:**
- Consumes: existing call sites for `inputPinId(int port)` / `outputPinId(int port)`.
- Produces: a single-pin, single-output interface. `SParamEngine` keeps its own multi-pin API.

- [ ] **Step 1: Find all multi-pin overload callers**

```bash
grep -rn "inputPinId([0-9]\|outputPinId([0-9]\|numInputPins\|numOutputPins" .
```

Expected: only `s_param_engine.cpp` (multi-pin) and `app.cpp` (`numInputPins()` loop). The `app.cpp` call stays — `numInputPins()` default is kept. `s_param_engine.cpp` will keep its own multi-pin methods.

- [ ] **Step 2: Delete the port overloads from IComponentEngine**

In `common/component_interface.h`, remove:

```cpp
virtual int inputPinId(int port) const { return port == 0 ? inputPinId() : -1; }
virtual int outputPinId(int /*port*/) const { return -1; }
```

The base class retains:
```cpp
virtual int numInputPins() const { return 1; }
virtual int numOutputPins() const { return 1; }
```

(`numInputPins` defaults are kept because `app.cpp:99` calls them and providing them in 7 engines would be more boilerplate than the one-line defaults.)

- [ ] **Step 3: Verify `SParamEngine` still compiles**

The s_param engine's `inputPinId(int port)` and `outputPinId(int port)` overrides are still valid — they're declared in `s_param_engine.h` and override the now-removed base methods. C++ allows declaring a method in a derived class that doesn't exist in the base. **But** the `s_param_engine.h` must NOT use `override` for the multi-pin versions now that they're not virtual on the base. Change `override` to a non-virtual declaration OR add the multi-pin methods as virtual on a `SParamEngine` base class. Recommended: declare them as virtual on `SParamEngine` directly:

```cpp
// In s_param_engine.h
int inputPinId(int port) const;
int outputPinId(int port) const;
int numInputPins() const override;
int numOutputPins() const override;
```

Wait — `inputPinId()` (no arg) still overrides the base. The multi-pin versions are now SParamEngine-specific. Verify this works by building.

- [ ] **Step 4: Build to verify**

```bash
cmake --build build
```

Expected: BUILD SUCCEEDED. If `s_param_engine.h` complains about a missing virtual base method, check that the `inputPinId()` (no arg) override is still there.

- [ ] **Step 5: Run tests**

```bash
ctest --test-dir build --output-on-failure
```

Expected: 73 tests pass.

- [ ] **Step 6: Commit**

```bash
git add common/component_interface.h s_parametric_component/
git commit -m "refactor: drop multi-pin port overloads from IComponentEngine"
```

---

## Task 3: Convert AmplifierEngine to use CachedInput

**Files:**
- Modify: `amplifier/include/amplifier_engine.h`
- Modify: `amplifier/src/amplifier_engine.cpp`

**Interfaces:**
- Consumes: `IComponentEngine::isCacheValid()`.
- Produces: same `update()` behavior with 2-line savings in the header.

- [ ] **Step 1: Replace the 3 fields in the header**

In `amplifier_engine.h`, delete:

```cpp
const Spectrum* m_cached_input_ptr = nullptr;
uint64_t m_cached_input_generation = 0;
bool m_dirty = true;
```

The `m_dirty` flag moves into the new `cache.dirty`. But because the parent `IComponentEngine` now has a protected `CachedInput cache;`, we don't need to redeclare anything.

- [ ] **Step 2: Update `update()` to use the new helper**

In `amplifier_engine.cpp`, the `update()` method currently looks roughly like:

```cpp
const Spectrum* input = m_node.inputs[0];
if (input == m_cached_input_ptr && input && input->generation == m_cached_input_generation && !m_dirty) {
    return;
}
// ... process ...
m_cached_input_ptr = input;
m_cached_input_generation = input ? input->generation : 0;
m_dirty = false;
```

Replace with:

```cpp
const Spectrum* input = m_node.inputs[0];
if (isCacheValid(input)) return;
// ... process ...
cache.ptr = input;
cache.generation = input ? input->generation : 0;
cache.dirty = false;
```

And the setters that flip `m_dirty = true` now do `cache.dirty = true`.

- [ ] **Step 3: Build and test**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: 73 tests pass.

- [ ] **Step 4: Commit**

```bash
git add amplifier/
git commit -m "refactor(amplifier): use IComponentEngine::CachedInput"
```

---

## Task 4: Convert AdcEngine, CoaxCableEngine, MixerEngine, SplitterEngine, IdealFilterEngine, PFBChannelizerEngine to use CachedInput

These six engines have the same pattern as amplifier. Repeat the same mechanical change for each.

**Files:**
- Modify: `adc/include/adc_engine.h`, `adc/src/adc_engine.cpp`
- Modify: `coax/include/coax_cable_engine.h`, `coax/src/coax_cable_engine.cpp`
- Modify: `mixer/include/mixer_engine.h`, `mixer/src/mixer_engine.cpp`
- Modify: `splitter/include/splitter_engine.h`, `splitter/src/splitter_engine.cpp`
- Modify: `ideal_filter/include/ideal_filter_engine.h`, `ideal_filter/src/ideal_filter_engine.cpp`
- Modify: `pfb_channelizer/include/pfb_channelizer_engine.h`, `pfb_channelizer/src/pfb_channelizer_engine.cpp`

- [ ] **Step 1: For each engine in turn — delete the 3 fields, update `update()` to use `isCacheValid`, update setters to set `cache.dirty = true`**

The pattern is identical to Task 3. The only engine with multi-pin caching is `SParamEngine` (handled in Task 5).

- [ ] **Step 2: Build incrementally to catch issues early**

```bash
cmake --build build
```

After each engine conversion, build. Fix any compile errors before moving to the next engine.

- [ ] **Step 3: Run tests**

```bash
ctest --test-dir build --output-on-failure
```

Expected: 73 tests pass.

- [ ] **Step 4: Commit**

```bash
git add adc/ coax/ mixer/ splitter/ ideal_filter/ pfb_channelizer/
git commit -m "refactor: use IComponentEngine::CachedInput in remaining engines"
```

---

## Task 5: Update SParamEngine to use CachedInput (multi-input version)

**Files:**
- Modify: `s_parametric_component/include/s_param_engine.h`
- Modify: `s_parametric_component/src/s_param_engine.cpp`

**Interfaces:**
- Consumes: `IComponentEngine::isCacheValid()` and `IComponentEngine::CachedInput`.
- Produces: same `update()` behavior. SParamEngine tracks multiple cached inputs in a `std::vector<CachedInput>` since it has N input ports (one per S-parameter port).

- [ ] **Step 1: Replace the 3 fields with a vector**

In `s_param_engine.h`, delete:

```cpp
std::vector<const Spectrum*> m_cached_input_ptrs;
std::vector<uint64_t> m_cached_input_generations;
```

Add a `std::vector<CachedInput> m_caches;` member. `m_dirty` is now `m_caches[0].dirty` (or a separate bool if you prefer — pick whichever is simpler). Use the base `IComponentEngine::cache` for the first port, plus additional `CachedInput` instances for ports 1..N-1.

- [ ] **Step 2: Update `update()` to iterate the cache vector**

Replace the per-input cache check with a loop over `m_caches`. The check becomes: if any input's pointer or generation changed, or any `cache.dirty` is true, recompute.

- [ ] **Step 3: Update setters**

The setters that flip `m_dirty = true` now flip all `m_caches[i].dirty = true`.

- [ ] **Step 4: Build and test**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: 73 tests pass. `test_s_parameter_amplifier` exercises this engine heavily.

- [ ] **Step 5: Commit**

```bash
git add s_parametric_component/
git commit -m "refactor(s_param): use IComponentEngine::CachedInput for multi-port"
```

---

## Task 6: Simplify ComponentRegistry — drop type_index, add getPFBs

**Files:**
- Modify: `app/include/component_registry.h`
- Modify: `app/src/component_registry.cpp`
- Modify: `app/src/app.cpp`

**Interfaces:**
- Consumes: existing call sites for `byType<T>()`.
- Produces: `std::vector<PFBChannelizerEngine*> getPFBs() const;` in `ComponentRegistry`.

- [ ] **Step 1: Drop `m_type_index` and `byType<T>()` from the header**

In `app/include/component_registry.h`:
- Delete the `<typeindex>` and `<typeinfo>` includes (no longer needed).
- Delete the `m_type_index` member: `std::unordered_map<std::type_index, std::vector<IComponentEngine*>> m_type_index;`.
- Delete the `byType<T>()` template.
- Delete the `m_type_index[std::type_index(typeid(T))].push_back(...)` line from the `add<T>` template.

- [ ] **Step 2: Add `getPFBs()` method**

In the public section of `ComponentRegistry`, add:

```cpp
std::vector<PFBChannelizerEngine*> getPFBs() const;
```

This requires including `pfb_channelizer_engine.h` in the registry header. **Or** declare it in the header and define it in `component_registry.cpp` to avoid the heavy include in the header. The latter is preferred — keep the registry header light. Forward-declare `class PFBChannelizerEngine;` in the header, define the method in the `.cpp` after the include.

- [ ] **Step 3: Implement `getPFBs()` in the .cpp**

In `app/src/component_registry.cpp`:

```cpp
#include "pfb_channelizer_engine.h"

std::vector<PFBChannelizerEngine*> ComponentRegistry::getPFBs() const {
    std::vector<PFBChannelizerEngine*> result;
    for (const auto& comp : m_components) {
        if (auto* pfb = dynamic_cast<PFBChannelizerEngine*>(comp.get())) {
            result.push_back(pfb);
        }
    }
    return result;
}
```

(Or use a static cast if you're sure the registry only holds `IComponentEngine*` subclasses and you're checking `PFB` specifically — but `dynamic_cast` is the safer pattern given RTTI is already on.)

- [ ] **Step 4: Update `app.cpp` callsites**

In `app/src/app.cpp`, find the two `m_components.byType<PFBChannelizerEngine>()` calls (one in the PFB onAdd callback, one in the onRemoveNode callback, and one in the destructor). Replace each with `m_components.getPFBs()`.

- [ ] **Step 5: Update `inspector_panel.cpp` callsites**

In `app/src/inspector_panel.cpp`, find the calls to `m_components->byType<PFBChannelizerEngine>()` (the inspector uses this for its PFB selector). Replace with the new accessor. If the inspector currently takes a `ComponentRegistry&`, add a `getPFBs()` method call there.

- [ ] **Step 6: Build and test**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: 73 tests pass.

- [ ] **Step 7: Commit**

```bash
git add app/
git commit -m "refactor: simplify ComponentRegistry, drop type_index, add getPFBs"
```

---

## Task 7: Final verification

- [ ] **Step 1: Full build**

```bash
cmake --build build
```

Expected: BUILD SUCCEEDED.

- [ ] **Step 2: Full test**

```bash
ctest --test-dir build --output-on-failure
```

Expected: 73 tests pass.

- [ ] **Step 3: Manual smoke**

Run the app. Add a PFB component, verify it shows up. Click around, drag, the DSP updates. The PFB widgets (IQ plot, channelizer grid) still work — they were driven by the byType lookup that's now getPFBs.

- [ ] **Step 4: Measure line reduction**

```bash
find . -path ./build -prune -o -path ./node_modules -prune -o -path ./out -prune -o -type f \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" \) -print | xargs wc -l 2>/dev/null | tail -1
```

Expected: ~150 lines removed (3 lines × 8 engines = 24 + ~30 lines from registry = ~54 lines, but the spec's "160" estimate is based on removing the whole pattern including comments and update() bodies, so realistic savings may be higher).

- [ ] **Step 5: Merge**

Push and open a PR.
