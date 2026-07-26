#include "test_helpers.h"
#include "app.h"
#include "imnodes.h"

int NodeHelper::addComponent(ImGuiTestContext *ctx, RfSimulatorApp &app, const char *menuLabel) {
    (void)ctx; (void)app; (void)menuLabel;
    return -1;
}

void NodeHelper::selectNode(ImGuiTestContext *ctx, int nodeId) {
    (void)ctx; (void)nodeId;
}

void NodeHelper::deleteSelectedNode(ImGuiTestContext *ctx) {
    (void)ctx;
}

void InspectorHelper::waitForPopulated(ImGuiTestContext *ctx, int nodeId) {
    (void)ctx; (void)nodeId;
}

void InspectorHelper::clickButton(ImGuiTestContext *ctx, const char *label) {
    (void)ctx; (void)label;
}

void InspectorHelper::toggleCheckbox(ImGuiTestContext *ctx, const char *label) {
    (void)ctx; (void)label;
}

void InspectorHelper::setInputDouble(ImGuiTestContext *ctx, const char *label, double value) {
    (void)ctx; (void)label; (void)value;
}

void InspectorHelper::selectComboItem(ImGuiTestContext *ctx, const char *comboLabel,
                                      const char *itemLabel) {
    (void)ctx; (void)comboLabel; (void)itemLabel;
}
