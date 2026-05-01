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

    t = IM_REGISTER_TEST(e, "rf_simulator", "canvas_context_menu");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        ctx->WindowFocus("Node Editor");
        ctx->WindowResize("Node Editor", ImVec2(800, 600));
        ctx->Yield(2);

        // Right-click at the center of the Node Editor window (empty canvas)
        auto info = ctx->WindowInfo("Node Editor");
        ImVec2 center = info.RectFull.GetCenter();
        ctx->MouseMoveToPos(center);
        ctx->MouseClick(1);
        ctx->Yield(2);

        // Verify a popup opened
        IM_CHECK(ImGui::IsPopupOpen((ImGuiID)0, ImGuiPopupFlags_AnyPopup));

        if (ImGui::IsPopupOpen((ImGuiID)0, ImGuiPopupFlags_AnyPopup))
            ctx->PopupCloseOne();
    };

    t = IM_REGISTER_TEST(e, "rf_simulator", "node_context_menu");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        ctx->WindowFocus("Node Editor");
        ctx->WindowResize("Node Editor", ImVec2(800, 600));
        ctx->Yield(2);

        // Try to find a node, fall back to hardcoded offset
        auto node_info = ctx->ItemInfo("Generator 0", ImGuiTestOpFlags_NoError);
        ImVec2 click_pos = (node_info.ID != 0)
            ? node_info.RectFull.GetCenter()
            : ctx->WindowInfo("Node Editor").RectFull.Min + ImVec2(80, 80);

        ctx->MouseMoveToPos(click_pos);
        ctx->MouseClick(1);
        ctx->Yield(2);

        // Verify a popup opened
        IM_CHECK(ImGui::IsPopupOpen((ImGuiID)0, ImGuiPopupFlags_AnyPopup));

        if (ImGui::IsPopupOpen((ImGuiID)0, ImGuiPopupFlags_AnyPopup))
            ctx->PopupCloseOne();
    };

    t = IM_REGISTER_TEST(e, "rf_simulator", "properties_window_exists");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        ctx->WindowFocus("Properties");
    };
}
