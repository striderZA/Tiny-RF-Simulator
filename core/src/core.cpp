#include "core.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl2.h"
#include "layout_manager.h"
#include <GLFW/glfw3.h>
#include <imgui_internal.h>
#include <implot.h>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif

struct RfSimulatorCore::Impl {
    GLFWwindow *window = nullptr;
    ImVec4 clearColor = ImVec4(0.45f, 0.55f, 0.60f, 1.00);
    bool done = false;
    std::string layoutIniPath;
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

    p_impl->window = glfwCreateWindow(1920, 1080, "Tiny RF Simulator", nullptr, nullptr);
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
    LayoutManager layout_mgr;
    p_impl->layoutIniPath = layout_mgr.defaultLayoutPath();
    io.IniFilename = p_impl->layoutIniPath.c_str();
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

        {
            static bool first_run = []() {
                LayoutManager lm;
                return !lm.defaultLayoutExists();
            }();

            if (first_run) {
                first_run = false;
                ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");
                ImGui::DockBuilderRemoveNode(dockspace_id);
                ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);

                ImGuiID dock_root = dockspace_id;
                ImGuiID dock_bottom = ImGui::DockBuilderSplitNode(dock_root, ImGuiDir_Down, 0.20f,
                                                                  nullptr, &dock_root);
                ImGuiID dock_right = ImGui::DockBuilderSplitNode(dock_root, ImGuiDir_Right, 0.30f,
                                                                 nullptr, &dock_root);
                ImGuiID dock_properties = ImGui::DockBuilderSplitNode(dock_root, ImGuiDir_Down,
                                                                      0.35f, nullptr, &dock_root);
                ImGuiID dock_iq = ImGui::DockBuilderSplitNode(dock_right, ImGuiDir_Down, 0.40f,
                                                              nullptr, &dock_right);

                ImGui::DockBuilderDockWindow("Node Editor", dock_root);
                ImGui::DockBuilderDockWindow("Properties", dock_properties);
                ImGui::DockBuilderDockWindow("Spectrum Analyzer", dock_right);
                ImGui::DockBuilderDockWindow("IQ Plot", dock_iq);
                ImGui::DockBuilderDockWindow("Log", dock_bottom);

                ImGui::DockBuilderFinish(dockspace_id);
            }
        }

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
