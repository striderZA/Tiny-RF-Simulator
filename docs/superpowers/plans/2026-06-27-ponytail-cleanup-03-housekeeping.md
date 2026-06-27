# Ponytail Cleanup 3 — Housekeeping Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Inline `SessionState` into `RfSimulatorApp` (delete the Win32-only INI machinery). Move `nonlinear_model.h` implementation into a `.cpp` so amplifier/coax/s_param don't recompile the math on every change. Trim the group-infrastructure cascade in `NodeGraphEngine::removeNode` and `addGroup`. Net ~160 lines removed and faster compilation for amplifier/coax/s_param.

**Architecture:** `SessionState` (Win32 INI via `WritePrivateProfileStringA` / `GetPrivateProfileStringA`) was used only to persist ~6 window-visibility booleans across app restart. Replace with a small `static bool loadBool(const char* path, const char* key, bool default)` and `static void saveBool(...)` pair in `app.cpp` that reads/writes a plain text file (one line per key=value). `nonlinear_model.h`'s `process()` and `recomputeCoefficients()` move to `nonlinear_model.cpp`. The group-infrastructure trim removes the auto-removal of groups with `<2` members and stops eagerly rebuilding boundary pins for all surviving groups on every node removal.

**Tech Stack:** C++20, Catch2 v3.4.0. No new dependencies.

**Specification:** `docs/superpowers/specs/2026-06-27-ponytail-cleanup-design.md` §6.

## Global Constraints

- **C++20** standard.
- **Code style**: 4-space indent, 100-col width, `PointerAlignment: Right` per `.clang-format`. Run `clang-format -i <file>` before committing.
- **No behavior changes** — all existing tests must pass after every task.
- **One commit per task** — independently revertible.
- **Branch from `master`**: `refactor/ponytail-cleanup-03-housekeeping`.

## File Structure

```
common/session_state.h                    # deleted
app/include/app.h                         # modified — gains 4 bools (already there)
app/src/app.cpp                           # modified — gains inline persistence helpers
app/include/component_registry.h          # (no change expected)
tests/test_session_state.cpp              # deleted
tests/CMakeLists.txt                      # modified — drop the test file

common/nonlinear_model.h                  # modified — keeps declarations only
common/nonlinear_model.cpp                # new
common/CMakeLists.txt                     # modified — compile the new .cpp

node_graph/src/node_graph_engine.cpp      # modified — group cascade removed
node_graph/include/node_graph_engine.h    # (signature changes if any)
```

---

## Task 1: Inline SessionState into RfSimulatorApp

**Files:**
- Delete: `common/session_state.h`
- Modify: `app/include/app.h` (gains inline persistence helpers)
- Modify: `app/src/app.cpp` (replaces all `m_state.loadBool` / `m_state.saveBool` calls with inline helpers)
- Delete: `tests/test_session_state.cpp`
- Modify: `tests/CMakeLists.txt` (drop the test file)

- [ ] **Step 1: Read the current SessionState + all its callers**

Read `common/session_state.h` to capture the API:
- `SessionState()` — finds the EXE directory on Windows
- `load(section, key, default)` — `GetPrivateProfileStringA`
- `save(section, key, value)` — `WritePrivateProfileStringA`
- `loadBool(section, key, default)` / `saveBool(section, key, val)` — string round-trip
- `fileExists()` — used in `core.cpp` for first-run dockspace layout

```bash
grep -rn "SessionState\|m_state\." .
```

Capture every call site. The likely ones are in `app/src/app.cpp` (load/save of window state, PFB window state by id).

- [ ] **Step 2: Add inline persistence helpers to `app.cpp`**

At the top of `app/src/app.cpp`, after includes, add:

```cpp
namespace {

constexpr const char* kSettingsPath = "rf_simulator_settings.txt";

// Read a bool from a key=value text file. Returns default_val if file or key missing.
bool loadBool(const char* key, bool default_val) {
    std::ifstream in(kSettingsPath);
    if (!in) return default_val;
    std::string line;
    while (std::getline(in, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        if (line.substr(0, eq) == key) {
            return line.substr(eq + 1) == "1";
        }
    }
    return default_val;
}

// Write a bool to the settings file (creates or overwrites). Keeps other keys intact.
void saveBool(const char* key, bool val) {
    std::unordered_map<std::string, std::string> entries;
    {
        std::ifstream in(kSettingsPath);
        std::string line;
        while (std::getline(in, line)) {
            auto eq = line.find('=');
            if (eq != std::string::npos)
                entries[line.substr(0, eq)] = line.substr(eq + 1);
        }
    }
    entries[key] = val ? "1" : "0";
    std::ofstream out(kSettingsPath, std::ios::trunc);
    for (const auto& [k, v] : entries)
        out << k << "=" << v << "\n";
}

} // namespace
```

