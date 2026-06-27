# Ponytail Cleanup 1 — Low-hanging Fruit Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Delete six small over-engineered or redundant pieces: IconRegistry PIMPL, RfSimulatorCore PIMPL, 5 zero-filled coax presets, mutable RBW cache in SpectrumAnalyzerEngine, double-clamp blocks in `utils.h`, and the redundant `nodes_span()` accessor in ViewManager. Net ~480 lines deleted, zero API changes, no behavior changes.

**Architecture:** Pure deletion work. Each task removes one piece of cruft, builds, and tests. No new dependencies, no shared state, no ordering dependency between tasks beyond what CMake requires for valid builds.

**Tech Stack:** C++20, Catch2 v3.4.0, ImGui, ImPlot, GLFW, OpenGL 2.1. No changes to tech stack.

**Specification:** `docs/superpowers/specs/2026-06-27-ponytail-cleanup-design.md` §4.

## Global Constraints

- **C++20** standard. No new language features used.
- **Code style**: 4-space indent, 100-col width, `PointerAlignment: Right` per `.clang-format`. Run `clang-format -i <file>` before committing.
- **No behavior changes** — all existing tests must pass after every task.
- **One commit per task** — each task is independently revertible.
- **Branch from `master`**: `refactor/ponytail-cleanup-01-low-hanging-fruit`.

## File Structure

```
icon_registry/
├── CMakeLists.txt                    # modified — drop texture_loader.cpp, keep header-only loaders
├── include/icon_registry.h           # modified — header-only loader, no PIMPL
└── src/                              # modified — drop icon_registry.cpp, texture_loader.h, texture_loader.cpp
    └── (deleted)

core/                                 # deleted entirely
core/CMakeLists.txt                   # deleted
core/include/core.h                   # deleted
core/src/core.cpp                     # deleted

src/main.cpp                          # modified — gains runSimulator() and inline GLFW/ImGui setup

coax/include/coax_presets.h           # modified — 5 placeholder entries removed
coax/src/coax_cable_engine.cpp        # modified — default m_preset_index = 0
tests/test_coax_cable_presets.cpp     # modified — assert size == 1

spectrum_analyzer/
├── include/spectrum_analyzer_engine.h    # modified — 5-line mutable cache removed
└── src/spectrum_analyzer_engine.cpp      # modified — cache read/write removed

core/include/utils.h                  # modified — before-widget clamp blocks removed
common/view_manager.h                 # modified — nodes_span() removed
common/CMakeLists.txt                 # modified — drop core from PUBLIC link lines if any

app/src/app.cpp                       # modified — IconRegistry member refactored, nodes_span() callers updated
app/src/component_registry.cpp        # modified — IconRegistry refactored, nodes_span() callers updated
node_graph/include/node_graph_widget.h    # modified — m_icons member type changes
node_graph/src/node_graph_widget.cpp      # modified — IconRegistry method calls replaced

CMakeLists.txt                        # modified — drop add_subdirectory(core) if present
tests/CMakeLists.txt                  # modified — drop core from test linkage if present
```

---

## Task 1: Replace IconRegistry with header-only loaders

**Files:**
- Modify: `icon_registry/include/icon_registry.h`
- Modify: `icon_registry/CMakeLists.txt`
- Delete: `icon_registry/src/icon_registry.cpp`
- Delete: `icon_registry/src/texture_loader.h`
- Delete: `icon_registry/src/texture_loader.cpp`
- Modify: `icon_registry/CMakeLists.txt` (the `src/` directory)

**Interfaces:**
- Consumes: `<imgui.h>` (for `ImTextureID`), `<unordered_map>`, `<string>`
- Produces: a `std::unordered_map<std::string, ImTextureID>` directly held by `NodeGraphWidget`; two free functions: `ImTextureID loadPNG(const char* path)`, `void freePNG(ImTextureID tex)`.

- [ ] **Step 1: Read current IconRegistry structure**

Read all four current files to confirm contents. The current `IconRegistry` has a PIMPL `struct Impl` with one `std::unordered_map<std::string, ImTextureID>`, and the free functions in `texture_loader.h/.cpp` (`loadTextureFromFile`, `freeTexture`).

- [ ] **Step 2: Rewrite `icon_registry/include/icon_registry.h` as a header-only loader module**

Replace the entire file with:

```cpp
#pragma once
#include "imgui.h"
#include <string>
#include <unordered_map>

// Load a PNG from disk into an ImGui-compatible GL texture.
// Returns ImTextureID(0) on failure.
ImTextureID loadPNG(const char* png_path);

// Release a texture previously returned by loadPNG().
void freePNG(ImTextureID tex);
```

- [ ] **Step 3: Move implementations into the header as `inline`**

