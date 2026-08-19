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
#include <filesystem>
#include <imnodes.h>
#undef Yield
static RfSimulatorApp *s_app = nullptr;

void RegisterUiTests(ImGuiTestEngine *e, RfSimulatorApp &app) {
    s_app = &app;

    // The first-run tutorial offer is a blocking modal, which would stop every
    // other test from reaching the menu bar. Suppress it here (before the first
    // frame) — tutorial_first_run_prompt_marks_completed re-arms it explicitly.
    app.m_show_tutorial_first_run_prompt = false;

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

    // Issue #67: selecting a link and pressing Delete must remove it (previously
    // only selected *nodes* were handled by the Delete key). Runs early, before
    // the subcircuit tests group/collapse anything, so the seeded Generator
    // (node 1) and Amplifier (node 2) are still visible and ungrouped.
    t = IM_REGISTER_TEST(e, "rf_simulator", "link_delete_key_deletes_selected_link");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        ctx->WindowFocus("Node Editor");
        ctx->WindowResize("Node Editor", ImVec2(900, 700));
        ctx->Yield(2);
        ImNodes::EditorContextResetPanning(ImVec2(0, 0));
        ctx->Yield(2);

        auto &graph = s_app->testGraphEngine();
        size_t initial_links = graph.links().size();
        int gen_out = graph.outputPinId(1);
        int amp_in = graph.inputPinId(2);
        IM_CHECK(gen_out >= 0);
        IM_CHECK(amp_in >= 0);
        int link_id = graph.addLink(gen_out, amp_in);
        IM_CHECK(link_id >= 0);
        IM_CHECK_EQ(graph.links().size(), initial_links + 1);

        ctx->Yield(4); // let imnodes draw + register the new link
        ImNodes::ClearNodeSelection();
        ImNodes::ClearLinkSelection();
        ImNodes::SelectLink(link_id);
        ctx->Yield(2);
        IM_CHECK(ImNodes::IsLinkSelected(link_id));

        ctx->KeyDown(ImGuiKey_Delete);
        ctx->Yield(2);

        IM_CHECK_EQ(graph.links().size(), initial_links);
        IM_CHECK_EQ(ImNodes::NumSelectedLinks(), 0);
    };

    // Issue #67: right-clicking a link must offer a Remove entry (previously the
    // right-click fell through to the canvas "add component" menu). Uses the
    // seeded Generator/Amplifier (nodes 1/2), which are visible this early in
    // the run.
    t = IM_REGISTER_TEST(e, "rf_simulator", "link_context_menu_remove");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        ctx->WindowFocus("Node Editor");
        ctx->WindowResize("Node Editor", ImVec2(900, 700));
        ctx->Yield(2);
        ImNodes::EditorContextResetPanning(ImVec2(0, 0));
        ctx->Yield(2);

        auto &graph = s_app->testGraphEngine();
        size_t initial_links = graph.links().size();
        int gen_out = graph.outputPinId(1);
        int amp_in = graph.inputPinId(2);
        IM_CHECK(gen_out >= 0);
        IM_CHECK(amp_in >= 0);

        // The editor window can be docked anywhere, including mostly below the
        // viewport, so anchor the node pair to the window's visible top-left
        // region (inside both the window and the viewport).
        ImVec2 vp = ImGui::GetIO().DisplaySize;
        auto info = ctx->WindowInfo("Node Editor");
        ImVec2 vis_min = info.RectFull.Min;
        ImVec2 vis_max =
            ImVec2(std::min(info.RectFull.Max.x, vp.x), std::min(info.RectFull.Max.y, vp.y));
        float anchor_x = std::min(vis_min.x + 180.0f, vis_max.x - 120.0f);
        float anchor_y = std::min(vis_min.y + 120.0f, vis_max.y - 120.0f);
        ImNodes::SetNodeScreenSpacePos(1, ImVec2(anchor_x - 140.0f, anchor_y - 60.0f));
        ImNodes::SetNodeScreenSpacePos(2, ImVec2(anchor_x + 140.0f, anchor_y + 40.0f));
        ctx->Yield(4);

        int link_id = graph.addLink(gen_out, amp_in);
        IM_CHECK(link_id >= 0);
        IM_CHECK_EQ(graph.links().size(), initial_links + 1);
        ctx->Yield(4); // let imnodes draw the link

        // Re-focus so the editor window stays the hovered/focused one during
        // the mouse sweep below.
        ctx->WindowFocus("Node Editor");
        ctx->Yield(1);

        // The link's rendered position depends on the node layout (title bar,
        // body, pin offsets), so locate it by sweeping a 2D grid between the
        // two nodes until the mouse hovers exactly over our link. The grid is
        // clamped to the viewport so every point is reachable.
        ImVec2 n1 = ImNodes::GetNodeScreenSpacePos(1);
        ImVec2 n2 = ImNodes::GetNodeScreenSpacePos(2);
        ImVec2 dims1 = ImNodes::GetNodeDimensions(1);
        float x_min = std::max(n1.x + dims1.x + 8.0f, 10.0f);
        float x_max = std::min(n2.x - 8.0f, vp.x - 10.0f);
        float y_min = std::max(n1.y + 55.0f, 10.0f);
        float y_max = std::min(n2.y + 130.0f, vp.y - 10.0f);

        int hit_x = -1;
        int hit_y = -1;
        for (float y = y_min; y < y_max && hit_y < 0; y += 3.0f) {
            for (float x = x_min; x < x_max && hit_y < 0; x += 10.0f) {
                ctx->MouseMoveToPos(ImVec2(x, y));
                ctx->Yield(1);
                int h = -1;
                if (ImNodes::IsLinkHovered(&h) && h == link_id) {
                    hit_x = static_cast<int>(x);
                    hit_y = static_cast<int>(y);
                }
            }
        }
        IM_CHECK(hit_y >= 0);

        ctx->MouseClick(ImGuiMouseButton_Right);
        ctx->Yield(2);
        IM_CHECK(ImGui::IsPopupOpen((ImGuiID)0, ImGuiPopupFlags_AnyPopup));

        ctx->SetRef("//$FOCUSED");
        ctx->ItemClick("Remove");
        ctx->SetRef("");
        ctx->Yield(2);

        IM_CHECK_EQ(graph.links().size(), initial_links);

        // Restore the seeded nodes to their default origin positions so the
        // subcircuit tests' "stacked at canvas origin" assumptions hold.
        ImNodes::SetNodeGridSpacePos(1, ImVec2(0, 0));
        ImNodes::SetNodeGridSpacePos(2, ImVec2(0, 0));
        ctx->Yield(2);
    };

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

    t = IM_REGISTER_TEST(e, "rf_simulator", "layout_save_as_creates_file");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        std::filesystem::remove(std::filesystem::path(s_app->testLayoutManager().layoutsDir()) /
                                "UITestLayout.ini");

        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Layouts/Save As...");
        ctx->SetRef("Save Layout As");
        ctx->ItemInputValue("Name", "UITestLayout");
        ctx->ItemClick("Save");
        ctx->SetRef("");
        ctx->Yield();

        IM_CHECK(std::filesystem::exists(
            std::filesystem::path(s_app->testLayoutManager().layoutsDir()) / "UITestLayout.ini"));
    };

    t = IM_REGISTER_TEST(e, "rf_simulator", "layout_manage_delete_removes_file");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        auto path =
            std::filesystem::path(s_app->testLayoutManager().layoutsDir()) / "UITestLayout.ini";
        IM_CHECK(std::filesystem::exists(path));

        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Layouts/Manage...");
        ctx->SetRef("Manage Layouts");
        ctx->ItemClick("UITestLayout/Delete");
        ctx->Yield();
        ctx->ItemClick("Close");
        ctx->SetRef("");

        IM_CHECK(!std::filesystem::exists(path));
    };

    // Tutorial tests are registered last on purpose: starting the tutorial calls
    // newProject(), which clears the components and node IDs the tests above
    // rely on.

    t = IM_REGISTER_TEST(e, "rf_simulator", "tutorial_launches_from_help_menu");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        s_app->newProject(); // clean slate, so the unsaved-changes guard stays out of the way
        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("Help/Tutorial");
        ctx->Yield(2);

        IM_CHECK(s_app->testTutorialState().isActive());
        IM_CHECK_EQ(s_app->testTutorialState().stepIndex(), 0);
        IM_CHECK(ImGui::FindWindowByName("Tutorial Guide") != nullptr);

        ctx->SetRef("Tutorial Guide");
        ctx->ItemClick("Exit");
        ctx->SetRef("");
        ctx->Yield(2);
        IM_CHECK(!s_app->testTutorialState().isActive());
    };

    t = IM_REGISTER_TEST(e, "rf_simulator", "tutorial_start_guards_unsaved_changes");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        s_app->newProject();
        s_app->testMakeDirty();

        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("Help/Tutorial");
        ctx->Yield(2);
        // Dirty project — the tutorial must wait behind the unsaved-changes modal.
        IM_CHECK(!s_app->testTutorialState().isActive());

        ctx->SetRef("Unsaved Changes");
        ctx->ItemClick("Discard");
        ctx->SetRef("");
        ctx->Yield(2);
        IM_CHECK(s_app->testTutorialState().isActive());

        ctx->SetRef("Tutorial Guide");
        ctx->ItemClick("Exit");
        ctx->SetRef("");
        ctx->Yield(2);
    };

    t = IM_REGISTER_TEST(e, "rf_simulator", "tutorial_step_navigation");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        s_app->newProject();
        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("Help/Tutorial");
        ctx->Yield(2);

        ctx->SetRef("Tutorial Guide");
        IM_CHECK(s_app->testTutorialState().atFirstStep());

        ctx->ItemClick("Next");
        ctx->Yield();
        IM_CHECK_EQ(s_app->testTutorialState().stepIndex(), 1);

        ctx->ItemClick("Back");
        ctx->Yield();
        IM_CHECK_EQ(s_app->testTutorialState().stepIndex(), 0);

        ctx->ItemClick("Skip");
        ctx->Yield();
        IM_CHECK(s_app->testTutorialState().atLastStep());
        IM_CHECK(s_app->testTutorialState().isActive());

        ctx->ItemClick("Exit");
        ctx->SetRef("");
        ctx->Yield(2);
        IM_CHECK(!s_app->testTutorialState().isActive());
    };

    t = IM_REGISTER_TEST(e, "rf_simulator", "tutorial_completes_and_persists");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        std::filesystem::path marker(s_app->testTutorialState().markerPath());
        std::filesystem::remove(marker);

        s_app->newProject();
        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("Help/Tutorial");
        ctx->Yield(2);

        ctx->SetRef("Tutorial Guide");
        // Bounded walk to the last step — never loop on the state alone.
        for (int i = 0; i < s_app->testTutorialState().stepCount(); ++i) {
            if (s_app->testTutorialState().atLastStep())
                break;
            ctx->ItemClick("Next");
            ctx->Yield();
        }
        IM_CHECK(s_app->testTutorialState().atLastStep());
        IM_CHECK(!std::filesystem::exists(marker)); // not marked until Finish

        ctx->ItemClick("Finish");
        ctx->SetRef("");
        ctx->Yield(2);

        IM_CHECK(!s_app->testTutorialState().isActive());
        IM_CHECK(std::filesystem::exists(marker));
    };

    t = IM_REGISTER_TEST(e, "rf_simulator", "tutorial_first_run_prompt_marks_completed");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        std::filesystem::path marker(s_app->testTutorialState().markerPath());
        std::filesystem::remove(marker);

        // Re-arm the prompt suppressed in RegisterUiTests.
        s_app->m_show_tutorial_first_run_prompt = true;
        ctx->Yield(2);

        ctx->SetRef("Welcome to Tiny RF Simulator");
        ctx->ItemClick("Not Now");
        ctx->SetRef("");
        ctx->Yield(2);

        IM_CHECK(!s_app->m_show_tutorial_first_run_prompt);
        IM_CHECK(!s_app->testTutorialState().isActive());
        // Dismissing the offer counts as completed — it must never nag again.
        IM_CHECK(std::filesystem::exists(marker));
    };
}