Add `#include <fstream>` and `#include <unordered_map>` to `app.cpp`.

- [ ] **Step 3: Replace all `m_state.loadBool("WindowState", key, default)` calls**

Pattern:
```cpp
m_state.loadBool("WindowState", "Log", true)
```
becomes:
```cpp
loadBool("Log", true)
```

- [ ] **Step 4: Replace all `m_state.saveBool("WindowState", key, val)` calls**

Same pattern — the section is no longer needed; the helper just stores key=value.

- [ ] **Step 5: Delete `SessionState` member from `RfSimulatorApp`**

In `app/include/app.h`, delete `SessionState m_state;`. Delete `#include "session_state.h"`.

- [ ] **Step 6: Delete the `SessionState` test file**

Delete `tests/test_session_state.cpp`. Remove it from the `if(WIN32) list(APPEND TEST_SOURCES test_session_state.cpp)` line in `tests/CMakeLists.txt`.

- [ ] **Step 7: Delete `common/session_state.h`**

```bash
git rm common/session_state.h
```

(Or just delete it and `git add -A` later.)

- [ ] **Step 8: Build to verify**

```bash
cmake --build build
```

Expected: BUILD SUCCEEDED.

- [ ] **Step 9: Run tests**

```bash
ctest --test-dir build --output-on-failure
```

Expected: 72 tests pass (session_state test removed). If `test_session_state` is still listed, check that the CMake list was edited.

- [ ] **Step 10: Manual verify window state persistence**

Run the app, change a window visibility (close a panel), quit, restart — the panel state should be remembered.

- [ ] **Step 11: Commit**

```bash
git add common/session_state.h app/ tests/
git commit -m "refactor: inline SessionState into RfSimulatorApp"
```

---

## Task 2: Split nonlinear_model.h into .h + .cpp

**Files:**
- Modify: `common/nonlinear_model.h`
- Create: `common/nonlinear_model.cpp`
- Modify: `common/CMakeLists.txt`

**Interfaces:**
- Consumes: `NonlinearModel` declared in the header.
- Produces: same `NonlinearModel` API. The implementations of `process()`, `recomputeCoefficients()`, and the `detail::dbmToW/wToDbm/dbmToV/vToDbm` helpers move to the `.cpp`.

- [ ] **Step 1: Read `common/nonlinear_model.h`**

Capture the full contents. The header is 147 lines and contains: `detail::dbmToW`, `detail::wToDbm`, `detail::dbmToV`, `detail::vToDbm` (inline functions), the `NonlinearModel` class with `setOIP2_dBm`, `setOIP3_dBm`, `setEnabled`, `enabled`, `oip2_dBm`, `oip3_dBm`, `process()`, `recomputeCoefficients()` (private), and `Result` struct.

- [ ] **Step 2: Create `common/nonlinear_model.cpp` with the implementations**

The `.cpp` should contain:
- The `detail::dbmToW` etc. functions (no longer inline).
- The `NonlinearModel::setOIP2_dBm`, `setOIP3_dBm`, `setEnabled` (move the body out of the header — the header retains the declaration only).
- The `NonlinearModel::process()` body.
- The `NonlinearModel::recomputeCoefficients()` body.

The header should retain:
- Includes.
- `namespace detail { double dbmToW(double dBm); ... }` declarations only.
- The `Result` struct.
- The `NonlinearModel` class declaration with method bodies for simple setters (or just declarations — the small ones can be defined inline if you prefer).

