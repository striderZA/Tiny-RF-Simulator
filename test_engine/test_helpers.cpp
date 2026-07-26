#define IMGUI_DEFINE_MATH_OPERATORS
#include "test_helpers.h"
#include "app.h"
#undef Yield
#include "imnodes.h"

int NodeHelper::addComponent(ImGuiTestContext *ctx, RfSimulatorApp &app, const char *menuLabel) {
    auto &nodes = app.testGraphEngine().nodes();
    size_t count_before = nodes.size();

    ctx->WindowFocus("Node Editor");
    auto info = ctx->WindowInfo("Node Editor");
    ctx->MouseMoveToPos(info.RectFull.GetCenter());
    ctx->MouseClick(ImGuiMouseButton_Right);
    ctx->Yield(2);

    IM_CHECK_RETV(ImGui::IsPopupOpen((ImGuiID)0, ImGuiPopupFlags_AnyPopup), -1);
    ctx->SetRef("canvas_context_menu");
    ctx->ItemClick(menuLabel);
    ctx->Yield(2);

    IM_CHECK_RETV(nodes.size() == count_before + 1, -1);
    return nodes.back().node_id;
}
void NodeHelper::selectNode(ImGuiTestContext *ctx, int nodeId) {
    ctx->WindowFocus("Node Editor");
    ImNodes::ClearNodeSelection();
    ImNodes::SelectNode(nodeId);
    ctx->Yield(2);

    IM_CHECK_EQ(ImNodes::NumSelectedNodes(), 1);
    int selected = -1;
    ImNodes::GetSelectedNodes(&selected);
    IM_CHECK_EQ(selected, nodeId);
}

void NodeHelper::deleteSelectedNode(ImGuiTestContext *ctx) {
    ctx->KeyDown(ImGuiKey_Delete);
    ctx->Yield(2);
    IM_CHECK_EQ(ImNodes::NumSelectedNodes(), 0);
}

void InspectorHelper::waitForPopulated(ImGuiTestContext *ctx, int nodeId) {
    for (int frame = 0; frame < 30; ++frame) {
        ctx->Yield();
        if (ImNodes::NumSelectedNodes() == 1) {
            int sel = -1;
            ImNodes::GetSelectedNodes(&sel);
            if (sel == nodeId)
                return;
        }
    }
    IM_CHECK(false);
}

void InspectorHelper::clickButton(ImGuiTestContext *ctx, const char *label) {
    ctx->SetRef("Properties");
    ctx->ItemClick(label);
    ctx->Yield(2);
}

void InspectorHelper::toggleCheckbox(ImGuiTestContext *ctx, const char *label) {
    ctx->SetRef("Properties");
    ctx->ItemClick(label);
    ctx->Yield(2);
}

void InspectorHelper::setInputDouble(ImGuiTestContext *ctx, const char *label, double value) {
    ctx->SetRef("Properties");
    char buf[64];
    snprintf(buf, sizeof(buf), "%.6g", value);
    ctx->ItemInputValue(label, buf);
    ctx->Yield(2);
}

void InspectorHelper::selectComboItem(ImGuiTestContext *ctx, const char *comboLabel,
                                      const char *itemLabel) {
    ctx->SetRef("Properties");
    ctx->ItemClick(comboLabel);
    ctx->Yield(2);
    ctx->ItemClick(itemLabel);
    ctx->Yield(2);
}