Edit the new header to add the implementations at the bottom (header-only). Use `inline` to avoid ODR violations. The body is the same as the current `texture_loader.cpp` content — copy verbatim. Result:

```cpp
#pragma once
#include "imgui.h"
#include <string>
#include <unordered_map>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/gl.h>

inline ImTextureID loadPNG(const char* png_path) {
    int w, h, channels;
    unsigned char* data = stbi_load(png_path, &w, &h, &channels, 4);
    if (!data)
        return ImTextureID(0);

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

    stbi_image_free(data);
    return ImTextureID(static_cast<ImU64>(tex));
}

inline void freePNG(ImTextureID tex) {
    if (!tex) return;
    GLuint gl_tex = static_cast<GLuint>(static_cast<ImU64>(tex));
    glDeleteTextures(1, &gl_tex);
}
```

- [ ] **Step 4: Delete the old `.cpp` and the texture_loader files**

Delete:
- `icon_registry/src/icon_registry.cpp`
- `icon_registry/src/texture_loader.h`
- `icon_registry/src/texture_loader.cpp`

The `icon_registry/src/` directory becomes empty — remove it.

- [ ] **Step 5: Update `icon_registry/CMakeLists.txt`**

The file currently builds a static library from `src/icon_registry.cpp`. Since the loader is now header-only, the library is unnecessary. Either:
- (a) Delete `icon_registry/CMakeLists.txt` and remove `add_subdirectory(icon_registry)` from the top-level `CMakeLists.txt`, OR
- (b) Keep the directory as a header-only INTERFACE library.

Pick (a) for fewer files. After this step, the top-level `CMakeLists.txt` should not contain `add_subdirectory("icon_registry")`. Verify by reading the top-level `CMakeLists.txt` and searching for `icon_registry`.

- [ ] **Step 6: Update `node_graph_widget` to use the new API**

The current `NodeGraphWidget` has an `IconRegistry m_icons;` member and calls `m_icons.load(prefix, path)`, `m_icons.get(label)`, `m_icons.clear()`. The new pattern is:
- Replace the member with: `std::unordered_map<std::string, ImTextureID> m_icons;`
- Replace `m_icons.load(prefix, path)` with: `m_icons[prefix] = loadPNG(path);` (skip if returns 0).
- Replace `m_icons.get(label)` with a small helper that walks the map by prefix (mirror the current behavior — `rfind(prefix, 0) == 0`).
- Replace `m_icons.clear()` with a loop that calls `freePNG(tex)` for each entry, then `m_icons.clear()`.

All call sites are in `node_graph/src/node_graph_widget.cpp`. Add `#include "icon_registry.h"` (which now defines `loadPNG`/`freePNG`).

- [ ] **Step 7: Build to verify**

```bash
cmake --build build
```

Expected: BUILD SUCCEEDED. If there are missing includes or symbol errors, fix and rebuild.

- [ ] **Step 8: Run tests**

```bash
ctest --test-dir build --output-on-failure
```

Expected: 73 tests pass.

- [ ] **Step 9: Commit**

```bash
git add icon_registry/ node_graph/ CMakeLists.txt
git commit -m "refactor: replace IconRegistry PIMPL with header-only loaders"
```

---

## Task 2: Inline RfSimulatorCore into `src/main.cpp`

**Files:**
- Delete: `core/include/core.h`
- Delete: `core/src/core.cpp`
- Delete: `core/CMakeLists.txt` (and the `core/` directory)
- Modify: `src/main.cpp`
- Modify: top-level `CMakeLists.txt` (remove `add_subdirectory("core")`)
- Modify: any target that depends on `core` (likely `app` and `node_graph`)

**Interfaces:**
- Consumes: nothing new.
- Produces: a `static bool runSimulator(const std::function<void()>& onGui);` in `src/main.cpp`. Returns `true` on graceful exit, `false` on GLFW init failure.

- [ ] **Step 1: Read the current RfSimulatorCore and main.cpp**

Read `core/include/core.h` and `core/src/core.cpp` to capture the full body. Read `src/main.cpp` to see how it currently calls `RfSimulatorCore`.

- [ ] **Step 2: Move the entire body into `src/main.cpp`**

The current `main.cpp` looks like:

```cpp
#include "core.h"
int main() {
    RfSimulatorCore sim;
    sim.Run([]() {
        RfSimulatorApp app;
        app.draw_ui();
    });
    return 0;
}
```

Replace `src/main.cpp` with the body of `core.cpp` inlined as a `static bool runSimulator(const std::function<void()>& onGui)`. The new main:

