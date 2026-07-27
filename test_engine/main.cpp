#define IMGUI_DEFINE_MATH_OPERATORS
#include "app.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl2.h"
#include "imgui_test_engine/imgui_te_engine.h"
#include "imgui_test_engine/imgui_te_exporters.h"
#include "imgui_test_engine/imgui_te_ui.h"
#include <GLFW/glfw3.h>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <imnodes.h>
#include <implot.h>
#include <thread>

// OpenGL screenshot capture callback for ImGui Test Engine
static bool ScreenCaptureFunc(ImGuiID viewport_id, int x, int y, int w, int h, unsigned int *pixels,
                              void * /*user_data*/) {
    IM_UNUSED(viewport_id);
    // Give compositor time to update before capture
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    int y2 = (int)ImGui::GetIO().DisplaySize.y - (y + h);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(x, y2, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    // Flip vertically (OpenGL reads bottom-up)
    const size_t comp = 4;
    const size_t stride = (size_t)w * comp;
    unsigned char *line_tmp = new unsigned char[stride];
    unsigned char *line_a = (unsigned char *)pixels;
    unsigned char *line_b = (unsigned char *)pixels + (stride * ((size_t)h - 1));
    while (line_a < line_b) {
        memcpy(line_tmp, line_a, stride);
        memcpy(line_a, line_b, stride);
        memcpy(line_b, line_tmp, stride);
        line_a += stride;
        line_b -= stride;
    }
    delete[] line_tmp;
    return true;
}

extern void RegisterUiTests(ImGuiTestEngine *engine, RfSimulatorApp &app);

int main() {
    if (!glfwInit())
        return 1;

    GLFWwindow *window = glfwCreateWindow(800, 600, "RF Simulator UI Tests", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImNodes::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL2_Init();

    RfSimulatorApp app;

    ImGuiTestEngine *engine = ImGuiTestEngine_CreateContext();
    ImGuiTestEngineIO &test_io = ImGuiTestEngine_GetIO(engine);
    test_io.ConfigVerboseLevel = ImGuiTestVerboseLevel_Info;
    test_io.ConfigVerboseLevelOnError = ImGuiTestVerboseLevel_Debug;
    test_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
    test_io.ConfigLogToTTY = true;
    test_io.ConfigSavedSettings = false;
    test_io.ScreenCaptureFunc = ScreenCaptureFunc;
    test_io.ConfigCaptureEnabled = true;
    test_io.ConfigCaptureOnError = true;

    ImGuiTestEngine_Start(engine, ImGui::GetCurrentContext());
    ImGuiTestEngine_InstallDefaultCrashHandler();

    RegisterUiTests(engine, app);
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
        // Skip the debug overlay in CI/headless runs: it grows as more tests
        // complete and can end up occluding the app's own windows, which
        // silently breaks position-based synthetic clicks in later tests. It's
        // only useful for interactive local debugging anyway.
        if (!std::getenv("CI"))
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
    ImNodes::DestroyContext();

    ImGuiTestEngine_DestroyContext(engine);

    glfwDestroyWindow(window);
    glfwTerminate();

    return exit_code;
}
