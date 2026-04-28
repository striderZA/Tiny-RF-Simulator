#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_test_engine/imgui_te_context.h"

void RegisterUiTests(ImGuiTestEngine* e) {
    ImGuiTest* t = nullptr;

    t = IM_REGISTER_TEST(e, "rf_simulator", "node_editor_exists");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        ctx->WindowFocus("Node Editor");
    };

    t = IM_REGISTER_TEST(e, "rf_simulator", "single_generator_present");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        ctx->SetRef("Generator 0");
        ctx->ItemExists("Measure");
    };

    t = IM_REGISTER_TEST(e, "rf_simulator", "single_amplifier_present");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        ctx->SetRef("Amplifier 0");
        ctx->ItemExists("Measure");
    };
}
