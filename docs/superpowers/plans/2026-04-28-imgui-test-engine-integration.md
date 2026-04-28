# ImGui Test Engine Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add imgui_test_engine as FetchContent dep, create `imgui_test_engine` static lib, build `test_ui` executable with first battery of UI tests.

**Architecture:** New `test_ui` binary mirrors `core.cpp`'s GLFW/ImGui init, creates `RfSimulatorApp`, then runs test engine in fast mode and auto-exits with pass/fail. Existing `main.exe` and Catch2 tests untouched.

**Tech Stack:** CMake FetchContent, imgui_test_engine (coroutine via std::thread), GLFW, OpenGL2, ImPlot

---

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `CMakeLists.txt` | Modify | Add FetchContent, `imgui_test_engine` lib target, `test_engine/` subdir |
| `test_engine/CMakeLists.txt` | Create | Build `test_ui.exe`, CTest integration |
| `test_engine/main.cpp` | Create | Entry point: init GLFW/ImGui/app, test engine lifecycle, main loop with auto-exit |
| `test_engine/ui_tests.cpp` | Create | `RegisterUiTests()` — first UI tests |

---

### Task 1: Root CMakeLists.txt — Add FetchContent + imgui_test_engine library

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add FetchContent declaration after existing FetchContent blocks**

Insert after the `FetchContent_MakeAvailable(catch2)` block at line 49:

```cmake
# Fetch imgui_test_engine
FetchContent_Declare(
    imgui_test_engine
    GIT_REPOSITORY https://github.com/ocornut/imgui_test_engine.git
    GIT_TAG main
)
FetchContent_MakeAvailable(imgui_test_engine)
```

- [ ] **Step 2: Add `IMGUI_ENABLE_TEST_ENGINE` define to imgui library target**

Find the existing `add_library(imgui STATIC ...)` block (line 57). Add a private compile definition:

```cmake
target_compile_definitions(imgui PRIVATE IMGUI_ENABLE_TEST_ENGINE)
```

This enables test engine hooks in imgui's context/items — needed for the test engine to track widget state.

- [ ] **Step 3: Create imgui_test_engine static library target**

Insert after the `target_link_libraries(implot PUBLIC imgui)` block (line 85):

```cmake
# Define imgui_test_engine library
add_library(imgui_test_engine STATIC
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_context.cpp
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_engine.cpp
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_coroutine.cpp
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_exporters.cpp
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_ui.cpp
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_utils.cpp
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_capture_tool.cpp
)

target_include_directories(imgui_test_engine PUBLIC
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/thirdparty
)

target_compile_definitions(imgui_test_engine PRIVATE
    IMGUI_TEST_ENGINE_ENABLE_COROUTINE_STDTHREAD_IMPL=1
)

target_link_libraries(imgui_test_engine PUBLIC imgui PRIVATE glfw OpenGL::GL)
```

- [ ] **Step 4: Add test_engine subdirectory**

Add after `add_subdirectory("tests")` (line 96):

```cmake
add_subdirectory("test_engine")
```

---

### Task 2: test_engine/main.cpp — Entry point with test engine lifecycle

**Files:**
- Create: `test_engine/main.cpp`

- [ ] **Step 1: Create the file**

