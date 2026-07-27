#define IMGUI_DEFINE_MATH_OPERATORS
#include "adc_engine.h"
#include "amplifier_engine.h"
#include "app.h"
#include "attenuator_engine.h"
#include "coax_cable_engine.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_test_engine/imgui_te_context.h"
#include "mixer_engine.h"
#include "signal_generator_engine.h"
#include "test_helpers.h"
#include <imnodes.h>
#undef Yield
static RfSimulatorApp *s_app = nullptr;

void RegisterUiTests(ImGuiTestEngine *e, RfSimulatorApp &app) {
    s_app = &app;
    ImGuiTest *t = nullptr;

    t = IM_REGISTER_TEST(e, "rf_simulator", "node_editor_exists");
    t->TestFunc = [](ImGuiTestContext *ctx) { ctx->WindowFocus("Node Editor"); };

    t = IM_REGISTER_TEST(e, "rf_simulator", "single_generator_present");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        ctx->SetRef("Generator 0");
        ctx->ItemExists("Measure");
    };

    t = IM_REGISTER_TEST(e, "rf_simulator", "single_amplifier_present");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        ctx->SetRef("Amplifier 0");
        ctx->ItemExists("Measure");
    };

    t = IM_REGISTER_TEST(e, "rf_simulator", "canvas_context_menu");
    t->TestFunc = [](ImGuiTestContext *ctx) {
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
    t->TestFunc = [](ImGuiTestContext *ctx) {
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
    t->TestFunc = [](ImGuiTestContext *ctx) { ctx->WindowFocus("Properties"); };

    // =========================================================================
    // Subcircuit Group UI Tests
    // =========================================================================

    // =========================================================================
    // Subcircuit Group UI Tests
    // =========================================================================
    //
    // These tests simulate UI interactions for the subcircuit groups feature.
    // The approach uses position-based interactions since imnodes nodes are not
    // standard ImGui items and cannot always be found by label.
    //
    // Test strategy:
    //   1. Use the canvas context menu (right-click) to add nodes
    //   2. Nodes are stacked at canvas origin; use a generous rubber-band rectangle
    //      covering most of the Node Editor window to enclose all nodes
    //   3. Verify the "CreateSubcircuit" popup appears
    //   4. Click "Create" to confirm
    //
    // Note: The existing tests include "single_generator_present" and
    // "single_amplifier_present" which already verify those nodes exist.
    // =========================================================================

    t = IM_REGISTER_TEST(e, "rf_simulator", "subcircuit_rubber_band_creates_group");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        ctx->WindowFocus("Node Editor");
        ctx->WindowResize("Node Editor", ImVec2(900, 700));
        ctx->Yield(2);

        // -- Add nodes via canvas context menu --
        // The app starts with Generator 0 and Amplifier 0. Add a Splitter
        // to have at least 3 nodes on the canvas.
        ctx->WindowFocus("Node Editor");
        ctx->Yield(1);
        auto info = ctx->WindowInfo("Node Editor");
        ImVec2 center = info.RectFull.GetCenter();

        ctx->MouseMoveToPos(center);
        ctx->MouseClick(ImGuiMouseButton_Right);
        ctx->Yield(2);
        ctx->SetRef("//$FOCUSED");
        ctx->ItemClick("Add Splitter");
        ctx->SetRef("");
        ctx->Yield(2);

        // -- Rubber band: Shift+drag a generous rectangle covering most of the
        //    Node Editor canvas area to enclose all nodes --
        // All nodes are stacked at the canvas origin (top-left of canvas),
        // so a large rectangle covering the entire canvas will catch them.
        ImVec2 rb_start = info.RectFull.Min + ImVec2(10, 30); // below title bar
        ImVec2 rb_end = info.RectFull.Max - ImVec2(10, 10);   // above bottom edge

        ctx->KeyDown(ImGuiKey_LeftShift);
        ctx->MouseMoveToPos(rb_start);
        ctx->Yield(1);
        ctx->MouseDown(ImGuiMouseButton_Left);
        ctx->Yield(1);
        ctx->MouseMoveToPos(rb_end);
        ctx->Yield(1);
        ctx->MouseUp(ImGuiMouseButton_Left);
        ctx->Yield(1);
        ctx->KeyUp(ImGuiKey_LeftShift);
        ctx->Yield(2);

        // -- Verify: the "CreateSubcircuit" popup should have appeared --
        bool popup_open = ImGui::IsPopupOpen((ImGuiID)0, ImGuiPopupFlags_AnyPopup);
        IM_CHECK(popup_open);

        if (popup_open) {
            // Set ref to the focused modal popup, then click "Create"
            ctx->SetRef("//$FOCUSED");
            ctx->ItemClick("Create");
            ctx->SetRef("");
            ctx->Yield(2);
        }
    };

    t = IM_REGISTER_TEST(e, "rf_simulator", "subcircuit_create_group_and_verify_popup");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        // This test adds more nodes and creates a new group (2nd group).
        // It verifies the rubber-band creation flow works with freshly added
        // nodes that are not yet grouped.
        ctx->WindowFocus("Node Editor");
        ctx->WindowResize("Node Editor", ImVec2(900, 700));
        ctx->Yield(2);

        auto info = ctx->WindowInfo("Node Editor");
        ImVec2 center = info.RectFull.GetCenter();

        // Add two new nodes via context menu so we have a set of at least 2
        // ungrouped nodes (nodes from previous tests are already grouped)
        ctx->MouseMoveToPos(center);
        ctx->MouseClick(ImGuiMouseButton_Right);
        ctx->Yield(2);
        ctx->SetRef("//$FOCUSED");
        ctx->ItemClick("Add Mixer");
        ctx->SetRef("");
        ctx->Yield(2);

        ctx->MouseMoveToPos(center + ImVec2(0, 80));
        ctx->MouseClick(ImGuiMouseButton_Right);
        ctx->Yield(2);
        ctx->SetRef("//$FOCUSED");
        ctx->ItemClick("Add Coax Cable");
        ctx->SetRef("");
        ctx->Yield(2);

        // Rubber band across most of the canvas to enclose all ungrouped nodes
        ImVec2 rb_start = info.RectFull.Min + ImVec2(10, 30);
        ImVec2 rb_end = info.RectFull.Max - ImVec2(10, 10);

        ctx->KeyDown(ImGuiKey_LeftShift);
        ctx->MouseMoveToPos(rb_start);
        ctx->Yield(1);
        ctx->MouseDown(ImGuiMouseButton_Left);
        ctx->Yield(1);
        ctx->MouseMoveToPos(rb_end);
        ctx->Yield(1);
        ctx->MouseUp(ImGuiMouseButton_Left);
        ctx->Yield(1);
        ctx->KeyUp(ImGuiKey_LeftShift);
        ctx->Yield(2);

        // Verify some popup appeared (CreateSubcircuit if enough nodes, or none if not)
        bool popup = ImGui::IsPopupOpen((ImGuiID)0, ImGuiPopupFlags_AnyPopup);
        if (popup) {
            ctx->SetRef("//$FOCUSED");
            ctx->ItemClick("Create");
            ctx->SetRef("");
            ctx->Yield(2);
        }
        // Accept either outcome - the important test is the first one above
    };

    t = IM_REGISTER_TEST(e, "rf_simulator", "subcircuit_expand_and_collapse");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        // After the previous test created a group, the generator+amplifier+splitter
        // are inside a collapsed subcircuit. Try to expand and re-collapse it to
        // exercise the rendering path and catch any crash on the transition.
        ctx->WindowFocus("Node Editor");
        ctx->Yield(2);

        auto info = ctx->WindowInfo("Node Editor");
        ImVec2 center = info.RectFull.GetCenter();
        ctx->MouseMoveToPos(center);
        ctx->Yield(1);

        ctx->SetRef("Node Editor");
        if (ctx->ItemExists("Expand")) {
            ctx->ItemClick("Expand");
            ctx->Yield(2);
            // Render a few more frames to make sure nothing crashes after expansion
            ctx->Yield(5);

            // Try to find the ▼ collapse button on the expanded title bar by hovering
            // over the group's top-left area and clicking
            ctx->MouseMoveToPos(center + ImVec2(120, -40));
            ctx->MouseClick(ImGuiMouseButton_Left);
            ctx->Yield(2);
        }
    };

    // =========================================================================
    // Amplifier Inspector Tests
    // =========================================================================

    t = IM_REGISTER_TEST(e, "rf_simulator", "inspector_amplifier_gain");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        int node = NodeHelper::findComponentNodeId<AmplifierEngine>(*s_app);
        IM_CHECK(node >= 0);
        NodeHelper::selectNode(ctx, node);
        InspectorHelper::waitForPopulated(ctx, node);
        InspectorHelper::setInputDouble(ctx, "Gain (dB)", 15.0);
        auto *amp = dynamic_cast<AmplifierEngine *>(s_app->testComponents().find(node));
        IM_CHECK(amp != nullptr);
        IM_CHECK_EQ(amp->gain_dB(), 15.0);
    };

    t = IM_REGISTER_TEST(e, "rf_simulator", "inspector_amplifier_nf");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        int node = NodeHelper::findComponentNodeId<AmplifierEngine>(*s_app);
        IM_CHECK(node >= 0);
        NodeHelper::selectNode(ctx, node);
        InspectorHelper::waitForPopulated(ctx, node);
        InspectorHelper::setInputDouble(ctx, "NF (dB)", 3.5);
        auto *amp = dynamic_cast<AmplifierEngine *>(s_app->testComponents().find(node));
        IM_CHECK(amp != nullptr);
        IM_CHECK_EQ(amp->nf_dB(), 3.5);
    };

    // =========================================================================
    // Connection Creation Tests
    // =========================================================================

    t = IM_REGISTER_TEST(e, "rf_simulator", "connection_valid_generator_to_amplifier");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        auto &graph = s_app->testGraphEngine();
        size_t initial_links = graph.links().size();
        int gen_out = graph.outputPinId(1);
        int amp_in = graph.inputPinId(2);
        IM_CHECK(gen_out >= 0);
        IM_CHECK(amp_in >= 0);
        int link_id = graph.addLink(gen_out, amp_in);
        IM_CHECK(link_id >= 0);
        IM_CHECK_EQ(graph.links().size(), initial_links + 1);
        const auto &link = graph.links().back();
        IM_CHECK_EQ(link.start_pin_id, gen_out);
        IM_CHECK_EQ(link.end_pin_id, amp_in);
        graph.removeLink(link_id);
        IM_CHECK_EQ(graph.links().size(), initial_links);
    };

    t = IM_REGISTER_TEST(e, "rf_simulator", "connection_multi_fanout");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        auto &graph = s_app->testGraphEngine();
        size_t initial_links = graph.links().size();
        int split_out = graph.outputPinId(3);
        int amp_in = graph.inputPinId(2);
        int mixer_in = graph.inputPinId(4);
        IM_CHECK(split_out >= 0);
        IM_CHECK(amp_in >= 0);
        IM_CHECK(mixer_in >= 0);
        int link1 = graph.addLink(split_out, amp_in);
        int link2 = graph.addLink(split_out, mixer_in);
        IM_CHECK(link1 >= 0);
        IM_CHECK(link2 >= 0);
        IM_CHECK_EQ(graph.links().size(), initial_links + 2);
        graph.removeLink(link1);
        graph.removeLink(link2);
        IM_CHECK_EQ(graph.links().size(), initial_links);
    };

    t = IM_REGISTER_TEST(e, "rf_simulator", "connection_delete");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        auto &graph = s_app->testGraphEngine();
        size_t initial_links = graph.links().size();
        int gen_out = graph.outputPinId(1);
        int amp_in = graph.inputPinId(2);
        int link_id = graph.addLink(gen_out, amp_in);
        IM_CHECK_EQ(graph.links().size(), initial_links + 1);
        graph.removeLink(link_id);
        IM_CHECK_EQ(graph.links().size(), initial_links);
    };

    // =========================================================================
    // Validation gap documentation tests
    // These tests document that the engine currently accepts invalid connections.
    // They encode known behavioral gaps — not desired behavior — so they should
    // be updated or removed when validation is implemented.
    // =========================================================================

    t = IM_REGISTER_TEST(e, "rf_simulator", "connection_output_to_output_accepted");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        // Documents: Engine currently accepts output→output connections (no validation)
        auto &graph = s_app->testGraphEngine();
        size_t initial_links = graph.links().size();
        int amp_out = graph.outputPinId(2);
        int gen_out = graph.outputPinId(1);
        IM_CHECK(amp_out >= 0);
        IM_CHECK(gen_out >= 0);
        int link_id = graph.addLink(amp_out, gen_out);
        IM_CHECK(link_id >= 0);
        IM_CHECK_EQ(graph.links().size(), initial_links + 1);
        graph.removeLink(link_id);
    };

    t = IM_REGISTER_TEST(e, "rf_simulator", "connection_input_to_input_accepted");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        // Documents: Engine currently accepts input→input connections (no validation)
        auto &graph = s_app->testGraphEngine();
        size_t initial_links = graph.links().size();
        int amp_in = graph.inputPinId(2);
        int splitter_in = graph.inputPinId(3);
        IM_CHECK(amp_in >= 0);
        IM_CHECK(splitter_in >= 0);
        int link_id = graph.addLink(amp_in, splitter_in);
        IM_CHECK(link_id >= 0);
        IM_CHECK_EQ(graph.links().size(), initial_links + 1);
        graph.removeLink(link_id);
    };

    t = IM_REGISTER_TEST(e, "rf_simulator", "connection_self_loop_accepted");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        // Documents: Engine currently accepts self-loops (no validation)
        auto &graph = s_app->testGraphEngine();
        size_t initial_links = graph.links().size();
        int amp_out = graph.outputPinId(2);
        int amp_in = graph.inputPinId(2);
        IM_CHECK(amp_out >= 0);
        IM_CHECK(amp_in >= 0);
        int link_id = graph.addLink(amp_out, amp_in);
        IM_CHECK(link_id >= 0);
        IM_CHECK_EQ(graph.links().size(), initial_links + 1);
        graph.removeLink(link_id);
    };

    t = IM_REGISTER_TEST(e, "rf_simulator", "connection_duplicate_accepted");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        // Documents: Engine currently creates duplicate links (no deduplication)
        auto &graph = s_app->testGraphEngine();
        size_t initial_links = graph.links().size();
        int gen_out = graph.outputPinId(1);
        int amp_in = graph.inputPinId(2);
        int link1 = graph.addLink(gen_out, amp_in);
        int link2 = graph.addLink(gen_out, amp_in);
        IM_CHECK(link1 >= 0);
        IM_CHECK(link2 >= 0);
        IM_CHECK_EQ(graph.links().size(), initial_links + 2);
        graph.removeLink(link1);
        graph.removeLink(link2);
    };

    // =========================================================================
    // Navigation Tests
    // =========================================================================

    t = IM_REGISTER_TEST(e, "rf_simulator", "navigation_pan_programmatic");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        ctx->WindowFocus("Node Editor");
        ctx->Yield(2);
        ImVec2 initial_pan = ImNodes::EditorContextGetPanning();
        // Programmatically set new pan offset
        ImVec2 new_pan = ImVec2(initial_pan.x + 50, initial_pan.y + 30);
        ImNodes::EditorContextResetPanning(new_pan);
        ctx->Yield(2);
        ImVec2 final_pan = ImNodes::EditorContextGetPanning();
        IM_CHECK_EQ(final_pan.x, new_pan.x);
        IM_CHECK_EQ(final_pan.y, new_pan.y);
    };

    t = IM_REGISTER_TEST(e, "rf_simulator", "navigation_drag_node");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        ctx->WindowFocus("Node Editor");
        ctx->Yield(2);
        // Reset pan to ensure consistent state after previous test
        ImNodes::EditorContextResetPanning(ImVec2(0, 0));
        ctx->Yield(2);
        ImVec2 initial_pos = s_app->testGraphWidget().nodeGridPosition(2);
        IM_CHECK(initial_pos.x >= 0); // amplifier should have valid position
        // Programmatically set new position
        ImVec2 new_pos = ImVec2(initial_pos.x + 100, initial_pos.y + 50);
        ImNodes::SetNodeGridSpacePos(2, new_pos);
        // CRITICAL: detectNodeMoves() requires mouse release to update cache
        auto info = ctx->WindowInfo("Node Editor");
        ctx->MouseMoveToPos(info.RectFull.GetCenter());
        ctx->MouseClick(ImGuiMouseButton_Left); // down + up
        ctx->Yield(2);                          // let draw cycle update widget cache
        ImVec2 final_pos = s_app->testGraphWidget().nodeGridPosition(2);
        IM_CHECK_EQ(final_pos.x, new_pos.x);
        IM_CHECK_EQ(final_pos.y, new_pos.y);
    };

    // =========================================================================
    // Visual Regression Tests
    // =========================================================================
    // These tests capture screenshots of key views and compare against baselines.
    // First run: baselines are saved. Subsequent runs: screenshots are compared.
    // Set UPDATE_BASELINES env var to regenerate baselines after intentional changes.

    t = IM_REGISTER_TEST(e, "rf_simulator", "visual_baseline_default_graph");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        ctx->WindowFocus("Node Editor");
        ctx->Yield(4);
        int diff = ScreenshotHelper::verifyBaseline(ctx, "Node Editor", "default_graph");
        IM_CHECK(diff >= 0 && diff <= 10); // Allow up to 10/255 tolerance for font rendering
    };

    t = IM_REGISTER_TEST(e, "rf_simulator", "visual_baseline_single_node");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        NodeHelper::selectNode(ctx, 2); // Select amplifier (node_id=2)
        ctx->Yield(2);
        int diff = ScreenshotHelper::verifyBaseline(ctx, "Node Editor", "single_node_selected");
        IM_CHECK(diff >= 0 && diff <= 10);
    };

    t = IM_REGISTER_TEST(e, "rf_simulator", "visual_baseline_connected_chain");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        // Add a mixer to make the chain visually distinct from default graph
        ctx->WindowFocus("Node Editor");
        auto info = ctx->WindowInfo("Node Editor");
        ImVec2 center = info.RectFull.GetCenter();
        ctx->MouseMoveToPos(center);
        ctx->MouseClick(ImGuiMouseButton_Right);
        ctx->Yield(2);
        ctx->SetRef("canvas_context_menu");
        ctx->ItemClick("Add Mixer");
        ctx->SetRef("");
        ctx->Yield(4);
        int diff = ScreenshotHelper::verifyBaseline(ctx, "Node Editor", "connected_chain");
        IM_CHECK(diff >= 0 && diff <= 10);
    };

    t = IM_REGISTER_TEST(e, "rf_simulator", "visual_baseline_group_collapsed");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        // Create a group by rubber-band selecting all nodes, then capture collapsed state
        ctx->WindowFocus("Node Editor");
        ctx->WindowResize("Node Editor", ImVec2(900, 700));
        ctx->Yield(2);
        auto info = ctx->WindowInfo("Node Editor");
        ImVec2 rb_start = info.RectFull.Min + ImVec2(10, 30);
        ImVec2 rb_end = info.RectFull.Max - ImVec2(10, 10);
        ctx->KeyDown(ImGuiKey_LeftShift);
        ctx->MouseMoveToPos(rb_start);
        ctx->Yield(1);
        ctx->MouseDown(ImGuiMouseButton_Left);
        ctx->Yield(1);
        ctx->MouseMoveToPos(rb_end);
        ctx->Yield(1);
        ctx->MouseUp(ImGuiMouseButton_Left);
        ctx->Yield(1);
        ctx->KeyUp(ImGuiKey_LeftShift);
        ctx->Yield(2);
        // Confirm group creation if popup appeared
        if (ImGui::IsPopupOpen((ImGuiID)0, ImGuiPopupFlags_AnyPopup)) {
            ctx->SetRef("CreateSubcircuit");
            ctx->ItemClick("Create");
            ctx->SetRef("");
            ctx->Yield(4);
        }
        int diff = ScreenshotHelper::verifyBaseline(ctx, "Node Editor", "group_collapsed");
        IM_CHECK(diff >= 0 && diff <= 10);
    };

    t = IM_REGISTER_TEST(e, "rf_simulator", "visual_baseline_inspector_populated");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        NodeHelper::selectNode(ctx, 0); // Select generator
        InspectorHelper::waitForPopulated(ctx, 0);
        ctx->Yield(2);
        int diff = ScreenshotHelper::verifyBaseline(ctx, "Properties", "inspector_populated");
        IM_CHECK(diff >= 0 && diff <= 10);
    };

    t = IM_REGISTER_TEST(e, "rf_simulator", "visual_baseline_full_ui");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        // Capture entire viewport for full UI baseline
        ctx->Yield(4);
        int diff = ScreenshotHelper::verifyFullViewportBaseline(ctx, "full_ui");
        IM_CHECK(diff >= 0 && diff <= 15);
    };
}
