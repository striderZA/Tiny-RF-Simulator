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
