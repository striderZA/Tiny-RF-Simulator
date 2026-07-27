#define IMGUI_DEFINE_MATH_OPERATORS
#include "test_helpers.h"
#include "app.h"
#undef Yield
#include "imnodes.h"

#include "imgui_test_engine/imgui_capture_tool.h"
#include "stb_image.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

const char *ScreenshotHelper::baselineDir() {
    static char path[512];
    static bool initialized = false;
    if (!initialized) {
        snprintf(path, sizeof(path), "%s/test_engine/baselines", PROJECT_SOURCE_DIR);
        initialized = true;
    }
    return path;
}

bool ScreenshotHelper::captureWindow(ImGuiTestContext *ctx, const char *windowName,
                                     const char *outputPath) {
    ctx->CaptureReset();
    ctx->CaptureAddWindow(windowName);
    snprintf(ctx->CaptureArgs->InOutputFile, sizeof(ctx->CaptureArgs->InOutputFile), "%s",
             outputPath);
    // Note: ImGuiCaptureFlags_Instant is only valid for explicit-rect captures
    // (IM_ASSERT(args->InCaptureWindows.empty()) in imgui_capture_tool.cpp). Window
    // captures must use the default multi-frame path.
    return ctx->CaptureScreenshot(0);
}

bool ScreenshotHelper::captureFullViewport(ImGuiTestContext *ctx, const char *outputPath) {
    ctx->CaptureReset();
    snprintf(ctx->CaptureArgs->InOutputFile, sizeof(ctx->CaptureArgs->InOutputFile), "%s",
             outputPath);
    return ctx->CaptureScreenshot(0);
}

int ScreenshotHelper::compareImages(const char *baselinePath, const char *currentPath,
                                    int *widthOut, int *heightOut) {
    int w1, h1, n1, w2, h2, n2;
    unsigned char *img1 = stbi_load(baselinePath, &w1, &h1, &n1, 4);
    if (!img1)
        return -1;
    unsigned char *img2 = stbi_load(currentPath, &w2, &h2, &n2, 4);
    if (!img2) {
        stbi_image_free(img1);
        return -1;
    }
    if (w1 != w2 || h1 != h2) {
        stbi_image_free(img1);
        stbi_image_free(img2);
        return -1;
    }
    if (widthOut)
        *widthOut = w1;
    if (heightOut)
        *heightOut = h1;
    int maxDiff = 0;
    size_t total = (size_t)w1 * h1 * 4;
    for (size_t i = 0; i < total; ++i) {
        int d = (int)img1[i] - (int)img2[i];
        if (d < 0)
            d = -d;
        if (d > maxDiff)
            maxDiff = d;
    }
    stbi_image_free(img1);
    stbi_image_free(img2);
    return maxDiff;
}

int ScreenshotHelper::verifyBaseline(ImGuiTestContext *ctx, const char *windowName,
                                     const char *baselineName) {
    char baselinePath[512];
    snprintf(baselinePath, sizeof(baselinePath), "%s/%s.png", baselineDir(), baselineName);
    char currentPath[512];
    snprintf(currentPath, sizeof(currentPath), "%s/%s_current.png", baselineDir(), baselineName);

    bool forceUpdate = std::getenv("UPDATE_BASELINES") != nullptr;

    if (!forceUpdate) {
        // Check if baseline exists
        FILE *f = fopen(baselinePath, "rb");
        if (f) {
            fclose(f);
            // Capture current screenshot
            if (!captureWindow(ctx, windowName, currentPath))
                return -1;
            return compareImages(baselinePath, currentPath);
        }
    }

    // Save as baseline
    return captureWindow(ctx, windowName, baselinePath) ? 0 : -1;
}

int ScreenshotHelper::verifyFullViewportBaseline(ImGuiTestContext *ctx, const char *baselineName) {
    char baselinePath[512];
    snprintf(baselinePath, sizeof(baselinePath), "%s/%s.png", baselineDir(), baselineName);
    char currentPath[512];
    snprintf(currentPath, sizeof(currentPath), "%s/%s_current.png", baselineDir(), baselineName);

    bool forceUpdate = std::getenv("UPDATE_BASELINES") != nullptr;

    if (!forceUpdate) {
        // Check if baseline exists
        FILE *f = fopen(baselinePath, "rb");
        if (f) {
            fclose(f);
            // Capture current screenshot
            if (!captureFullViewport(ctx, currentPath))
                return -1;
            return compareImages(baselinePath, currentPath);
        }
    }

    // Save as baseline
    return captureFullViewport(ctx, baselinePath) ? 0 : -1;
}

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
    ctx->Yield(4); // Let imnodes draw and register nodes in object pool
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
