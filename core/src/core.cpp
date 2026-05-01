#include "core.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl2.h"
#include <GLFW/glfw3.h>
#include <implot.h>

struct RfSimulatorCore::Impl {
    GLFWwindow *window = nullptr;
    ImVec4 clearColor = ImVec4(0.45f, 0.55f, 0.60f, 1.00);
    bool done = false;
};

RfSimulatorCore::RfSimulatorCore() : p_impl(new Impl()) {}

RfSimulatorCore::~RfSimulatorCore() {}

void RfSimulatorCore::Run(const std::function<void()> &onGui) {
    if (!Initialize()) {
        Shutdown();
        return;
    }
    MainLoop(onGui);
    Shutdown();
}

bool RfSimulatorCore::Initialize() {
    if (!glfwInit())
        return false;

    p_impl->window = glfwCreateWindow(1600, 900, "RF Simulator GUI", nullptr, nullptr);
    if (p_impl->window == nullptr) {
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(p_impl->window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();

    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;     // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;   // Enable Multi-Viewport
                                                          // / Platform Win

    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(p_impl->window, true);
    ImGui_ImplOpenGL2_Init();
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

    return true;
}

void RfSimulatorCore::Shutdown() {
    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    ImPlot::DestroyContext();

    if (p_impl->window)
        glfwDestroyWindow(p_impl->window);

    glfwTerminate();
}

void RfSimulatorCore::MainLoop(const std::function<void()> &onGui) {
    while (!glfwWindowShouldClose(p_impl->window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::DockSpaceOverViewport(ImGui::GetID("MainDockSpace"), nullptr,
                                     ImGuiDockNodeFlags_PassthruCentralNode);

        onGui();

        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(p_impl->window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(p_impl->clearColor.x * p_impl->clearColor.w,
                     p_impl->clearColor.y * p_impl->clearColor.w,
                     p_impl->clearColor.z * p_impl->clearColor.w, p_impl->clearColor.w);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(p_impl->window); // Restore main context after viewport rendering
        }
        glfwSwapBuffers(p_impl->window);
    }
    p_impl->done = true;
}
