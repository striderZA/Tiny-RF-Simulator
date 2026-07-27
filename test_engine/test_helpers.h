#pragma once

#include "app.h"
#include "component_registry.h"
#include "imgui.h"
#include "imgui_test_engine/imgui_te_context.h"

class RfSimulatorApp;

struct NodeHelper {
    static int addComponent(ImGuiTestContext *ctx, RfSimulatorApp &app, const char *menuLabel);
    static void selectNode(ImGuiTestContext *ctx, int nodeId);
    static void deleteSelectedNode(ImGuiTestContext *ctx);
    template <typename T> static int findComponentNodeId(RfSimulatorApp &app) {
        auto comps = app.testComponents().byType<T>();
        return comps.empty() ? -1 : comps[0]->graphNodeId();
    }
};

struct InspectorHelper {
    static void waitForPopulated(ImGuiTestContext *ctx, int nodeId);
    static void clickButton(ImGuiTestContext *ctx, const char *label);
    static void toggleCheckbox(ImGuiTestContext *ctx, const char *label);
    static void setInputDouble(ImGuiTestContext *ctx, const char *label, double value);
    static void selectComboItem(ImGuiTestContext *ctx, const char *comboLabel,
                                const char *itemLabel);
};
