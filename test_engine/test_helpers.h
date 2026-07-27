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

struct ScreenshotHelper {
    // Capture a specific window to a PNG file. Returns true on success.
    static bool captureWindow(ImGuiTestContext *ctx, const char *windowName,
                              const char *outputPath);

    // Capture the full viewport (all windows) to a PNG file. Returns true on success.
    static bool captureFullViewport(ImGuiTestContext *ctx, const char *outputPath);

    // Compare two PNG images pixel-by-pixel. Returns max channel difference (0-255).
    // Returns -1 on file load error or size mismatch.
    static int compareImages(const char *baselinePath, const char *currentPath,
                             int *widthOut = nullptr, int *heightOut = nullptr);

    // Capture window and compare against baseline. If baseline doesn't exist,
    // saves it and returns 0. If UPDATE_BASELINES env var is set, always overwrite.
    // Returns max pixel difference (0-255), or -1 on error.
    static int verifyBaseline(ImGuiTestContext *ctx, const char *windowName,
                              const char *baselineName);

    // Capture full viewport and compare against baseline. Same semantics as verifyBaseline.
    static int verifyFullViewportBaseline(ImGuiTestContext *ctx, const char *baselineName);

    // Baseline directory (relative to test executable working dir)
    static const char *baselineDir();
};