```cpp
#define IMGUI_DEFINE_MATH_OPERATORS
#include "app.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl2.h"
#include <GLFW/glfw3.h>
#include <implot.h>
#include "imgui_test_engine/imgui_te_engine.h"
#include "imgui_test_engine/imgui_te_ui.h"
#include "imgui_test_engine/imgui_te_exporters.h"

extern void RegisterUiTests(ImGuiTestEngine* engine);

int main() {
    if (!glfwInit())
        return 1;

    GLFWwindow* window = glfwCreateWindow(800, 600, "RF Simulator UI Tests", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL2_Init();

    RfSimulatorApp app;

    ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
    ImGuiTestEngineIO& test_io = ImGuiTestEngine_GetIO(engine);
    test_io.ConfigVerboseLevel = ImGuiTestVerboseLevel_Info;
    test_io.ConfigVerboseLevelOnError = ImGuiTestVerboseLevel_Debug;
    test_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;

    ImGuiTestEngine_Start(engine, ImGui::GetCurrentContext());
    ImGuiTestEngine_InstallDefaultCrashHandler();

    RegisterUiTests(engine);
    ImGuiTestEngine_QueueTests(engine, ImGuiTestGroup_Tests, "all");

    int done_frames = 0;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        if (test_io.IsRequestingMaxAppSpeed)
            glfwSwapInterval(0);

        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::DockSpaceOverViewport(ImGui::GetID("MainDockSpace"), nullptr,
                                     ImGuiDockNodeFlags_PassthruCentralNode);

        app.draw_ui();
        app.update_dsp();
        ImGuiTestEngine_ShowTestEngineWindows(engine, nullptr);

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(window);
        }

        glfwSwapBuffers(window);

        ImGuiTestEngine_PostSwap(engine);

        if (ImGuiTestEngine_IsTestQueueEmpty(engine))
            done_frames++;
        else
            done_frames = 0;

        if (done_frames > 10)
            break;
    }

    ImGuiTestEngine_Stop(engine);

    int count_tested = 0, count_success = 0;
    ImGuiTestEngine_GetResult(engine, count_tested, count_success);
    ImGuiTestEngine_PrintResultSummary(engine);

    int exit_code = (count_tested > 0 && count_tested == count_success) ? 0 : 1;

    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    ImPlot::DestroyContext();

    ImGuiTestEngine_DestroyContext(engine);

    glfwDestroyWindow(window);
    glfwTerminate();

    return exit_code;
}
```

---

### Task 3: test_engine/ui_tests.cpp — First UI test registrations

**Files:**
- Create: `test_engine/ui_tests.cpp`

- [ ] **Step 1: Create the file**

```cpp
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_test_engine/imgui_te_context.h"

void RegisterUiTests(ImGuiTestEngine* e) {
    ImGuiTest* t = nullptr;

    t = IM_REGISTER_TEST(e, "rf_simulator", "signal_chain_exists");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        ctx->SetRef("Signal Chain");
        ctx->ItemExists("Generators:");
        ctx->ItemExists("Add Generator");
        ctx->ItemExists("Add Amplifier");
    };

    t = IM_REGISTER_TEST(e, "rf_simulator", "default_generators_present");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        ctx->SetRef("Signal Chain");
        ctx->ItemExists("Generator 0");
        ctx->ItemExists("Generator 1");
        ctx->ItemExists("Generator 2");
        ctx->ItemExists("Generator 3");
    };

    t = IM_REGISTER_TEST(e, "rf_simulator", "add_generator");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        ctx->SetRef("Signal Chain");
        ctx->ItemClick("Add Generator");
        ctx->ItemExists("Generator 4");
    };

    t = IM_REGISTER_TEST(e, "rf_simulator", "add_amplifier");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        ctx->SetRef("Signal Chain");
        ctx->ItemClick("Add Amplifier");
        ctx->ItemExists("Amplifier 1");
    };
}
```

---

### Task 4: test_engine/CMakeLists.txt — Build the test_ui executable

**Files:**
- Create: `test_engine/CMakeLists.txt`

- [ ] **Step 1: Create the file**

```cmake
cmake_minimum_required(VERSION 3.20)

project(rf_simulator_ui_tests LANGUAGES CXX)

add_executable(test_ui
    main.cpp
    ui_tests.cpp
)

target_link_libraries(test_ui PRIVATE
    imgui_test_engine
    simulator::app
    simulator::core
    common
)

include(CTest)
add_test(NAME test_ui COMMAND test_ui)
```

---

### Task 5: Build and verify

- [ ] **Step 1: Reconfigure and build**

```bash
cmake -B build -G Ninja
cmake --build build
```

- [ ] **Step 2: Run UI tests**

```bash
build/bin/test_ui.exe
```

Expected: Test engine runs, all 4 tests pass, exit code 0.

- [ ] **Step 3: Verify CTest integration**

```bash
ctest --test-dir build -R test_ui --output-on-failure
```

Expected: 1 test passes.
