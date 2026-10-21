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
void NodeHelper::selectNode(ImGuiTestContext *ctx, RfSimulatorApp &app, int nodeId) {
    const NodeGraphEngine &graph = app.testGraphEngine();
    const int group_id = graph.groupIdForNode(nodeId);
    const auto *group = graph.groupById(group_id);
    if (group && group->collapsed)
        IM_CHECK(app.testSetGroupCollapsed(group_id, false));

    // The Node Editor is a top-level window: address it from the root ref, since callers
    // often leave a window ref such as "Properties" set from an earlier inspector check.
    ctx->SetRef("");
    ctx->WindowFocus("Node Editor");
    ctx->Yield(4); // Let imnodes draw and register nodes in object pool
    ImNodes::EditorContextSet(app.testGraphWidget().context());
    ImNodes::ClearNodeSelection();
    ImNodes::SelectNode(nodeId);
    ctx->Yield(2);
    ImNodes::EditorContextSet(app.testGraphWidget().context());

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
    // The combo list is its own popup window, so resolve the item across windows from the root.
    ctx->SetRef("");
    char item_path[256];
    snprintf(item_path, sizeof(item_path), "**/%s", itemLabel);
    ctx->ItemClick(item_path);
    ctx->Yield(2);
}
