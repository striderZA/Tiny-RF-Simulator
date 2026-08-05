#include "amplifier_digitizer_widget.h"
#include <catch2/catch_test_macros.hpp>
#include <imgui.h>
#include <implot.h>

struct ImGuiFixture {
    ImGuiFixture() {
        ImGui::CreateContext();
        ImPlot::CreateContext();
        ImGui::GetIO().DisplaySize = ImVec2(1280.0f, 720.0f);
        unsigned char *pixels = nullptr;
        int width = 0;
        int height = 0;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    }
    ~ImGuiFixture() {
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
    }
};

TEST_CASE_METHOD(ImGuiFixture, "AmplifierDigitizerWidget draws closed-safe",
                 "[amp_import_widget]") {
    AmplifierDigitizerWidget widget;
    bool open = true;
    ImGui::NewFrame();
    REQUIRE_NOTHROW(widget.draw(&open));
    ImGui::Render();
}