Recommended split: **everything in the .cpp** except the class declaration and the `Result` struct. Inline getter bodies are fine to keep in the header (they're trivial).

- [ ] **Step 3: Update `common/CMakeLists.txt`**

Add `src/nonlinear_model.cpp` to the `common` library's source list. The current `common/CMakeLists.txt` likely has a `target_sources(common PRIVATE ...)` line — append the new file.

- [ ] **Step 4: Build to verify**

```bash
cmake --build build
```

Expected: BUILD SUCCEEDED. If a link error appears for any of the moved functions, check that the `.cpp` is in the source list.

- [ ] **Step 5: Run tests**

```bash
ctest --test-dir build --output-on-failure
```

Expected: 72 tests pass. `test_s_parameter_amplifier` exercises `NonlinearModel` indirectly.

- [ ] **Step 6: Commit**

```bash
git add common/
git commit -m "refactor: split nonlinear_model.h to .cpp"
```

---

## Task 3: Trim group infrastructure cascade in NodeGraphEngine

**Files:**
- Modify: `node_graph/src/node_graph_engine.cpp`
- Modify: `node_graph/include/node_graph_engine.h` (if any signature changes)

- [ ] **Step 1: Read `NodeGraphEngine::removeNode` and `addGroup`**

Capture the current cascade:
- `removeNode`: removes node → cascades to "remove node_id from any groups, drop groups with < 2 members" → "rebuild boundary pins for all surviving groups".
- `addGroup`: at the end, calls `rebuildGroupBoundaryPins(m_groups.back().id);` immediately.

- [ ] **Step 2: Remove the auto-removal of groups with < 2 members in `removeNode`**

Delete the loop that finds `groups_to_remove` and the loop that calls `removeGroup`. The cascade becomes:

```cpp
// Was:
//   for (auto& g : m_groups) { ... erase from member_node_ids ... }
//   std::vector<int> groups_to_remove;
//   for (const auto& g : m_groups) { if (size < 2) groups_to_remove.push_back(g.id); }
//   for (int gid : groups_to_remove) { removeGroup(gid, false); }
//   rebuildNodeToGroupCache();
//   for (const auto& g : m_groups) { rebuildGroupBoundaryPins(g.id); }

// Becomes:
rebuildNodeToGroupCache();
// (group boundary pin rebuild is now lazy — handled when widget pulls it)
```

- [ ] **Step 3: Remove the eager `rebuildGroupBoundaryPins` loop in `removeNode`**

Delete the `for (const auto& g : m_groups) { rebuildGroupBoundaryPins(g.id); }` block.

- [ ] **Step 4: Make `rebuildGroupBoundaryPins` lazy in `addGroup`**

In `NodeGraphEngine::addGroup`, delete the `rebuildGroupBoundaryPins(m_groups.back().id);` call. The widget will pull boundary pins when it draws the group.

- [ ] **Step 5: Verify the widget handles stale boundary pins**

Open `node_graph/src/node_graph_widget.cpp` and find where `g.boundary_pins` is read. If the widget was already using the cache, no change is needed. If it relies on the eager rebuild, add a lazy rebuild at the start of the draw:

```cpp
for (auto& g : m_engine.groups()) {
    if (g.boundary_pins.empty() || /* group has been modified since last rebuild */) {
        m_engine.rebuildGroupBoundaryPins(g.id);
    }
}
```

The exact condition depends on how staleness is tracked. The simplest is: rebuild at the start of every group draw. The performance cost is acceptable for typical group counts (< 20).

- [ ] **Step 6: Build to verify**

```bash
cmake --build build
```

Expected: BUILD SUCCEEDED.

- [ ] **Step 7: Run tests**

```bash
ctest --test-dir build --output-on-failure
```

Expected: 72 tests pass. `test_group` and `test_bench_groups` exercise this code.

- [ ] **Step 8: Manual verify groups still work**

Run the app, create a group from 3 nodes, collapse it, expand it. Boundary pins should still appear when the group is collapsed. Run a node removal — the group should retain the remaining 2 members (not auto-removed, per the new behavior).

- [ ] **Step 9: Commit**

```bash
git add node_graph/
git commit -m "refactor: trim group infrastructure cascade in NodeGraphEngine"
```

---

## Task 4: Final verification

- [ ] **Step 1: Full build**

```bash
cmake --build build
```

Expected: BUILD SUCCEEDED.

- [ ] **Step 2: Full test**

```bash
ctest --test-dir build --output-on-failure
```

Expected: 72 tests pass.

- [ ] **Step 3: Manual smoke**

Run the app:
- Window state persistence (from Task 1).
- Add components, create a group, expand/collapse.
- Verify the engine compiles quickly when only `nonlinear_model.cpp` changes (touch it, run `cmake --build build`, observe only that file recompiles, not amplifier/coax/s_param).

- [ ] **Step 4: Measure line reduction**

```bash
find . -path ./build -prune -o -path ./node_modules -prune -o -path ./out -prune -o -type f \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" \) -print | xargs wc -l 2>/dev/null | tail -1
```

Expected: ~150 lines removed.

- [ ] **Step 5: Merge**

Push and open a PR.
