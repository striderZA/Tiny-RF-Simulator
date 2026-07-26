#pragma once

#include "imgui.h"
#include "imgui_test_engine/imgui_te_context.h"

class RfSimulatorApp;

struct NodeHelper {
    static int addComponent(ImGuiTestContext *ctx, RfSimulatorApp &app, const char *menuLabel);
    static void selectNode(ImGuiTestContext *ctx, int nodeId);
    static void deleteSelectedNode(ImGuiTestContext *ctx);
};

struct InspectorHelper {
    static void waitForPopulated(ImGuiTestContext *ctx, int nodeId);
    static void clickButton(ImGuiTestContext *ctx, const char *label);
    static void toggleCheckbox(ImGuiTestContext *ctx, const char *label);
    static void setInputDouble(ImGuiTestContext *ctx, const char *label, double value);
    static void selectComboItem(ImGuiTestContext *ctx, const char *comboLabel,
                                const char *itemLabel);
};