```cpp
#include "app.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl2.h"
#include "implot.h"
#include "logging_core.h"
#include "session_state.h"
#include <GLFW/glfw3.h>
#include <functional>
#include <imgui_internal.h>

static bool runSimulator(const std::function<void()>& onGui) {
    // ... full body of the old RfSimulatorCore::Initialize, MainLoop, Shutdown ...
}

int main() {
    if (!runSimulator([]() {
        RfSimulatorApp app;
        app.draw_ui();
    })) {
        return 1;
    }
    return 0;
}
```

Copy the bodies of `RfSimulatorCore::Initialize`, `RfSimulatorCore::MainLoop`, `RfSimulatorCore::Shutdown` verbatim into `runSimulator`. The `Impl` struct fields (`GLFWwindow* window`, `ImVec4 clearColor`, `bool done`) become local variables.

- [ ] **Step 3: Delete the `core/` directory**

Delete:
- `core/include/core.h`
- `core/src/core.cpp`
- `core/CMakeLists.txt`

The `core/` directory should be empty — remove it.

- [ ] **Step 4: Update top-level `CMakeLists.txt`**

Remove `add_subdirectory("core")` from the top-level `CMakeLists.txt`. Identify any target that linked to `core` and link it directly to `imgui`, `implot`, `glfw`, `OpenGL::GL` instead. Likely candidates: `app/CMakeLists.txt` and `node_graph/CMakeLists.txt` (they may have `target_link_libraries(... core)`). Update those to link to `imgui implot glfw OpenGL::GL` directly.

- [ ] **Step 5: Move any helpers from `core/include/utils.h`**

`core/include/utils.h` contains `utils::inputDouble` and `utils::inputFrequency` — these are used by other modules' widget code. If `core/` is being deleted, either:
- (a) Move `utils.h` into `common/include/utils.h` and update includes in widget `.cpp` files, OR
- (b) Create a new `ui/` module for it.

Pick (a) — it's the smallest move. Edit each `#include "core/include/utils.h"` to `#include "utils.h"`.

- [ ] **Step 6: Build to verify**

```bash
cmake --build build
```

Expected: BUILD SUCCEEDED. If a `core` symbol is unresolved, check `app/CMakeLists.txt` and `node_graph/CMakeLists.txt` for missed link lines.

- [ ] **Step 7: Run tests**

```bash
ctest --test-dir build --output-on-failure
```

Expected: 73 tests pass.

- [ ] **Step 8: Commit**

```bash
git add core/ src/main.cpp CMakeLists.txt app/ node_graph/
git commit -m "refactor: inline RfSimulatorCore into main.cpp"
```

---

## Task 3: Trim coax presets to MT 340 only

**Files:**
- Modify: `coax/include/coax_presets.h`
- Modify: `coax/src/coax_cable_engine.cpp`
- Modify: `tests/test_coax_cable_presets.cpp`

**Interfaces:**
- Consumes: existing `kCoaxCablePresets` consumers (coax_cable_engine.cpp uses `m_preset_index = 4`).
- Produces: `inline const std::array<CableSpec, 1> kCoaxCablePresets` containing only MT 340.

- [ ] **Step 1: Update `kCoaxCablePresets` to 1 entry**

Edit `coax/include/coax_presets.h`:

```cpp
#pragma once
#include <array>

struct CableSpec {
    const char* name;
    double K1_dB_per_m;
    double K2_dB_per_m;
    double delay_ns_per_m;
    double max_freq_GHz;
    double diameter_mm;
};

inline const std::array<CableSpec, 1> kCoaxCablePresets = {{
    // name,    K1,        K2,        delay, max_f,  diam
    {"MT 340", 0.004710,  0.000004,  0.4,   18.5,   8.6},
}};
```

- [ ] **Step 2: Update the default index in `CoaxCableEngine`**

In `coax/src/coax_cable_engine.cpp` (or `.h`), change the default `m_preset_index = 4` to `m_preset_index = 0` (since MT 340 is now at index 0).

- [ ] **Step 3: Update `tests/test_coax_cable_presets.cpp`**

Find the test that asserts `kCoaxCablePresets.size() == 6` and change it to `== 1`. If the test enumerates entries, remove the now-obsolete assertions for the dropped cables.

- [ ] **Step 4: Build and test**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: build succeeds, all tests pass.

- [ ] **Step 5: Commit**

```bash
git add coax/
git commit -m "refactor: trim coax presets to MT 340 only"
```

---

## Task 4: Remove mutable RBW cache in SpectrumAnalyzerEngine

**Files:**
- Modify: `spectrum_analyzer/include/spectrum_analyzer_engine.h`
- Modify: `spectrum_analyzer/src/spectrum_analyzer_engine.cpp`

**Interfaces:**
- Consumes: existing `renderSpectrum` callers (mainly `spectrum_analyzer_widget.cpp`).
- Produces: same `renderSpectrum` signature, just no cache lookup.

- [ ] **Step 1: Read the current `renderSpectrum` to find the cache logic**

