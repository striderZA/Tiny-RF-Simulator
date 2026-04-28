# ImGui Test Engine Integration Design

## Summary

Integrate Dear ImGui Test Engine (`imgui_test_engine`) as a UI automation/testing framework via a separate `test_ui` executable, enabling automated UI testing of widgets (generator, amplifier, spectrum analyzer) when UI changes are made.

## Approach

A separate `test_ui` binary that initializes GLFW + ImGui + ImPlot + `RfSimulatorApp`, then runs the test engine in fast mode and exits. Existing Catch2 unit tests (engine-level DSP) are untouched.

## Architecture

### 1. Build System (root `CMakeLists.txt`)

- Add `FetchContent_Declare` for `ocornut/imgui_test_engine` (pinned to a recent tag)
- Add private compile definition `IMGUI_ENABLE_TEST_ENGINE` to the existing `imgui` static library target (enables test hooks in imgui's context struct — negligible overhead)
- Create `imgui_test_engine` static library from the fetched source files
- Add `add_subdirectory("test_engine")` for the test binary

### 2. `imgui_test_engine` library target

**Sources** (from `imgui_test_engine/`):
```
imgui_te_context.cpp
imgui_te_engine.cpp
imgui_te_coroutine.cpp
imgui_te_exporters.cpp
imgui_te_ui.cpp
imgui_te_utils.cpp
imgui_capture_tool.cpp
```
**Include dirs**: `imgui_test_engine/` (headers), `imgui_test_engine/thirdparty/` (Str.h, stb)
**Defines**: `IMGUI_TEST_ENGINE_ENABLE_COROUTINE_STDTHREAD_IMPL=1` (simplest coroutine setup via std::thread)
**Links**: `imgui`, `glfw`, `OpenGL::GL`

### 3. `test_ui` executable (`test_engine/CMakeLists.txt`)

**Sources**:
- `main.cpp` — entry point, init/shutdown/loop, test engine lifecycle
- `ui_tests.cpp` — test registration (via `IM_REGISTER_TEST`)

**Links**: `imgui_test_engine`, `simulator::app`, `simulator::core`, `common`

**Lifecycle**:
1. Init GLFW → create window → make context current
2. `ImGui::CreateContext()` + `ImPlot::CreateContext()`
3. Init OpenGL2 backends
4. `ImGuiTestEngine_CreateContext()` → configure I/O → `ImGuiTestEngine_Start()`
5. Register tests via `RegisterUiTests(engine)`
6. Queue all tests programmatically
7. Main loop: `glfwPollEvents()` → `ImGui::NewFrame()` → `m_app.draw_ui()` + `m_app.update_dsp()` + `ImGuiTestEngine_ShowTestEngineWindows()` → render → `ImGuiTestEngine_PostSwap()`
8. After queue drains: `ImGuiTestEngine_Stop()` → print results → return exit code
9. Shutdown backends, destroy contexts

### 4. Initial test file (`test_engine/ui_tests.cpp`)

First battery of tests:
- **Add/remove generators**: Click "Add Generator", verify count, click "Remove"
- **Add/remove amplifiers**: Same pattern
- **Default state**: Verify initial generators present in UI, verify initial amplifiers present

### 5. CTest integration

In `test_engine/CMakeLists.txt`:
```cmake
add_test(NAME test_ui COMMAND test_ui)
```
The test engine runs in fast mode, queued tests execute sequentially, exit code reflects pass/fail.

## Key Decisions

- **Separate binary** over embedding in main.exe: clean separation, CI-friendly, no conditional compilation in release build
- **Always compile imgui with `IMGUI_ENABLE_TEST_ENGINE`**: two imgui libs would be messy; the define adds negligible overhead (a pointer + a few branches)
- **`STDTHREAD_IMPL` coroutines**: simplest setup, no dependency on platform coroutines
- **No `imgui_app` helper**: the project already has its own GLFW init in `core.cpp`; the test binary mirrors that pattern instead of pulling in shared helper code from the test engine repo

## Files Changed

| File | Change |
|------|--------|
| `CMakeLists.txt` | Add FetchContent, test_engine lib, subdirectory |
| `test_engine/CMakeLists.txt` | New: test_ui executable with CTest |
| `test_engine/main.cpp` | New: test_ui entry point |
| `test_engine/ui_tests.cpp` | New: UI test registrations |