Open `spectrum_analyzer/src/spectrum_analyzer_engine.cpp` and locate where `m_cache_spectrum`, `m_cache_spec_gen`, `m_cache_rbw`, `m_cache_bin_width`, and `m_cache_rbw_power_W` are read or written. The cache skips the inner `applyRBW` call when the same input + same RBW are seen twice in a row.

- [ ] **Step 2: Delete the cache fields from the header**

In `spectrum_analyzer/include/spectrum_analyzer_engine.h`, remove these 5 member declarations:

```cpp
mutable const Spectrum* m_cache_spectrum = nullptr;
mutable uint64_t m_cache_spec_gen = 0;
mutable double m_cache_rbw = 0;
mutable double m_cache_bin_width = 0;
mutable std::vector<double> m_cache_rbw_power_W;
```

- [ ] **Step 3: Replace the cache-check with an unconditional `applyRBW` call**

In `renderSpectrum`, delete the `if (cache hit) skip applyRBW` branch and just call `applyRBW` directly. The function becomes ~5 lines shorter and the cache state is gone.

- [ ] **Step 4: Build and test**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: build succeeds, all tests pass.

- [ ] **Step 5: Manual verify spectrum widget**

Run the app, add a signal generator and an amplifier, ensure the spectrum analyzer still shows the same trace (RBW/VBW still applied, but recomputed every frame).

- [ ] **Step 6: Commit**

```bash
git add spectrum_analyzer/
git commit -m "refactor: remove mutable RBW cache from SpectrumAnalyzerEngine"
```

---

## Task 5: Remove double-clamp in `utils.h`

**Files:**
- Modify: `core/include/utils.h` (or `common/include/utils.h` if it was moved in Task 2)

**Interfaces:**
- Consumes: existing widget callers.
- Produces: same `inputDouble` / `inputFrequency` signatures, just one clamp (after the widget call).

- [ ] **Step 1: Read the current `inputDouble` and `inputFrequency`**

Confirm the current shape. Each function has:
- A "clamp external writes" block (with `LOG_WARN`) BEFORE the `ImGui::InputDouble` call.
- The widget call.
- A "clamp after user change" block AFTER the widget call.

- [ ] **Step 2: Delete the BEFORE-widget clamp blocks**

Remove the `if (ref > upperLimit) { LOG_WARN; ref = upperLimit; } else if (ref < lowerLimit) { LOG_WARN; ref = lowerLimit; }` block at the top of each function. Keep the AFTER-widget clamp block (that's the one that actually guards user input).

- [ ] **Step 3: Build and test**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: build succeeds, all tests pass.

- [ ] **Step 4: Manual verify gain/Freq widgets still clamp**

Run the app, edit a generator frequency, a gain, an NF. Type a value above the max — it should still snap to the max (the post-widget clamp still does this).

- [ ] **Step 5: Commit**

```bash
git add core/include/utils.h
git commit -m "refactor: drop before-widget clamp blocks in utils"
```

---

## Task 6: Drop `nodes_span()` from ViewManager

**Files:**
- Modify: `common/view_manager.h`
- Modify: any caller (likely `app/src/app.cpp` or `app/src/component_registry.cpp` if they use `nodes_span()`)

- [ ] **Step 1: Find all `nodes_span()` callers**

```bash
grep -rn "nodes_span" .
```

Expected output: one definition in `common/view_manager.h` and zero callers (the audit confirmed only `nodes()` is used). If there are callers, switch them to `nodes()`.

- [ ] **Step 2: Delete the `nodes_span()` method**

In `common/view_manager.h`, remove:

```cpp
std::span<SignalNode *const> nodes_span() const { return {m_nodes.data(), m_nodes.size()}; }
```

The `<span>` include can also be removed from this header if no other use exists.

- [ ] **Step 3: Build and test**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: build succeeds, all tests pass.

- [ ] **Step 4: Commit**

```bash
git add common/view_manager.h
git commit -m "refactor: drop unused ViewManager::nodes_span"
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

Run the app:
- Window layout looks the same.
- Add signal generator, amplifier, coax cable, splitter. The coax preset dropdown shows only "MT 340".
- Drag the node graph around; PFB / IQ / spectrum widgets still update.
- Close and restart the app; window visibility state remembers (this still works because SessionState is not touched in this plan).

- [ ] **Step 4: Measure line reduction**

```bash
find . -path ./build -prune -o -path ./node_modules -prune -o -path ./out -prune -o -type f \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" \) -print | xargs wc -l 2>/dev/null | tail -1
```

Expected: down from 10,675 to ~10,200 (~475 lines removed).

- [ ] **Step 5: Merge**

Push the branch and open a PR (or merge directly to `master` if no PR review is needed for this kind of cleanup).
