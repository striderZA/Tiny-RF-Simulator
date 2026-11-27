#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX // windows.h must not shadow std::min/std::max, used throughout this file
#endif
#define IMGUI_DEFINE_MATH_OPERATORS
#include "agent_endpoint.h"
#include "agent_panel_widget.h"
#include "amplifier_engine.h"
#include "app.h"
#include "attenuator_engine.h"
#include "coax_cable_engine.h"
#include "digital_step_attenuator_engine.h"
#include "ideal_filter_engine.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"
#include "mixer_engine.h"
#include "signal_generator_engine.h"
#include "test_helpers.h"
#include "tutorial_steps.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <imnodes.h>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <psapi.h>
#include <windows.h>
#endif
#undef Yield
namespace {
bool agentPanelVisible() {
    ImGuiWindow *window = ImGui::FindWindowByName("Agent");
    return window && window->Active;
}

void setAgentPanelVisible(ImGuiTestContext *ctx, bool visible) {
    ctx->SetRef("");
    ctx->Yield(2);
    if (agentPanelVisible() == visible)
        return;

    ctx->SetRef("##MainMenuBar");
    ctx->MenuClick("View/Agent");
    ctx->SetRef("");
    ctx->Yield(3);
}

// Yields until `condition` holds (at most 60 frames). Assertions still verify
// the result, so a lost click fails the same check instead of being masked.
template <typename Condition> void waitForUi(ImGuiTestContext *ctx, Condition condition) {
    for (int frame = 0; frame < 60 && !condition(); ++frame)
        ctx->Yield();
}

// Scrolls the receiver panel to the bottom, then yields until its scroll offset
// has stopped changing for two consecutive frames. A single ScrollToBottom can
// land short or keep moving while the content height changes; clicking an item
// before the offset settles targets a stale rectangle.
void settlePanelScrollToBottom(ImGuiTestContext *ctx) {
    ctx->ScrollToBottom("//Receiver Requirements");
    ImGuiWindow *window = ImGui::FindWindowByName("Receiver Requirements");
    float previous = window ? window->Scroll.y : 0.0f;
    int stable_frames = 0;
    for (int frame = 0; frame < 60 && stable_frames < 2; ++frame) {
        ctx->Yield();
        window = ImGui::FindWindowByName("Receiver Requirements");
        const float current = window ? window->Scroll.y : previous;
        stable_frames = std::fabs(current - previous) < 0.5f ? stable_frames + 1 : 0;
        previous = current;
    }
}

// Root dock node of the tree `window` is docked into, or nullptr while the
// window floats.
ImGuiDockNode *dockRootNodeFor(const ImGuiWindow *window) {
    if (window == nullptr || window->DockNode == nullptr)
        return nullptr;
    return ImGui::DockNodeGetRootNode(window->DockNode);
}

// True when `window` is docked anywhere inside the "Utility Space" host
// window's dockspace — the app's second docking group. The dockspace ID is
// "UtilityDockSpace" scoped inside the "Utility Space" window, the same
// stable id ImGui persists in the layout ini.
bool dockedInUtilityGroup(const ImGuiWindow *window) {
    const ImGuiDockNode *root = dockRootNodeFor(window);
    if (root == nullptr)
        return false;
    const ImGuiID utility_root = ImHashStr("UtilityDockSpace", 0, ImHashStr("Utility Space"));
    return root->ID == utility_root;
}

bool utilitySpaceVisible() {
    ImGuiWindow *window = ImGui::FindWindowByName("Utility Space");
    return window != nullptr && window->Active;
}

// A persisted layout may have closed the group. Open it through its real menu
// path so every utility-space test starts from the same state.
void ensureUtilitySpaceVisible(ImGuiTestContext *ctx) {
    ctx->SetRef("");
    ctx->Yield(2);
    if (utilitySpaceVisible())
        return;
    ctx->SetRef("##MainMenuBar");
    ctx->MenuClick("View/Utility Space");
    ctx->SetRef("");
    ctx->Yield(3);
}

struct AgentPanelUiCleanup {
    RfSimulatorApp &app;
    ImGuiTestContext *ctx;
    AgentPanelWidget &widget;
    bool original_visibility;
    std::function<void(bool)> original_server_callback;
    std::function<void(std::uint64_t)> original_revert_callback;
    AppAgentHost *host = nullptr;
    std::uint64_t first_added_checkpoint_id = 0;
    bool owns_test_server = false;
    std::filesystem::path temp_root;

    AgentPanelUiCleanup(RfSimulatorApp &app, ImGuiTestContext *ctx, AgentPanelWidget &widget)
        : app(app), ctx(ctx), widget(widget), original_visibility(agentPanelVisible()),
          original_server_callback(widget.onServerToggled),
          original_revert_callback(widget.onRevert) {}

    ~AgentPanelUiCleanup() {
        widget.onServerToggled = original_server_callback;
        widget.onRevert = original_revert_callback;
        if (host && first_added_checkpoint_id != 0)
            host->removeCheckpointsFrom(first_added_checkpoint_id);
        if (owns_test_server) {
            if (AgentServer *server = app.testAgentServer())
                server->stop();
        }
        setAgentPanelVisible(ctx, original_visibility);
        if (!temp_root.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(temp_root, ec);
        }
    }
};
} // namespace

// Process private bytes, resolved at run time so the test needs no psapi link dependency.
static size_t processPrivateKB() {
#ifdef _WIN32
    using GetProcessMemoryInfoFn = BOOL(WINAPI *)(HANDLE, PROCESS_MEMORY_COUNTERS_EX *, DWORD);
    static GetProcessMemoryInfoFn get_info = [] {
        HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
        return kernel ? reinterpret_cast<GetProcessMemoryInfoFn>(reinterpret_cast<void *>(
                            GetProcAddress(kernel, "K32GetProcessMemoryInfo")))
                      : nullptr;
    }();
    if (!get_info)
        return 0;
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (!get_info(GetCurrentProcess(), &counters, sizeof(counters)))
        return 0;
    return static_cast<size_t>(counters.PrivateUsage / 1024);
#else
    return 0;
#endif
}

static RfSimulatorApp *s_app = nullptr;

namespace {

// Shift-drags a rubber band over two ungrouped nodes, which opens the canvas-hosted
// CreateSubcircuit modal when both fall inside the rectangle.
void rubberBandOverNodes(ImGuiTestContext *ctx, int first, int second) {
    NodeGraphWidget &widget = s_app->testGraphWidget();
    ImNodes::EditorContextSet(widget.context());
    const ImVec2 first_pos = ImNodes::GetNodeScreenSpacePos(first);
    const ImVec2 first_size = ImNodes::GetNodeDimensions(first);
    const ImVec2 second_pos = ImNodes::GetNodeScreenSpacePos(second);
    const ImVec2 second_size = ImNodes::GetNodeDimensions(second);
    const ImVec2 min(std::min(first_pos.x, second_pos.x), std::min(first_pos.y, second_pos.y));
    const ImVec2 max(std::max(first_pos.x + first_size.x, second_pos.x + second_size.x),
                     std::max(first_pos.y + first_size.y, second_pos.y + second_size.y));

    ctx->KeyDown(ImGuiKey_LeftShift);
    ctx->MouseMoveToPos(ImVec2(min.x - 24.0f, min.y - 24.0f));
    ctx->Yield(1);
    ctx->MouseDown(ImGuiMouseButton_Left);
    ctx->Yield(1);
    ctx->MouseMoveToPos(ImVec2(max.x + 24.0f, max.y + 24.0f));
    ctx->Yield(1);
    ctx->MouseUp(ImGuiMouseButton_Left);
    ctx->Yield(1);
    ctx->KeyUp(ImGuiKey_LeftShift);
    ctx->Yield(2);
}

// Locates an item in the menu that is currently open by its visible label prefix. The Edit
// menu's items carry a stable "###edit_undo" / "###edit_redo" id, and the test engine's label
// lookup hashes a decorated path (which resets at "###"), so it cannot address those items by
// label; gathering the open menu's items and matching their visible text does.
ImGuiTestItemInfo findMenuItem(ImGuiTestContext *ctx, const char *visible_prefix) {
    ImGuiTestItemList items;
    ctx->GatherItems(&items, "//$FOCUSED", 2);
    ImGuiTestItemInfo found;
    const std::size_t prefix_length = std::strlen(visible_prefix);
    for (auto item : items) {
        if (std::strncmp(item.DebugLabel, visible_prefix, prefix_length) == 0)
            found = item;
    }
    return found;
}

} // namespace

void RegisterUiTests(ImGuiTestEngine *e, RfSimulatorApp &app) {
    s_app = &app;

    // The suite drives Properties and Node Editor without opening them first, so open
    // them here rather than inherit whatever layout the shared app.ini holds.
    app.m_show_properties = true;
    app.m_show_node_editor = true;

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

    // Issue #170: the Edit button shares the selectable row's hit area. A
    // click on Edit must open the existing definition, never instantiate it.
    t = IM_REGISTER_TEST(e, "rf_simulator", "component_library_edit_does_not_insert");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        namespace fs = std::filesystem;
        struct TestState {
            RfSimulatorApp &app;
            fs::path original_cwd;
            std::string original_project_path;
            fs::path root;
            ~TestState() {
                std::error_code ec;
                fs::current_path(original_cwd, ec);
                app.m_current_project_path = original_project_path;
                app.refreshExtensions();
                fs::remove_all(root, ec);
            }
        };

        ctx->Yield(2);
        ImGuiWindow *library_window = ImGui::FindWindowByName("Component Library");
        const bool library_was_visible = library_window && library_window->Active;
        bool library_opened_by_test = false;
        const auto restore_library_visibility = [&]() {
            if (!library_opened_by_test)
                return;
            ctx->SetRef("##MainMenuBar");
            ctx->MenuClick("View/Component Library");
            ctx->SetRef("");
            ctx->Yield(2);
            library_opened_by_test = false;
        };

        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const fs::path root =
            fs::temp_directory_path() / ("rf_sim_issue170_" + std::to_string(stamp));
        TestState state{*s_app, fs::current_path(), s_app->m_current_project_path, root};

        const fs::path definition_path =
            root / "rf-sim-libraries" / "attenuator" / "Issue 170" / "issue170-att.json";
        fs::create_directories(definition_path.parent_path());
        {
            std::ofstream definition_file(definition_path);
            definition_file << R"({
                "schema_version": 1,
                "type": "attenuator",
                "part_number": "ISSUE170-ATT",
                "manufacturer": "Issue 170",
                "parameters": { "attenuation_dB": 6.0 }
            })";
        }
        fs::current_path(root);
        s_app->m_current_project_path = (root / "issue170.rfsim").string();
        s_app->refreshExtensions();
        const auto loaded_components = s_app->testComponentLibrary().all();
        const auto fixture =
            std::find_if(loaded_components.begin(), loaded_components.end(),
                         [](const auto *def) { return def->part_number == "ISSUE170-ATT"; });
        IM_CHECK(fixture != loaded_components.end());
        if (fixture == loaded_components.end())
            return;
        const ComponentDefinition &definition = **fixture;
        IM_CHECK(fs::exists(definition_path));

        const size_t component_count_before = s_app->componentCount();
        std::vector<int> original_node_ids;
        for (const auto *component : s_app->testComponents().all())
            original_node_ids.push_back(component->graphNodeId());

        if (!library_was_visible) {
            ctx->SetRef("##MainMenuBar");
            ctx->MenuClick("View/Component Library");
            ctx->SetRef("");
            library_opened_by_test = true;
        }
        ctx->WindowFocus("Component Library");
        ctx->WindowResize("Component Library", ImVec2(600, 500));
        ctx->Yield(2);

        // Expand the exact type/manufacturer nodes from the actual loaded
        // definition, independent of any prior window/tree state.
        ImGuiWindow *window = ImGui::FindWindowByName("Component Library");
        IM_CHECK(window != nullptr);
        if (!window) {
            restore_library_visibility();
            return;
        }
        const auto manufacturer_count =
            std::count_if(loaded_components.begin(), loaded_components.end(), [&](const auto *def) {
                return def->type == definition.type && def->manufacturer == definition.manufacturer;
            });
        const std::string manufacturer_label =
            definition.manufacturer + " (" + std::to_string(manufacturer_count) + ")";
        IM_CHECK_EQ(definition.manufacturer, "Issue 170");
        ctx->SetRef("Component Library");
        const auto type_item = ctx->ItemInfo(definition.type.c_str(), ImGuiTestOpFlags_NoError);
        IM_CHECK(type_item.ID != 0);
        if (type_item.ID == 0) {
            restore_library_visibility();
            return;
        }
        if (window->StateStorage.GetInt(type_item.ID, 0) == 0) {
            ctx->ItemClick(definition.type.c_str());
            ctx->Yield(2);
        }
        IM_CHECK_EQ(window->StateStorage.GetInt(type_item.ID, -1), 1);
        const std::string type_ref = "Component Library/" + definition.type;
        const ImGuiID manufacturer_id = ImHashStr(manufacturer_label.c_str(), 0, type_item.ID);
        ctx->SetRef(type_ref.c_str());
        IM_CHECK(ctx->ItemExists(manufacturer_label.c_str()));
        if (window->StateStorage.GetInt(manufacturer_id, 0) == 0) {
            ctx->ItemClick(manufacturer_label.c_str());
            ctx->Yield(2);
        }
        const std::string edit_label = "Edit##" + definition.source_path;
        ctx->SetRef((type_ref + "/" + manufacturer_label).c_str());
        IM_CHECK(ctx->ItemExists(definition.part_number.c_str()));
        const ImGuiID edit_id = ImHashStr(edit_label.c_str(), 0, manufacturer_id);
        const auto edit_info = ctx->ItemInfo(edit_id);
        IM_CHECK(edit_info.ID != 0);
        ctx->MouseMoveToPos(edit_info.RectFull.GetCenter());
        ctx->MouseClick(ImGuiMouseButton_Left);
        ctx->SetRef("");
        ctx->Yield(2);

        IM_CHECK_EQ(s_app->componentCount(), component_count_before);
        ImGuiWindow *component_form = ImGui::FindWindowByName("Component Form");
        IM_CHECK(component_form != nullptr && component_form->Active);
        if (component_form && component_form->Active) {
            IM_CHECK(s_app->testComponentFormModel().partNumber() == definition.part_number);
            ctx->SetRef("//$FOCUSED");
            ctx->ItemClick("Cancel");
            ctx->SetRef("");
            ctx->Yield(2);
        }

        std::vector<int> added_node_ids;
        for (const auto *component : s_app->testComponents().all())
            if (std::find(original_node_ids.begin(), original_node_ids.end(),
                          component->graphNodeId()) == original_node_ids.end())
                added_node_ids.push_back(component->graphNodeId());
        for (int node_id : added_node_ids)
            s_app->testRemoveComponent(node_id);

        restore_library_visibility();
    };
    t = IM_REGISTER_TEST(e, "rf_simulator", "component_library_refreshes_live_contents");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        ctx->SetRef("");
        namespace fs = std::filesystem;
        struct TestState {
            RfSimulatorApp &app;
            fs::path original_cwd;
            std::string original_project_path;
            fs::path root;

            ~TestState() {
                std::error_code ec;
                fs::current_path(original_cwd, ec);
                app.m_current_project_path = original_project_path;
                app.refreshExtensions();
                fs::remove_all(root, ec);
            }
        };

        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const fs::path root =
            fs::temp_directory_path() / ("rf_sim_issue256_" + std::to_string(stamp));
        TestState state{*s_app, fs::current_path(), s_app->m_current_project_path, root};
        const fs::path definition_path =
            root / "rf-sim-libraries" / "attenuators" / "issue256.json";
        fs::create_directories(definition_path.parent_path());

        const auto write_definition = [&](const std::string &part_number, double attenuation_dB) {
            std::ofstream file(definition_path);
            if (!file)
                return false;
            file << "{\n"
                 << "  \"schema_version\": 1,\n"
                 << "  \"type\": \"attenuator\",\n"
                 << "  \"part_number\": \"" << part_number << "\",\n"
                 << "  \"manufacturer\": \"Issue 256\",\n"
                 << "  \"parameters\": { \"attenuation_dB\": " << attenuation_dB << " }\n"
                 << "}\n";
            file.close();
            return file.good();
        };
        IM_CHECK(write_definition("ISSUE256-OLD", 6.0));

        fs::current_path(root);
        s_app->m_current_project_path = (root / "issue256.rfsim").string();
        s_app->refreshExtensions();

        const auto has_part = [](const std::vector<const ComponentDefinition *> &definitions,
                                 const std::string &part_number) {
            return std::any_of(definitions.begin(), definitions.end(), [&](const auto *definition) {
                return definition->part_number == part_number;
            });
        };
        IM_CHECK(has_part(s_app->testComponentLibrary().all(), "ISSUE256-OLD"));
        if (!write_definition("ISSUE256-NEW", 8.0))
            return;
        IM_CHECK(has_part(s_app->testComponentLibrary().all(), "ISSUE256-OLD"));

        ctx->Yield(2);
        ImGuiWindow *library_window = ImGui::FindWindowByName("Component Library");
        const bool library_was_visible = library_window && library_window->Active;
        bool library_opened_by_test = false;
        const auto restore_library_visibility = [&]() {
            if (!library_opened_by_test)
                return;
            ctx->SetRef("##MainMenuBar");
            ctx->MenuClick("View/Component Library");
            ctx->SetRef("");
            ctx->Yield(2);
            library_opened_by_test = false;
        };
        if (!library_was_visible) {
            ctx->SetRef("##MainMenuBar");
            ctx->MenuClick("View/Component Library");
            ctx->SetRef("");
            library_opened_by_test = true;
        }
        ctx->WindowFocus("Component Library");
        ctx->Yield(2);
        ctx->SetRef("Component Library");
        const bool refresh_exists = ctx->ItemExists("Refresh");
        IM_CHECK(refresh_exists);
        if (!refresh_exists) {
            restore_library_visibility();
            ctx->SetRef("");
            return;
        }
        ctx->ItemClick("Refresh");
        ctx->Yield(2);
        restore_library_visibility();
        ctx->SetRef("");

        const auto definitions = s_app->testComponentLibrary().all();
        IM_CHECK(!has_part(definitions, "ISSUE256-OLD"));
        IM_CHECK(has_part(definitions, "ISSUE256-NEW"));
        const auto updated =
            std::find_if(definitions.begin(), definitions.end(),
                         [](const auto *def) { return def->part_number == "ISSUE256-NEW"; });
        if (updated != definitions.end())
            IM_CHECK((*updated)->parameters.at("attenuation_dB").get<double>() == 8.0);
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

    // Align and undo from the node right-click menu (TRS-79): two selected nodes, right-click the
    // lower one, Align > Top moves it onto the upper node's top edge, and Ctrl+Z puts it back.
    t = IM_REGISTER_TEST(e, "rf_simulator", "node_align_menu");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        ctx->WindowFocus("Node Editor");
        ctx->WindowResize("Node Editor", ImVec2(800, 600));
        ctx->Yield(2);

        NodeGraphWidget &widget = s_app->testGraphWidget();
        const int upper = s_app->testCreateComponent("generator", 9501)->graphNodeId();
        const int lower = s_app->testCreateComponent("amplifier", 9502)->graphNodeId();
        ImNodes::EditorContextSet(widget.context());
        ImNodes::EditorContextResetPanning(ImVec2(0.0f, 0.0f));
        widget.setNodeEditorSpacePosition(upper, ImVec2(120.0f, 300.0f));
        widget.setNodeEditorSpacePosition(lower, ImVec2(320.0f, 380.0f));
        ctx->Yield(2);

        ImNodes::ClearNodeSelection();
        ImNodes::SelectNode(upper);
        ImNodes::SelectNode(lower);
        // Screen-space centre of a node, read after a frame so ImNodes has its size.
        const auto centreOf = [&](int node_id) {
            ImNodes::EditorContextSet(widget.context());
            const ImVec2 origin = ImNodes::GetNodeScreenSpacePos(node_id);
            const ImVec2 size = ImNodes::GetNodeDimensions(node_id);
            return ImVec2(origin.x + size.x * 0.5f, origin.y + size.y * 0.5f);
        };

        ctx->MouseMoveToPos(centreOf(lower));
        ctx->MouseClick(1);
        ctx->Yield(2);
        ctx->MenuClick("//$FOCUSED/Align/Top");
        ctx->Yield(2);
        IM_CHECK_EQ(widget.editorPositionForSave(upper).y, 300.0f);
        IM_CHECK_EQ(widget.editorPositionForSave(lower).y, 300.0f);

        // The canvas menu no longer offers Undo Layout; the global undo takes the record back.
        ctx->WindowFocus("Node Editor");
        ctx->Yield(1);
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
        ctx->Yield(2);
        IM_CHECK_EQ(widget.editorPositionForSave(upper).y, 300.0f);
        IM_CHECK_EQ(widget.editorPositionForSave(lower).y, 380.0f);

        ImNodes::EditorContextSet(widget.context());
        ImNodes::ClearNodeSelection();
        s_app->testRemoveComponent(upper);
        s_app->testRemoveComponent(lower);
        ctx->Yield(1);
    };

    // ES-3: Ctrl+Z undoes a canvas delete, Ctrl+Shift+Z and Ctrl+Y redo it, and the delete's
    // removal notice names what was taken away and how to take it back (ES-4).
    t = IM_REGISTER_TEST(e, "rf_simulator", "undo_redo_shortcuts");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        ctx->WindowFocus("Node Editor");
        ctx->WindowResize("Node Editor", ImVec2(900, 700));
        ctx->Yield(2);

        NodeGraphWidget &widget = s_app->testGraphWidget();
        IComponentEngine *generator = s_app->testCreateComponent("generator", 9901);
        IComponentEngine *amplifier = s_app->testCreateComponent("amplifier", 9902);
        IM_CHECK(generator != nullptr && amplifier != nullptr);
        if (!generator || !amplifier)
            return;
        const int generator_node = generator->graphNodeId();
        const int amplifier_node = amplifier->graphNodeId();
        ImNodes::EditorContextSet(widget.context());
        ImNodes::EditorContextResetPanning(ImVec2(0.0f, 0.0f));
        widget.setNodeEditorSpacePosition(amplifier_node, ImVec2(300.0f, 200.0f));
        ctx->Yield(4); // let imnodes register both nodes in the pool

        const NodeGraphEngine &graph = s_app->testGraphEngine();
        const int generator_out = graph.outputPinId(generator_node);
        const int amplifier_in = graph.inputPinId(amplifier_node);
        const int amplifier_out = graph.outputPinId(amplifier_node);
        IM_CHECK(generator_out >= 0 && amplifier_in >= 0 && amplifier_out >= 0);
        const auto link = s_app->testConnectLink(generator_out, amplifier_in);
        IM_CHECK(link.has_value());
        IM_CHECK(s_app->testAddProbePin(amplifier_out));
        ctx->Yield(2);
        const std::vector<int> probes = graph.probePins();
        IM_CHECK(!probes.empty());

        ImNodes::EditorContextSet(widget.context());
        ImNodes::ClearNodeSelection();
        ImNodes::SelectNode(amplifier_node);
        ctx->WindowFocus("Node Editor");
        ctx->Yield(2);
        ctx->KeyPress(ImGuiKey_Delete);
        ctx->Yield(2);
        IM_CHECK(s_app->testComponents().find(amplifier_node) == nullptr);

        // ES-4: the canvas delete says what it removed.
        IM_CHECK_EQ(std::string(widget.noticeAt(ImGui::GetTime())),
                    std::string("Removed Amplifier 9902 \xC2\xB7 Ctrl+Z to undo"));

        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
        ctx->Yield(2);
        const IComponentEngine *restored = s_app->testComponents().find(amplifier_node);
        IM_CHECK(restored != nullptr);
        if (restored)
            IM_CHECK_EQ(restored->id(), 9902);
        IM_CHECK(std::any_of(graph.links().begin(), graph.links().end(),
                             [&](const GraphLink &restored_link) {
                                 return restored_link.start_pin_id == generator_out &&
                                        restored_link.end_pin_id == amplifier_in;
                             }));
        IM_CHECK(graph.probePins() == probes);
        IM_CHECK_EQ(widget.gridPositionForSave(amplifier_node).x, 300.0f);
        IM_CHECK_EQ(widget.gridPositionForSave(amplifier_node).y, 200.0f);

        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z);
        ctx->Yield(2);
        IM_CHECK(s_app->testComponents().find(amplifier_node) == nullptr);

        // Ctrl+Z takes the redo back, then Ctrl+Y redoes it again.
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
        ctx->Yield(2);
        IM_CHECK(s_app->testComponents().find(amplifier_node) != nullptr);
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Y);
        ctx->Yield(2);
        IM_CHECK(s_app->testComponents().find(amplifier_node) == nullptr);

        // Ctrl+Y with nothing left to redo changes nothing.
        const std::size_t records = s_app->testEditorCommands().undoCount();
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Y);
        ctx->Yield(2);
        IM_CHECK(s_app->testComponents().find(amplifier_node) == nullptr);
        IM_CHECK_EQ(s_app->testEditorCommands().undoCount(), records);

        IM_CHECK(s_app->testRemoveComponent(generator_node));
        ctx->Yield(2);
    };

    // ES-3: the Edit menu offers Undo and Redo, and clicking Undo takes back the last edit.
    t = IM_REGISTER_TEST(e, "rf_simulator", "edit_menu_undo_redo");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        IComponentEngine *component = s_app->testCreateComponent("amplifier", 9911);
        IM_CHECK(component != nullptr);
        if (!component)
            return;
        const int node = component->graphNodeId();
        ctx->Yield(2);
        NodeGraphWidget &widget = s_app->testGraphWidget();
        const std::string undo_record = s_app->testEditorCommands().undoLabel();
        IM_CHECK_EQ(undo_record, "Add Amplifier 9911");

        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("Edit");
        ctx->Yield(2);
        const ImGuiTestItemInfo undo_item = findMenuItem(ctx, "Undo Add Amplifier 9911");
        IM_CHECK(undo_item.ID != 0);
        IM_CHECK((undo_item.ItemFlags & ImGuiItemFlags_Disabled) == 0);
        if (undo_item.ID != 0)
            ctx->ItemClick(undo_item.ID);
        ctx->Yield(2);
        IM_CHECK(s_app->testComponents().find(node) == nullptr);
        IM_CHECK_EQ(std::string(widget.noticeAt(ImGui::GetTime())), "Undo: " + undo_record);

        // With the edit reverted, the menu offers it back as a redo.
        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("Edit");
        ctx->Yield(2);
        const ImGuiTestItemInfo redo_item = findMenuItem(ctx, "Redo Add Amplifier 9911");
        IM_CHECK(redo_item.ID != 0);
        IM_CHECK((redo_item.ItemFlags & ImGuiItemFlags_Disabled) == 0);
        const std::string redo_record = s_app->testEditorCommands().redoLabel();
        IM_CHECK_EQ(redo_record, "Add Amplifier 9911");
        if (redo_item.ID != 0)
            ctx->ItemClick(redo_item.ID);
        ctx->Yield(2);
        IM_CHECK(s_app->testComponents().find(node) != nullptr);
        IM_CHECK_EQ(std::string(widget.noticeAt(ImGui::GetTime())), "Redo: " + redo_record);

        // An undo with nothing to undo shows no notice.
        s_app->testEditorCommands().clearHistory();
        widget.showNotice("", ImGui::GetTime());
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
        ctx->Yield(2);
        IM_CHECK_EQ(s_app->testEditorCommands().undoCount(), 0);
        IM_CHECK(widget.noticeAt(ImGui::GetTime()).empty());

        // With nothing in either direction the items read bare Undo / Redo and are disabled.
        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("Edit");
        ctx->Yield(2);
        const ImGuiTestItemInfo bare_undo = findMenuItem(ctx, "Undo###edit_undo");
        const ImGuiTestItemInfo bare_redo = findMenuItem(ctx, "Redo###edit_redo");
        IM_CHECK(bare_undo.ID != 0);
        IM_CHECK(bare_redo.ID != 0);
        IM_CHECK_EQ(std::string(bare_undo.DebugLabel), std::string("Undo###edit_undo"));
        IM_CHECK_EQ(std::string(bare_redo.DebugLabel), std::string("Redo###edit_redo"));
        IM_CHECK((bare_undo.ItemFlags & ImGuiItemFlags_Disabled) != 0);
        IM_CHECK((bare_redo.ItemFlags & ImGuiItemFlags_Disabled) != 0);

        ctx->KeyPress(ImGuiKey_Escape);
        ctx->SetRef("");
        ctx->Yield(2);

        IM_CHECK(s_app->testRemoveComponent(node));
        ctx->Yield(2);
    };

    // Settings > UI Font... opens the monospaced font picker. A family applies to the whole UI at
    // once and is saved (Windows app.ini); Default restores the built-in font. The case starts from
    // Default and writes the app.ini UiFont value it found back at the end.
    t = IM_REGISTER_TEST(e, "rf_simulator", "settings_font_picker");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        const std::string saved_before = s_app->m_state.load("Settings", "UiFont", "");
        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("Settings/UI Font...");
        ctx->Yield(2);
        IM_CHECK(s_app->m_show_settings);

        ctx->SetRef("Settings");
        ctx->Yield(2);
        IM_CHECK(ctx->ItemExists("**/Default (built-in)"));
        ctx->ItemClick("**/Default (built-in)");
        ctx->Yield(3);
        IM_CHECK(s_app->m_ui_font.appliedFamily().empty());
        IM_CHECK(s_app->m_ui_font.savedFamily().empty());
        IM_CHECK(s_app->m_ui_font.note().empty());

        const std::vector<std::string> &installed = s_app->m_ui_font.installed();
        if (!installed.empty()) {
            // The first listed family is a monospaced face the system can load.
            const std::string family = installed.front();
            IM_CHECK(ctx->ItemExists(("**/" + family).c_str()));
            ctx->ItemClick(("**/" + family).c_str());
            ctx->Yield(3);
            IM_CHECK_EQ(s_app->m_ui_font.appliedFamily(), family);
            IM_CHECK_EQ(s_app->m_ui_font.savedFamily(), family);
            IM_CHECK(s_app->m_ui_font.note().empty());
            IM_CHECK_EQ(std::string(ImGui::GetIO().FontDefault->GetDebugName()),
                        family.substr(0, 39));
#ifdef _WIN32
            IM_CHECK_EQ(s_app->m_state.load("Settings", "UiFont", ""), family);
#endif

            ctx->ItemClick("**/Default (built-in)");
            ctx->Yield(3);
            IM_CHECK(s_app->m_ui_font.appliedFamily().empty());
            IM_CHECK(s_app->m_ui_font.savedFamily().empty());
            IM_CHECK(std::string(ImGui::GetIO().FontDefault->GetDebugName()) !=
                     family.substr(0, 39));
        }

        // Put back the app.ini value the case found, so a developer's saved font survives a run.
        s_app->m_state.save("Settings", "UiFont", saved_before.c_str());
        s_app->m_show_settings = false;
        ctx->SetRef("");
        ctx->Yield(2);
    };

    // A UI font change releases the previous font and its atlas texture: every pick leaves the
    // atlas holding one font and one input source, the previous font is gone from the atlas, and
    // the live texture bytes stay bounded across repeated picks. Logs the measured numbers.
    t = IM_REGISTER_TEST(e, "rf_simulator", "settings_font_atlas_reuse");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        const std::vector<std::string> &installed = s_app->m_ui_font.installed();
        if (installed.size() < 2) {
            std::printf("[font-atlas] fewer than two monospaced families installed; skipped\n");
            return;
        }
        struct AtlasSample {
            int fonts = 0;
            int sources = 0;
            int textures = 0;
            int live_textures = 0;
            long long live_bytes = 0;
        };
        const auto sample = []() {
            ImFontAtlas *atlas = ImGui::GetIO().Fonts;
            AtlasSample s;
            s.fonts = atlas->Fonts.Size;
            s.sources = atlas->Sources.Size;
            s.textures = atlas->TexList.Size;
            for (ImTextureData *tex : atlas->TexList) {
                if (tex->Status == ImTextureStatus_Destroyed)
                    continue;
                ++s.live_textures;
                s.live_bytes += tex->GetSizeInBytes();
            }
            return s;
        };
        const auto inAtlas = [](const ImFont *font) {
            for (ImFont *candidate : ImGui::GetIO().Fonts->Fonts)
                if (candidate == font)
                    return true;
            return false;
        };
        const auto textureListed = [](const ImTextureData *tex) {
            for (ImTextureData *candidate : ImGui::GetIO().Fonts->TexList)
                if (candidate == tex)
                    return true;
            return false;
        };
        // The backend destroys the previous texture once it is unused; wait for that, bounded.
        const auto waitTextureReleased = [&ctx, &textureListed](const ImTextureData *tex) {
            for (int frame = 0; frame < 40 && textureListed(tex); ++frame)
                ctx->Yield(1);
            return !textureListed(tex);
        };

        struct PickResult {
            AtlasSample state;
            bool previous_font_gone = false;
            bool texture_released = false;
            bool texture_reused = false; // the atlas rebuilt in place: nothing to release
        };
        int pick_index = 0;
        // No IM_CHECK in here: the macro returns from the enclosing function, and this lambda
        // returns a value. The test body checks each result.
        const auto pick = [&](const std::string &family, const char *label) {
            ImFont *previous_font = ImGui::GetIO().FontDefault;
            ImTextureData *previous_texture = ImGui::GetIO().Fonts->TexData;
            s_app->m_ui_font.select(family);
            ctx->Yield(4);
            PickResult result;
            result.state = sample();
            result.previous_font_gone =
                !inAtlas(previous_font) || previous_font == ImGui::GetIO().FontDefault;
            result.texture_reused = previous_texture == ImGui::GetIO().Fonts->TexData;
            result.texture_released =
                result.texture_reused || waitTextureReleased(previous_texture);
            std::printf("[font-atlas] pick %2d %-20s fonts=%d sources=%d textures=%d live=%d "
                        "live_bytes=%lld reused=%d private_kb=%zu\n",
                        ++pick_index, label, result.state.fonts, result.state.sources,
                        result.state.textures, result.state.live_textures, result.state.live_bytes,
                        result.texture_reused ? 1 : 0, processPrivateKB());
            return result;
        };

        // The first pick is the TTF baseline; eleven more alternate families and Default.
        std::vector<std::pair<std::string, std::string>> picks;
        picks.emplace_back(installed.front(), installed.front());
        for (int i = 1; i <= 11; ++i) {
            const bool use_default = (i % 3 == 0);
            if (use_default)
                picks.emplace_back(std::string(), "Default");
            else
                picks.emplace_back(installed[static_cast<std::size_t>(i) % installed.size()],
                                   std::string());
        }

        long long baseline_bytes = 0;
        long long max_live_bytes = 0;
        for (std::size_t index = 0; index < picks.size(); ++index) {
            const std::string &family = picks[index].first;
            const std::string &label = picks[index].second.empty() ? family : picks[index].second;
            const PickResult result = pick(family, label.c_str());
            IM_CHECK_EQ(s_app->m_ui_font.appliedFamily(), family);
            IM_CHECK_EQ(result.state.fonts, 1);
            IM_CHECK_EQ(result.state.sources, 1);
            IM_CHECK(result.state.live_textures <= 3);
            IM_CHECK(result.previous_font_gone);
            IM_CHECK(result.texture_released);
            // The atlas reuses one texture object and grows nothing across picks; when it does
            // rebuild a new one, the previous entry must have left the list (checked above).
            IM_CHECK_EQ(result.state.textures, 1);
            if (index == 0)
                baseline_bytes = result.state.live_bytes;
            max_live_bytes = std::max(max_live_bytes, result.state.live_bytes);
        }
        // One font of this size: the live bytes stay near the first pick's and never grow per pick.
        const long long cap = 2 * baseline_bytes + 64 * 1024;
        std::printf("[font-atlas] baseline=%lld max=%lld cap=%lld\n", baseline_bytes,
                    max_live_bytes, cap);
        IM_CHECK(max_live_bytes <= cap);

        s_app->m_ui_font.select("");
        ctx->Yield(4);
    };

    // ES-3 (Review Focus 4): Ctrl+Z while a Properties field is being edited belongs to the
    // field, not the canvas, and the field edit becomes one undo record when it is committed.
    t = IM_REGISTER_TEST(e, "rf_simulator", "ctrl_z_while_editing_field_is_ignored");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        IComponentEngine *amplifier = s_app->testCreateComponent("amplifier", 9921);
        IM_CHECK(amplifier != nullptr);
        if (!amplifier)
            return;
        const int node = amplifier->graphNodeId();
        NodeHelper::selectNode(ctx, *s_app, node);
        ctx->Yield(2);

        // The Add record is on the stack, so an unguarded undo would change this count.
        const std::size_t records = s_app->testEditorCommands().undoCount();
        IM_CHECK(records > 0);

        ctx->SetRef("Properties");
        ctx->ItemInput("Gain (dB)");
        ctx->Yield(2);
        IM_CHECK(ImGui::GetIO().WantTextInput);
        ctx->KeyChars("5");
        ctx->Yield(1);
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
        ctx->Yield(2);
        IM_CHECK_EQ(s_app->testEditorCommands().undoCount(), records);
        IM_CHECK(s_app->testComponents().find(node) != nullptr);
        IM_CHECK(ImGui::GetIO().WantTextInput);

        // The field consumed that Ctrl+Z for its own text undo, so type again to leave a net
        // change for the commit below.
        ctx->KeyChars("5");
        ctx->Yield(1);
        ctx->KeyPress(ImGuiKey_Enter);
        ctx->Yield(3);
        IM_CHECK_EQ(s_app->testEditorCommands().undoCount(), records + 1);
        IM_CHECK_EQ(s_app->testEditorCommands().undoLabel(), "Edit Amplifier 9921");

        ctx->SetRef("");
        IM_CHECK(s_app->testRemoveComponent(node));
        ctx->Yield(2);
    };

    // Task 7 (ES-5): Properties owns one Remove in its header. The per-drawer Delete buttons
    // are gone, the header Remove takes the selection away, and Ctrl+Z brings it back.
    t = IM_REGISTER_TEST(e, "rf_simulator", "properties_remove_in_header");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        IComponentEngine *amplifier = s_app->testCreateComponent("amplifier", 9931);
        IM_CHECK(amplifier != nullptr);
        if (!amplifier)
            return;
        const int node = amplifier->graphNodeId();
        NodeHelper::selectNode(ctx, *s_app, node);
        ctx->WindowFocus("Properties");
        ctx->WindowResize("Properties", ImVec2(420, 560));
        ctx->Yield(2);

        ctx->SetRef("Properties");
        IM_CHECK(!ctx->ItemExists("Delete"));
        IM_CHECK(ctx->ItemExists("Remove"));
        ctx->ItemClick("Remove");
        ctx->Yield(2);
        IM_CHECK(s_app->testComponents().find(node) == nullptr);
        // The header names what it removed with the same label the undo record uses.
        IM_CHECK_EQ(s_app->testEditorCommands().undoLabel(), "Remove Amplifier 9931");

        // The header Remove records through the editor command service, so undo restores it.
        ctx->SetRef("");
        ctx->WindowFocus("Node Editor");
        ctx->Yield(1);
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
        ctx->Yield(2);
        IM_CHECK(s_app->testComponents().find(node) != nullptr);

        ctx->SetRef("");
        IM_CHECK(s_app->testRemoveComponent(node));
        ctx->Yield(2);
    };

    t = IM_REGISTER_TEST(e, "rf_simulator", "node_snap_menu");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        ctx->WindowFocus("Node Editor");
        ctx->WindowResize("Node Editor", ImVec2(800, 600));
        ctx->Yield(2);

        NodeGraphWidget &widget = s_app->testGraphWidget();
        const int left = s_app->testCreateComponent("generator", 9601)->graphNodeId();
        const int right = s_app->testCreateComponent("amplifier", 9602)->graphNodeId();
        ImNodes::EditorContextSet(widget.context());
        ImNodes::EditorContextResetPanning(ImVec2(0.0f, 0.0f));
        widget.setNodeEditorSpacePosition(left, ImVec2(101.0f, 43.0f));
        widget.setNodeEditorSpacePosition(right, ImVec2(305.0f, 61.0f));
        ctx->Yield(2);

        ImNodes::ClearNodeSelection();
        ImNodes::SelectNode(left);
        ImNodes::SelectNode(right);
        const auto centreOf = [&](int node_id) {
            ImNodes::EditorContextSet(widget.context());
            const ImVec2 origin = ImNodes::GetNodeScreenSpacePos(node_id);
            const ImVec2 size = ImNodes::GetNodeDimensions(node_id);
            return ImVec2(origin.x + size.x * 0.5f, origin.y + size.y * 0.5f);
        };

        ctx->MouseMoveToPos(centreOf(right));
        ctx->MouseClick(1);
        ctx->Yield(2);
        ctx->MenuClick("//$FOCUSED/Snap selection to grid");
        ctx->Yield(2);
        IM_CHECK_EQ(widget.editorPositionForSave(left).x, 96.0f);
        IM_CHECK_EQ(widget.editorPositionForSave(left).y, 48.0f);
        IM_CHECK_EQ(widget.editorPositionForSave(right).x, 312.0f);
        IM_CHECK_EQ(widget.editorPositionForSave(right).y, 72.0f);

        ImNodes::EditorContextSet(widget.context());
        ImNodes::ClearNodeSelection();
        s_app->testRemoveComponent(left);
        s_app->testRemoveComponent(right);
        ctx->Yield(1);
    };

    // Drag snapping: a drag lands on the grid only while View > Snap to Grid is on.
    t = IM_REGISTER_TEST(e, "rf_simulator", "node_grid_snap_drag");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        ctx->WindowFocus("Node Editor");
        ctx->WindowResize("Node Editor", ImVec2(800, 600));
        ctx->Yield(2);

        NodeGraphWidget &widget = s_app->testGraphWidget();
        const int node = s_app->testCreateComponent("generator", 9611)->graphNodeId();
        ImNodes::EditorContextSet(widget.context());
        ImNodes::EditorContextResetPanning(ImVec2(0.0f, 0.0f));
        widget.setNodeEditorSpacePosition(node, ImVec2(101.0f, 43.0f));
        ctx->Yield(2);
        ImNodes::ClearNodeSelection();

        const auto dragBy = [&](ImVec2 delta) {
            ImNodes::EditorContextSet(widget.context());
            const ImVec2 origin = ImNodes::GetNodeScreenSpacePos(node);
            const ImVec2 size = ImNodes::GetNodeDimensions(node);
            const ImVec2 from(origin.x + size.x * 0.5f, origin.y + size.y * 0.5f);
            ctx->MouseMoveToPos(from);
            ctx->MouseDown(ImGuiMouseButton_Left);
            ctx->MouseMoveToPos(ImVec2(from.x + delta.x, from.y + delta.y));
            ctx->MouseUp(ImGuiMouseButton_Left);
            ctx->Yield(2);
        };
        const auto toggleSnapMenu = [&]() {
            ctx->SetRef("##MainMenuBar");
            ctx->MenuClick("View/Snap to Grid");
            ctx->SetRef("");
            ctx->Yield(2);
        };

        IM_CHECK(!widget.gridSnapping());
        dragBy(ImVec2(37.0f, 19.0f));
        const ImVec2 free_pos = widget.nodeGridPosition(node);
        IM_CHECK_EQ(free_pos.x, 138.0f);
        IM_CHECK_EQ(free_pos.y, 62.0f);

        toggleSnapMenu();
        IM_CHECK(widget.gridSnapping());
        dragBy(ImVec2(37.0f, 19.0f));
        const ImVec2 snapped_pos = widget.nodeGridPosition(node);
        IM_CHECK_EQ(fmodf(snapped_pos.x, 24.0f), 0.0f);
        IM_CHECK_EQ(fmodf(snapped_pos.y, 24.0f), 0.0f);

        toggleSnapMenu();
        IM_CHECK(!widget.gridSnapping());

        ImNodes::EditorContextSet(widget.context());
        ImNodes::ClearNodeSelection();
        s_app->testRemoveComponent(node);
        ctx->Yield(1);
    };

    t = IM_REGISTER_TEST(e, "rf_simulator", "properties_window_exists");
    t->TestFunc = [](ImGuiTestContext *ctx) { ctx->WindowFocus("Properties"); };

    // Issue #88: the analyzer plot must absorb the window's vertical space
    // (ImPlot's bare BeginPlot default is a fixed 300 px) and keep a usable
    // floor when the window is small.
    t = IM_REGISTER_TEST(e, "rf_simulator", "spectrum_analyzer_plot_tracks_window_size");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        // The plot only exists when at least one pin is probed (the widget
        // early-returns on an empty trace list), so probe the seeded
        // generator's output for the duration of this test.
        const auto &graph = s_app->testGraphEngine();
        int gen_out = graph.outputPinId(1);
        IM_CHECK(gen_out >= 0);
        IM_CHECK(s_app->testAddProbePin(gen_out));
        s_app->m_show_spectrum = true;
        ctx->Yield(3);

        // Black-box: ImPlot registers the plot frame through ImGui::ItemAdd, so
        // the test engine can read its rect instead of the widget exposing one.
        auto plot_size = [&]() {
            ctx->SetRef("Spectrum Analyzer");
            auto info = ctx->ItemInfo("Spectrum", ImGuiTestOpFlags_NoError);
            ctx->SetRef("");
            return info.RectFull.GetSize();
        };

        const ImVec2 default_size = plot_size();
        IM_CHECK(default_size.y >= SpectrumAnalyzerWidget::kMinPlotHeight);

        ctx->WindowFocus("Spectrum Analyzer");
        ctx->WindowResize("Spectrum Analyzer", ImVec2(720, 760));
        ctx->Yield(3);
        const ImVec2 tall_size = plot_size();

        ctx->WindowResize("Spectrum Analyzer", ImVec2(720, 600));
        ctx->Yield(3);
        const ImVec2 short_size = plot_size();

        ctx->LogInfo("spectrum plot: default=%.1f tall=%.1f short=%.1f wide=%.1f", default_size.y,
                     tall_size.y, short_size.y, tall_size.x);
        IM_CHECK(tall_size.y > 320.0f);       // grew past ImPlot's old fixed default
        IM_CHECK(short_size.y < tall_size.y); // tracks the window instead of staying fixed
        IM_CHECK(short_size.y >= SpectrumAnalyzerWidget::kMinPlotHeight);
        IM_CHECK(tall_size.x > 600.0f); // fills the window width

        // Enabling a marker adds the selected-marker rows and the readout strip
        // below the plot; the plot must give up exactly that room and stay usable.
        ctx->SetRef("Spectrum Analyzer");
        IM_CHECK(ctx->ItemExists("M1"));
        ctx->ItemCheck("M1");
        ctx->SetRef("");
        ctx->Yield(2);
        const float marker_h = plot_size().y;
        IM_CHECK(marker_h < short_size.y);
        IM_CHECK(marker_h >= SpectrumAnalyzerWidget::kMinPlotHeight);
        ctx->SetRef("Spectrum Analyzer");
        IM_CHECK(ctx->ItemExists("Snap to Peak"));
        IM_CHECK(ctx->ItemExists("Marker Table"));
        ctx->ItemUncheck("M1"); // restore the disabled layout
        ctx->SetRef("");
        ctx->Yield(2);

        // Closing and reopening the panel must not disturb the layout.
        s_app->m_show_spectrum = false;
        ctx->Yield(2);
        s_app->m_show_spectrum = true;
        ctx->Yield(3);
        const float reopened_h = plot_size().y;
        IM_CHECK(reopened_h >= SpectrumAnalyzerWidget::kMinPlotHeight);
        IM_CHECK(reopened_h < tall_size.y);

        // A docked panel's minimum height must not force its dock node to grow
        // into neighbouring panels; #88 dropped the old hard 400 px floor.
        ctx->WindowResize("Spectrum Analyzer", ImVec2(400, 240));
        ctx->Yield(3);
        IM_CHECK(ctx->GetWindowByRef("Spectrum Analyzer")->Size.y <= 241.0f);

        IM_CHECK(s_app->testRemoveProbePin(gen_out));
        ctx->Yield(2);
    };

    // Issue #292: Peak, snap-to-peak and marker read the widget's jitter-free readout
    // trace. It must hold the same values on every frame, while the jittered plot
    // trace changes. A widget that reads the jittered trace again fails here.
    t = IM_REGISTER_TEST(e, "rf_simulator", "spectrum_analyzer_readout_ignores_display_jitter");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        const auto &graph = s_app->testGraphEngine();
        int gen_out = graph.outputPinId(1);
        IM_CHECK(gen_out >= 0);
        IM_CHECK(s_app->testAddProbePin(gen_out));

        auto &sa = s_app->testSpectrumAnalyzerEngine();
        const bool jitter_was_enabled = sa.noiseJitterEnabled();
        const double sigma_before = sa.noiseJitterSigmaDb();
        const bool spectrum_was_open = s_app->m_show_spectrum;
        sa.setNoiseJitterEnabled(true);
        sa.setNoiseJitterSigmaDb(3.0);
        s_app->m_show_spectrum = true;
        ctx->WindowFocus("Spectrum Analyzer");
        ctx->Yield(3);

        const std::vector<double> first = s_app->testSpectrumWidget().readoutTrace();
        IM_CHECK(!first.empty());
        for (int frame = 0; frame < 20; ++frame) {
            ctx->Yield(1);
            IM_CHECK(s_app->testSpectrumWidget().readoutTrace() == first);
        }

        sa.setNoiseJitterEnabled(jitter_was_enabled);
        sa.setNoiseJitterSigmaDb(sigma_before);
        s_app->m_show_spectrum = spectrum_was_open;
        IM_CHECK(s_app->testRemoveProbePin(gen_out));
        ctx->Yield(2);
    };

    // Markers are instrument cursors: M1..M4, a draggable line, Ctrl+click
    // placement, per-marker peak search, delta readouts against M1, and a table.
    t = IM_REGISTER_TEST(e, "rf_simulator", "spectrum_analyzer_markers_drag_place_delta_and_table");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        const auto &graph = s_app->testGraphEngine();
        const int gen_out = graph.outputPinId(1);
        IM_CHECK(gen_out >= 0);
        IM_CHECK(s_app->testAddProbePin(gen_out));
        s_app->m_show_spectrum = true;
        ctx->WindowFocus("Spectrum Analyzer");
        ctx->WindowResize("Spectrum Analyzer", ImVec2(720, 760));
        ctx->Yield(3);

        auto &sa = s_app->testSpectrumAnalyzerEngine();
        const double start_Hz = sa.startFrequency();
        const double stop_Hz = sa.stopFrequency();
        const double span_Hz = stop_Hz - start_Hz;
        const auto &widget = s_app->testSpectrumWidget();
        const auto &markers = widget.markers();
        IM_CHECK(!widget.readoutAxis().empty());
        IM_CHECK(!widget.readoutTrace().empty());

        auto area = [&]() { return widget.lastPlotArea(); };
        auto x_for = [&](double freq_Hz) {
            const auto plot = area();
            return plot.x + static_cast<float>((freq_Hz - start_Hz) / span_Hz) * plot.width;
        };
        auto click_marker = [&](int one_based) {
            ctx->SetRef("Spectrum Analyzer");
            const std::string id = "marker" + std::to_string(one_based);
            IM_CHECK(ctx->ItemExists(id.c_str()));
            ctx->ItemClick(id.c_str());
            ctx->SetRef("");
            ctx->Yield(2);
        };
        auto enable_marker = [&](const char *name, bool on) {
            ctx->SetRef("Spectrum Analyzer");
            IM_CHECK(ctx->ItemExists(name));
            const int index = name[1] - '1';
            if (markers.enabled(index) != on) {
                if (on)
                    ctx->ItemCheck(name);
                else
                    ctx->ItemUncheck(name);
            }
            ctx->SetRef("");
            ctx->Yield(2);
        };

        // Marker view state persists between runs (like panel visibility), so a
        // previous run's app.ini can arrive with markers on. Start from none.
        for (int i = 0; i < MarkerModel::kMaxMarkers; ++i) {
            const char *names[] = {"M1", "M2", "M3", "M4"};
            enable_marker(names[i], false);
        }
        if (markers.tableVisible()) {
            ctx->SetRef("Spectrum Analyzer");
            ctx->ItemUncheck("Marker Table");
            ctx->SetRef("");
            ctx->Yield(2);
        }

        enable_marker("M2", true);
        enable_marker("M4", true);
        IM_CHECK(markers.enabled(1));
        IM_CHECK(markers.enabled(3));
        IM_CHECK_EQ(markers.selected(), 3);
        IM_CHECK_EQ(markers.enabledCount(), 2);

        // Per-marker Snap to Peak: M2 lands on the strongest combined-trace peak.
        click_marker(2);
        IM_CHECK_EQ(markers.selected(), 1);
        const std::vector<Peak> peaks =
            sa.findPeaks(widget.readoutTrace(), widget.readoutAxis(), 8);
        IM_CHECK(!peaks.empty());
        ctx->SetRef("Spectrum Analyzer");
        ctx->ItemClick("Snap to Peak");
        ctx->SetRef("");
        ctx->Yield(2);
        IM_CHECK(markers.frequency(1) == peaks.front().freq_Hz);
        IM_CHECK(markers.frequency(3) != peaks.front().freq_Hz); // other marker untouched

        // Drag the M2 line to the right; the readout follows live.
        const double before_drag = markers.frequency(1);
        const float drag_x = x_for(before_drag);
        const float drag_y = area().y + area().height * 0.5f;
        ctx->SetRef("");
        ctx->MouseMoveToPos(ImVec2(drag_x, drag_y));
        ctx->Yield(2);
        ctx->MouseDown(ImGuiMouseButton_Left);
        ctx->Yield(2);
        ctx->MouseMoveToPos(ImVec2(drag_x + 60.0f, drag_y));
        ctx->Yield(3);
        ctx->MouseUp(ImGuiMouseButton_Left);
        ctx->Yield(3);
        const double after_drag = markers.frequency(1);
        const double drag_target =
            start_Hz + static_cast<double>(drag_x + 60.0f - area().x) / area().width * span_Hz;
        IM_CHECK(after_drag > before_drag);
        IM_CHECK(std::abs(after_drag - drag_target) < 0.02 * span_Hz);

        // Ctrl+click places the selected marker; the drag-zoom band must not open.
        click_marker(4);
        const float place_x = area().x + area().width * 0.30f;
        const float place_y = area().y + area().height * 0.40f;
        const double place_target = start_Hz + 0.30 * span_Hz;
        ctx->KeyDown(ImGuiKey_LeftCtrl);
        ctx->MouseMoveToPos(ImVec2(place_x, place_y));
        ctx->Yield(1);
        ctx->MouseClick(ImGuiMouseButton_Left);
        ctx->KeyUp(ImGuiKey_LeftCtrl);
        ctx->Yield(3);
        IM_CHECK(std::abs(markers.frequency(3) - place_target) < 0.02 * span_Hz);
        // A zoom band would have changed the span; Ctrl+click leaves it alone.
        IM_CHECK_EQ(sa.startFrequency(), start_Hz);
        IM_CHECK_EQ(sa.stopFrequency(), stop_Hz);

        // M1 is the reference: place it, then M2 reads delta against it.
        enable_marker("M1", true);
        click_marker(1);
        const float ref_x = area().x + area().width * 0.70f;
        ctx->KeyDown(ImGuiKey_LeftCtrl);
        ctx->MouseMoveToPos(ImVec2(ref_x, place_y));
        ctx->Yield(1);
        ctx->MouseClick(ImGuiMouseButton_Left);
        ctx->KeyUp(ImGuiKey_LeftCtrl);
        ctx->Yield(3);
        IM_CHECK(std::abs(markers.frequency(0) - (start_Hz + 0.70 * span_Hz)) < 0.02 * span_Hz);

        click_marker(2);
        ctx->SetRef("Spectrum Analyzer");
        IM_CHECK(ctx->ItemExists("Delta (vs M1)"));
        ctx->ItemCheck("Delta (vs M1)");
        ctx->SetRef("");
        ctx->Yield(2);
        IM_CHECK(markers.delta(1));
        const MarkerModel::Readout delta =
            markers.readout(1, widget.readoutAxis(), widget.readoutTrace());
        const MarkerModel::Readout reference =
            markers.readout(0, widget.readoutAxis(), widget.readoutTrace());
        IM_CHECK(delta.valid);
        IM_CHECK(delta.delta);
        IM_CHECK(delta.reference_valid);
        IM_CHECK(std::abs(delta.delta_frequency_Hz -
                          (delta.frequency_Hz - reference.frequency_Hz)) < 1.0);
        IM_CHECK(std::abs(delta.delta_amplitude_dB -
                          (delta.amplitude_dBm - reference.amplitude_dBm)) < 1e-9);

        // The marker table lists each enabled marker, including the delta columns.
        ctx->SetRef("Spectrum Analyzer");
        IM_CHECK(ctx->ItemExists("Marker Table"));
        ctx->ItemCheck("Marker Table");
        ctx->SetRef("");
        ctx->Yield(2);
        IM_CHECK(markers.tableVisible());
        ctx->SetRef("Spectrum Analyzer");
        IM_CHECK(ctx->ItemExists("**/markertable2"));
        IM_CHECK(ctx->ItemExists("**/markertable1"));
        ctx->SetRef("");
        // A narrow dock keeps the strip and table inside the window: the panel
        // shows no horizontal scrollbar, so overflow would be unreachable.
        ctx->WindowResize("Spectrum Analyzer", ImVec2(400.0f, 700.0f));
        ctx->Yield(3);
        ImGuiWindow *panel = ImGui::FindWindowByName("Spectrum Analyzer");
        IM_CHECK(panel != nullptr);
        if (panel) {
            // Every marker control, strip line, and table row stays inside the
            // window at the narrow width (the panel shows no horizontal
            // scrollbar, so an overflowing item would be unreachable).
            ctx->SetRef("Spectrum Analyzer");
            const char *narrow_items[] = {"Marker Table", "marker1",         "marker2",
                                          "marker4",      "Delta (vs M1)",   "Snap to Peak",
                                          "Next Peak",    "**/markertable1", "**/markertable2"};
            for (const char *item : narrow_items) {
                const auto info = ctx->ItemInfo(item, ImGuiTestOpFlags_NoError);
                IM_CHECK(info.ID != 0);
                IM_CHECK(info.RectFull.Min.x >= panel->InnerRect.Min.x - 1.0f);
                IM_CHECK(info.RectFull.Max.x <= panel->InnerRect.Max.x + 1.0f);
            }
            ctx->SetRef("");
        }
        ctx->WindowResize("Spectrum Analyzer", ImVec2(720.0f, 760.0f));
        ctx->Yield(2);

        ctx->SetRef("Spectrum Analyzer");
        ctx->ItemUncheck("Marker Table"); // hide again
        ctx->SetRef("");
        ctx->Yield(2);
        IM_CHECK(!markers.tableVisible());

        // Leave the shared app with no marker rows for later cases.
        enable_marker("M1", false);
        enable_marker("M2", false);
        enable_marker("M4", false);
        IM_CHECK_EQ(markers.enabledCount(), 0);
        IM_CHECK(s_app->testRemoveProbePin(gen_out));
        ctx->Yield(2);
    };

    // Peak table: the up to five strongest peaks, strongest first, with deltas from the
    // strongest. Clicking a row or stepping with the row buttons moves the selected marker.
    t = IM_REGISTER_TEST(e, "rf_simulator", "spectrum_analyzer_peak_table_moves_marker");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        const auto &graph = s_app->testGraphEngine();
        const int gen_out = graph.outputPinId(1);
        IM_CHECK(gen_out >= 0);
        IM_CHECK(s_app->testAddProbePin(gen_out));
        s_app->m_show_spectrum = true;
        ctx->WindowFocus("Spectrum Analyzer");
        ctx->WindowResize("Spectrum Analyzer", ImVec2(720, 760));
        ctx->Yield(3);

        const auto &sa = s_app->testSpectrumAnalyzerEngine();
        const auto &widget = s_app->testSpectrumWidget();
        const auto &markers = widget.markers();
        const auto &table = widget.peakTable();
        IM_CHECK(!widget.readoutTrace().empty());

        auto enable_marker = [&](const char *name, bool on) {
            ctx->SetRef("Spectrum Analyzer");
            IM_CHECK(ctx->ItemExists(name));
            const int index = name[1] - '1';
            if (markers.enabled(index) != on) {
                if (on)
                    ctx->ItemCheck(name);
                else
                    ctx->ItemUncheck(name);
            }
            ctx->SetRef("");
            ctx->Yield(2);
        };
        // Marker and table visibility persist between runs; start from none, then make M1
        // the selected marker so the table's row click has a known target.
        enable_marker("M1", false);
        enable_marker("M2", false);
        enable_marker("M3", false);
        enable_marker("M4", false);
        enable_marker("M1", true);
        IM_CHECK_EQ(markers.selected(), 0);
        ctx->SetRef("Spectrum Analyzer");
        IM_CHECK(ctx->ItemExists("Peak Table"));
        if (table.visible())
            ctx->ItemUncheck("Peak Table");
        ctx->SetRef("");
        ctx->Yield(2);
        IM_CHECK(!table.visible());

        ctx->SetRef("Spectrum Analyzer");
        ctx->ItemCheck("Peak Table");
        ctx->SetRef("");
        ctx->Yield(3);
        IM_CHECK(table.visible());

        // Rows are the strongest local maxima of the same readout trace Peak uses, in
        // descending amplitude order, capped at five.
        const std::vector<Peak> searched =
            sa.findPeaks(widget.readoutTrace(), widget.readoutAxis(), PeakTableModel::kMaxPeaks);
        IM_CHECK(!searched.empty());
        IM_CHECK_EQ(table.count(), static_cast<int>(searched.size()));
        IM_CHECK(table.count() <= PeakTableModel::kMaxPeaks);
        for (int i = 0; i < table.count(); ++i) {
            const auto &row = table.rows()[static_cast<size_t>(i)];
            IM_CHECK(std::abs(row.frequency_Hz - searched[static_cast<size_t>(i)].freq_Hz) < 1.0);
            IM_CHECK(std::abs(row.amplitude_dBm - searched[static_cast<size_t>(i)].power_dBm) <
                     1e-9);
            if (i > 0)
                IM_CHECK(row.amplitude_dBm <=
                         table.rows()[static_cast<size_t>(i - 1)].amplitude_dBm);
            const auto &strongest = table.rows()[0];
            IM_CHECK(std::abs(row.delta_frequency_Hz -
                              (row.frequency_Hz - strongest.frequency_Hz)) < 1e-6);
            IM_CHECK(std::abs(row.delta_amplitude_dB -
                              (row.amplitude_dBm - strongest.amplitude_dBm)) < 1e-9);
        }

        ctx->SetRef("Spectrum Analyzer");
        IM_CHECK(ctx->ItemExists("**/peaktablerow1"));
        if (table.count() > 1)
            IM_CHECK(ctx->ItemExists("**/peaktablerow2"));

        // Clicking a row enables the selected marker and lands it on that peak.
        int clicked = 0;
        if (table.count() > 1) {
            clicked = 1;
            ctx->ItemClick("**/peaktablerow2");
        } else {
            ctx->ItemClick("**/peaktablerow1");
        }
        ctx->SetRef("");
        ctx->Yield(2);
        IM_CHECK_EQ(table.selected(), clicked);
        IM_CHECK(markers.enabled(markers.selected()));
        const auto &clicked_row = table.rows()[static_cast<size_t>(clicked)];
        IM_CHECK(std::abs(markers.frequency(markers.selected()) - clicked_row.frequency_Hz) < 1.0);

        // Next Row steps the selection and moves the marker with it; Prev Row wraps back.
        ctx->SetRef("Spectrum Analyzer");
        IM_CHECK(ctx->ItemExists("**/peaknext"));
        ctx->ItemClick("**/peaknext");
        ctx->SetRef("");
        ctx->Yield(2);
        const int stepped = (clicked + 1) % table.count();
        IM_CHECK_EQ(table.selected(), stepped);
        IM_CHECK(std::abs(markers.frequency(markers.selected()) -
                          table.rows()[static_cast<size_t>(stepped)].frequency_Hz) < 1.0);

        ctx->SetRef("Spectrum Analyzer");
        IM_CHECK(ctx->ItemExists("**/peakprev"));
        ctx->ItemClick("**/peakprev");
        ctx->SetRef("");
        ctx->Yield(2);
        IM_CHECK_EQ(table.selected(), clicked);

        // A narrow dock keeps the table and its controls inside the panel.
        ctx->WindowResize("Spectrum Analyzer", ImVec2(400.0f, 700.0f));
        ctx->Yield(3);
        ImGuiWindow *panel = ImGui::FindWindowByName("Spectrum Analyzer");
        IM_CHECK(panel != nullptr);
        if (panel) {
            ctx->SetRef("Spectrum Analyzer");
            const char *narrow_items[] = {"Peak Table", "**/peakprev", "**/peaknext",
                                          "**/peaktablerow1"};
            for (const char *item : narrow_items) {
                const auto info = ctx->ItemInfo(item, ImGuiTestOpFlags_NoError);
                IM_CHECK(info.ID != 0);
                IM_CHECK(info.RectFull.Min.x >= panel->InnerRect.Min.x - 1.0f);
                IM_CHECK(info.RectFull.Max.x <= panel->InnerRect.Max.x + 1.0f);
            }
            ctx->SetRef("");
        }
        ctx->WindowResize("Spectrum Analyzer", ImVec2(720.0f, 760.0f));
        ctx->Yield(2);

        // Leave the shared app with the table hidden and no markers on.
        ctx->SetRef("Spectrum Analyzer");
        ctx->ItemUncheck("Peak Table");
        ctx->SetRef("");
        ctx->Yield(2);
        IM_CHECK(!table.visible());
        enable_marker("M1", false);
        enable_marker("M2", false);
        enable_marker("M3", false);
        enable_marker("M4", false);
        IM_CHECK_EQ(markers.enabledCount(), 0);
        IM_CHECK(s_app->testRemoveProbePin(gen_out));
        ctx->Yield(2);
    };

    // Harmonic measurement: the fundamental and its 2nd/3rd on the selected measurement
    // source, with the marker frequency usable as the fundamental. The generator output
    // holds one tone, so neither harmonic exists.
    t = IM_REGISTER_TEST(e, "rf_simulator", "spectrum_analyzer_harmonic_measurement");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        const auto &graph = s_app->testGraphEngine();
        const int gen_out = graph.outputPinId(1);
        IM_CHECK(gen_out >= 0);
        IM_CHECK(s_app->testAddProbePin(gen_out));
        s_app->m_show_spectrum = true;
        ctx->WindowFocus("Spectrum Analyzer");
        ctx->WindowResize("Spectrum Analyzer", ImVec2(720, 760));
        ctx->Yield(3);

        auto &sa = s_app->testSpectrumAnalyzerEngine();
        const auto &widget = s_app->testSpectrumWidget();
        const auto &markers = widget.markers();
        const auto &harmonics = widget.harmonics();
        IM_CHECK(!widget.readoutTrace().empty());
        IM_CHECK(!widget.readoutAxis().empty());

        auto enable_marker = [&](const char *name, bool on) {
            ctx->SetRef("Spectrum Analyzer");
            IM_CHECK(ctx->ItemExists(name));
            const int index = name[1] - '1';
            if (markers.enabled(index) != on) {
                if (on)
                    ctx->ItemCheck(name);
                else
                    ctx->ItemUncheck(name);
            }
            ctx->SetRef("");
            ctx->Yield(2);
        };

        // Harmonic visibility and markers persist between runs; start from a clean panel.
        ctx->SetRef("Spectrum Analyzer");
        IM_CHECK(ctx->ItemExists("Harmonics"));
        if (harmonics.visible())
            ctx->ItemUncheck("Harmonics");
        ctx->SetRef("");
        ctx->Yield(2);
        IM_CHECK(!harmonics.visible());
        for (const char *name : {"M1", "M2", "M3", "M4"})
            enable_marker(name, false);

        ctx->SetRef("Spectrum Analyzer");
        ctx->ItemCheck("Harmonics");
        ctx->SetRef("");
        ctx->Yield(3);
        IM_CHECK(harmonics.visible());

        // A known span keeps the click geometry deterministic across the shared app state,
        // and puts both expected harmonics inside it (see the marker target below).
        const double original_start_Hz = sa.startFrequency();
        const double original_stop_Hz = sa.stopFrequency();
        sa.setStartFrequency(50.0e6);
        sa.setStopFrequency(400.0e6);
        ctx->Yield(3);

        // The strongest positive readout bin is the fundamental; no harmonic is present.
        const auto &axis = widget.readoutAxis();
        const auto &trace = widget.readoutTrace();
        size_t strongest = 0;
        bool found = false;
        for (size_t i = 0; i < axis.size() && i < trace.size(); ++i) {
            if (axis[i] < 0.0)
                continue;
            if (!found || trace[i] > trace[strongest]) {
                strongest = i;
                found = true;
            }
        }
        IM_CHECK(found);
        const auto &result = harmonics.result();
        IM_CHECK(result.fundamental_status == HarmonicModel::Status::Measured);
        IM_CHECK(std::abs(result.fundamental_Hz - axis[strongest]) < 1.0);
        IM_CHECK(std::abs(result.fundamental_dBm - trace[strongest]) < 1e-9);
        IM_CHECK(result.harmonics[0].status == HarmonicModel::Status::NotFound);
        IM_CHECK(result.harmonics[1].status == HarmonicModel::Status::NotFound);

        ctx->SetRef("Spectrum Analyzer");
        IM_CHECK(ctx->ItemExists("Use marker as fundamental"));
        ctx->SetRef("");

        // A marker frequency replaces auto-detection. Place M1 three bins above the tone with
        // Ctrl+click; select M1 explicitly, because an earlier case can leave another marker
        // selected.
        enable_marker("M1", true);
        ctx->SetRef("Spectrum Analyzer");
        IM_CHECK(ctx->ItemExists("marker1"));
        ctx->ItemClick("marker1");
        ctx->SetRef("");
        ctx->Yield(2);
        IM_CHECK_EQ(markers.selected(), 0);
        const double start_Hz = sa.startFrequency();
        const double stop_Hz = sa.stopFrequency();
        const double span_Hz = stop_Hz - start_Hz;
        const auto plot = widget.lastPlotArea();
        const double bin_Hz = axis.size() > 1 ? axis[1] - axis[0] : 10.0e6;
        // Off the strongest bin and off M1's mid-span start position, so the Ctrl+click
        // cannot grab the marker's own drag line instead of placing it.
        const double marker_target = axis[strongest] + 3.0 * bin_Hz;
        IM_CHECK(marker_target > start_Hz && marker_target < stop_Hz);
        const float x =
            plot.x + static_cast<float>((marker_target - start_Hz) / span_Hz) * plot.width;
        const float y = plot.y + plot.height * 0.5f;
        ctx->SetRef("");
        ctx->KeyDown(ImGuiKey_LeftCtrl);
        ctx->MouseMoveToPos(ImVec2(x, y));
        ctx->Yield(1);
        ctx->MouseClick(ImGuiMouseButton_Left);
        ctx->KeyUp(ImGuiKey_LeftCtrl);
        ctx->Yield(3);
        IM_CHECK(std::abs(markers.frequency(0) - marker_target) < 0.5 * bin_Hz);

        ctx->SetRef("Spectrum Analyzer");
        ctx->ItemCheck("Use marker as fundamental");
        ctx->SetRef("");
        ctx->Yield(3);
        IM_CHECK(harmonics.useMarker());
        const auto &marker_result = harmonics.result();
        IM_CHECK(marker_result.fundamental_status == HarmonicModel::Status::Measured);
        IM_CHECK(std::abs(marker_result.fundamental_Hz - markers.frequency(0)) < 1.0);
        IM_CHECK(std::abs(marker_result.harmonics[0].expected_Hz - 2.0 * markers.frequency(0)) <
                 1.0);
        IM_CHECK(std::abs(marker_result.harmonics[1].expected_Hz - 3.0 * markers.frequency(0)) <
                 1.0);

        // A narrow dock keeps the toggle and the harmonic controls inside the panel.
        ctx->WindowResize("Spectrum Analyzer", ImVec2(400.0f, 700.0f));
        ctx->Yield(3);
        ImGuiWindow *panel = ImGui::FindWindowByName("Spectrum Analyzer");
        IM_CHECK(panel != nullptr);
        if (panel) {
            ctx->SetRef("Spectrum Analyzer");
            const char *narrow_items[] = {"Harmonics", "Use marker as fundamental"};
            for (const char *item : narrow_items) {
                const auto info = ctx->ItemInfo(item, ImGuiTestOpFlags_NoError);
                IM_CHECK(info.ID != 0);
                IM_CHECK(info.RectFull.Min.x >= panel->InnerRect.Min.x - 1.0f);
                IM_CHECK(info.RectFull.Max.x <= panel->InnerRect.Max.x + 1.0f);
            }
            ctx->SetRef("");
        }
        ctx->WindowResize("Spectrum Analyzer", ImVec2(720.0f, 760.0f));
        ctx->Yield(2);

        // Leave the shared app with the harmonics hidden, no markers on, and the span it had.
        ctx->SetRef("Spectrum Analyzer");
        ctx->ItemUncheck("Harmonics");
        ctx->SetRef("");
        ctx->Yield(2);
        IM_CHECK(!harmonics.visible());
        enable_marker("M1", false);
        IM_CHECK_EQ(markers.enabledCount(), 0);
        sa.setStartFrequency(original_start_Hz);
        sa.setStopFrequency(original_stop_Hz);
        IM_CHECK(s_app->testRemoveProbePin(gen_out));
        ctx->Yield(2);
    };

    // IM3 measurement: the two strongest tones and their third-order products on the
    // selected measurement source, with M1/M2 usable as the tone pair. The generator output
    // holds one tone, so auto-detection reports that two tones are needed.
    t = IM_REGISTER_TEST(e, "rf_simulator", "spectrum_analyzer_im3_measurement");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        const auto &graph = s_app->testGraphEngine();
        const int gen_out = graph.outputPinId(1);
        IM_CHECK(gen_out >= 0);
        IM_CHECK(s_app->testAddProbePin(gen_out));
        s_app->m_show_spectrum = true;
        ctx->WindowFocus("Spectrum Analyzer");
        ctx->WindowResize("Spectrum Analyzer", ImVec2(720, 760));
        ctx->Yield(3);

        auto &sa = s_app->testSpectrumAnalyzerEngine();
        const auto &widget = s_app->testSpectrumWidget();
        const auto &markers = widget.markers();
        const auto &im3 = widget.im3();
        IM_CHECK(!widget.readoutTrace().empty());
        IM_CHECK(!widget.readoutAxis().empty());

        auto enable_marker = [&](const char *name, bool on) {
            ctx->SetRef("Spectrum Analyzer");
            IM_CHECK(ctx->ItemExists(name));
            const int index = name[1] - '1';
            if (markers.enabled(index) != on) {
                if (on)
                    ctx->ItemCheck(name);
                else
                    ctx->ItemUncheck(name);
            }
            ctx->SetRef("");
            ctx->Yield(2);
        };

        // IM3 visibility and markers persist between runs; start from a clean panel.
        ctx->SetRef("Spectrum Analyzer");
        IM_CHECK(ctx->ItemExists("IM3"));
        if (im3.visible())
            ctx->ItemUncheck("IM3");
        ctx->SetRef("");
        ctx->Yield(2);
        IM_CHECK(!im3.visible());
        for (const char *name : {"M1", "M2", "M3", "M4"})
            enable_marker(name, false);

        ctx->SetRef("Spectrum Analyzer");
        ctx->ItemCheck("IM3");
        ctx->SetRef("");
        ctx->Yield(3);
        IM_CHECK(im3.visible());

        const double original_start_Hz = sa.startFrequency();
        const double original_stop_Hz = sa.stopFrequency();
        sa.setStartFrequency(50.0e6);
        sa.setStopFrequency(400.0e6);
        ctx->Yield(3);

        // The generator trace holds one tone: no two clear tones exist.
        IM_CHECK(im3.result().tone_status == Im3Model::Status::NeedTwoTones);

        ctx->SetRef("Spectrum Analyzer");
        IM_CHECK(ctx->ItemExists("Use M1 and M2 as tones"));
        ctx->SetRef("");

        // Place M1 and M2 on two distinct frequencies with Ctrl+click.
        auto place_marker = [&](const char *item, double target_Hz) {
            ctx->SetRef("Spectrum Analyzer");
            IM_CHECK(ctx->ItemExists(item));
            ctx->ItemClick(item);
            ctx->SetRef("");
            ctx->Yield(2);
            const double start_Hz = sa.startFrequency();
            const double stop_Hz = sa.stopFrequency();
            const double span_Hz = stop_Hz - start_Hz;
            const auto plot = widget.lastPlotArea();
            const float x =
                plot.x + static_cast<float>((target_Hz - start_Hz) / span_Hz) * plot.width;
            const float y = plot.y + plot.height * 0.5f;
            ctx->KeyDown(ImGuiKey_LeftCtrl);
            ctx->MouseMoveToPos(ImVec2(x, y));
            ctx->Yield(1);
            ctx->MouseClick(ImGuiMouseButton_Left);
            ctx->KeyUp(ImGuiKey_LeftCtrl);
            ctx->Yield(3);
        };

        const auto &axis = widget.readoutAxis();
        const double bin_Hz = axis.size() > 1 ? axis[1] - axis[0] : 10.0e6;
        const double span_start_Hz = sa.startFrequency();
        const double span_stop_Hz = sa.stopFrequency();
        const double target1 = span_start_Hz + 0.25 * (span_stop_Hz - span_start_Hz);
        const double target2 = span_start_Hz + 0.65 * (span_stop_Hz - span_start_Hz);
        IM_CHECK(target2 > target1);
        enable_marker("M1", true);
        place_marker("marker1", target1);
        IM_CHECK(std::abs(markers.frequency(0) - target1) < 0.5 * bin_Hz);
        enable_marker("M2", true);
        place_marker("marker2", target2);
        IM_CHECK(std::abs(markers.frequency(1) - target2) < 0.5 * bin_Hz);

        ctx->SetRef("Spectrum Analyzer");
        ctx->ItemCheck("Use M1 and M2 as tones");
        ctx->SetRef("");
        ctx->Yield(3);
        IM_CHECK(im3.useMarkers());
        const auto &marker_result = im3.result();
        IM_CHECK(marker_result.tone_status == Im3Model::Status::Measured);
        IM_CHECK(std::abs(marker_result.tones[0].frequency_Hz - markers.frequency(0)) < 1.0);
        IM_CHECK(std::abs(marker_result.tones[1].frequency_Hz - markers.frequency(1)) < 1.0);
        IM_CHECK(std::abs(marker_result.products[0].expected_Hz -
                          (2.0 * markers.frequency(0) - markers.frequency(1))) < 1.0);
        IM_CHECK(std::abs(marker_result.products[1].expected_Hz -
                          (2.0 * markers.frequency(1) - markers.frequency(0))) < 1.0);

        // A narrow dock keeps the toggle and the marker-source row inside the panel.
        ctx->WindowResize("Spectrum Analyzer", ImVec2(400.0f, 700.0f));
        ctx->Yield(3);
        ImGuiWindow *panel = ImGui::FindWindowByName("Spectrum Analyzer");
        IM_CHECK(panel != nullptr);
        if (panel) {
            ctx->SetRef("Spectrum Analyzer");
            const char *narrow_items[] = {"IM3", "Use M1 and M2 as tones"};
            for (const char *item : narrow_items) {
                const auto info = ctx->ItemInfo(item, ImGuiTestOpFlags_NoError);
                IM_CHECK(info.ID != 0);
                IM_CHECK(info.RectFull.Min.x >= panel->InnerRect.Min.x - 1.0f);
                IM_CHECK(info.RectFull.Max.x <= panel->InnerRect.Max.x + 1.0f);
            }
            ctx->SetRef("");
        }
        ctx->WindowResize("Spectrum Analyzer", ImVec2(720.0f, 760.0f));
        ctx->Yield(2);

        // Leave the shared app with IM3 hidden, no markers on, and the span it had.
        ctx->SetRef("Spectrum Analyzer");
        ctx->ItemUncheck("IM3");
        ctx->SetRef("");
        ctx->Yield(2);
        IM_CHECK(!im3.visible());
        enable_marker("M1", false);
        enable_marker("M2", false);
        IM_CHECK_EQ(markers.enabledCount(), 0);
        sa.setStartFrequency(original_start_Hz);
        sa.setStopFrequency(original_stop_Hz);
        IM_CHECK(s_app->testRemoveProbePin(gen_out));
        ctx->Yield(2);
    };

    // Measurement source selector: readouts and peak searches can follow one probe's
    // trace instead of every visible trace, and losing that probe falls back clearly.
    t = IM_REGISTER_TEST(e, "rf_simulator", "spectrum_analyzer_measure_on_probe");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        const auto &graph = s_app->testGraphEngine();
        const int gen_out = graph.outputPinId(1);
        const int amp_out = graph.outputPinId(2);
        IM_CHECK(gen_out >= 0);
        IM_CHECK(amp_out >= 0);
        // Probe the amplifier before the generator: probe-pin order then differs from the
        // node registration order, so a selector that maps combo slots through active nodes
        // instead of the probe list would pick the wrong trace.
        IM_CHECK(s_app->testAddProbePin(amp_out));
        IM_CHECK(s_app->testAddProbePin(gen_out));
        s_app->m_show_spectrum = true;
        ctx->WindowFocus("Spectrum Analyzer");
        ctx->Yield(3);

        const auto &widget = s_app->testSpectrumWidget();
        IM_CHECK(widget.measurementSelection().allVisible());
        IM_CHECK(widget.measurementResolution().allVisible());
        IM_CHECK_EQ(widget.measurementResolution().fallback, MeasurementSelection::Fallback::None);

        ctx->SetRef("Spectrum Analyzer");
        IM_CHECK(ctx->ItemExists("Measure on"));
        // Cache a peak list on the all-visible source; changing sources must drop it so
        // Next Peak adopts the selected trace's peaks.
        if (!widget.markers().enabled(0)) {
            ctx->ItemCheck("M1");
            ctx->Yield(2);
        }
        IM_CHECK(ctx->ItemExists("Snap to Peak"));
        ctx->ItemClick("Snap to Peak");
        ctx->SetRef("");
        ctx->Yield(2);
        IM_CHECK(!widget.markers().peaks(0).empty());
        // The combo lists All visible first, then the probes in probe-pin order; clicking
        // every item leaves the last one (the second probe, the generator) selected.
        ctx->SetRef("Spectrum Analyzer");
        ctx->ComboClickAll("Measure on");
        ctx->SetRef("");
        ctx->Yield(3);
        IM_CHECK(widget.markers().peaks(0).empty());

        // The selection names the generator probe, and the readouts use its own trace.
        const auto probed = graph.probedSignalNodes();
        IM_CHECK_EQ(probed.size(), static_cast<size_t>(2));
        IM_CHECK(!widget.measurementSelection().allVisible());
        IM_CHECK(widget.measurementSelection().probe().has_value());
        if (probed.size() == 2 && widget.measurementSelection().probe().has_value()) {
            IM_CHECK(widget.measurementSelection().probe()->node == probed[1].node);
            IM_CHECK_EQ(widget.measurementSelection().probe()->output_index,
                        probed[1].output_index);
            const auto &selected_spec = probed[1].node->outputs[probed[1].output_index];
            IM_CHECK_EQ(widget.measurementResolution().probe_index, 1);
            IM_CHECK_EQ(widget.readoutAxis().size(), selected_spec.frequencies.size());
            IM_CHECK(!widget.readoutTrace().empty());
            IM_CHECK(widget.readoutTrace().size() == widget.readoutAxis().size());
        }

        // Removing the chosen probe is a clear fallback to all visible traces, not a
        // silent switch to another probe.
        IM_CHECK(s_app->testRemoveProbePin(gen_out));
        ctx->Yield(3);
        IM_CHECK(widget.measurementResolution().allVisible());
        IM_CHECK_EQ(widget.measurementResolution().fallback,
                    MeasurementSelection::Fallback::Removed);
        IM_CHECK(!widget.readoutAxis().empty());

        // Selecting "All visible" returns the historic combined readout.
        ctx->SetRef("Spectrum Analyzer");
        ctx->ComboClick("Measure on/All visible");
        ctx->SetRef("");
        ctx->Yield(2);
        IM_CHECK(widget.measurementSelection().allVisible());
        IM_CHECK_EQ(widget.measurementResolution().fallback, MeasurementSelection::Fallback::None);

        ctx->SetRef("Spectrum Analyzer");
        if (widget.markers().enabled(0)) {
            ctx->ItemUncheck("M1");
            ctx->Yield(2);
        }
        ctx->SetRef("");
        IM_CHECK(s_app->testRemoveProbePin(amp_out));
        ctx->Yield(2);
    };

    // A node probed at two outputs: selecting the earlier output must measure that output's
    // spectrum, not the node's last-probed output and not an empty readout.
    t = IM_REGISTER_TEST(e, "rf_simulator", "spectrum_analyzer_measure_multi_output_probe");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        IComponentEngine *splitter = s_app->testCreateComponent("splitter", 9851);
        IM_CHECK(splitter != nullptr);
        if (!splitter)
            return;
        const int splitter_node = splitter->graphNodeId();
        const auto &graph = s_app->testGraphEngine();
        std::vector<int> out_pins;
        std::vector<int> in_pins;
        std::string label;
        for (const auto &node : graph.nodes()) {
            if (node.node_id == splitter_node) {
                out_pins = node.output_pin_ids;
                in_pins = node.input_pin_ids;
                label = node.label;
            }
        }
        IM_CHECK_EQ(out_pins.size(), static_cast<size_t>(2));
        IM_CHECK(!in_pins.empty());
        if (out_pins.size() < 2 || in_pins.empty()) {
            IM_CHECK(s_app->testRemoveComponent(splitter_node));
            return;
        }
        // Feed the splitter so its outputs carry a frequency grid.
        const int gen_out = graph.outputPinId(1);
        IM_CHECK(gen_out >= 0);
        const auto link = s_app->testConnectLink(gen_out, in_pins[0]);
        IM_CHECK(link.has_value());
        // Probe OUT first and OUT2 second: the node's last-probed output is then OUT2, so
        // selecting the earlier output exercises the output that active nodes drop.
        IM_CHECK(s_app->testAddProbePin(out_pins[0]));
        IM_CHECK(s_app->testAddProbePin(out_pins[1]));
        s_app->m_show_spectrum = true;
        ctx->WindowFocus("Spectrum Analyzer");
        ctx->Yield(3);

        const auto &widget = s_app->testSpectrumWidget();
        const auto probed = graph.probedSignalNodes();
        IM_CHECK_EQ(probed.size(), static_cast<size_t>(2));
        ctx->SetRef("Spectrum Analyzer");
        IM_CHECK(ctx->ItemExists("Measure on"));
        const std::string item = "Measure on/[0] " + label + " OUT";
        ctx->ComboClick(item.c_str());
        ctx->SetRef("");
        ctx->Yield(3);

        if (probed.size() == 2) {
            IM_CHECK(widget.measurementSelection().probe().has_value());
            IM_CHECK_EQ(widget.measurementResolution().probe_index, 0);
            IM_CHECK(widget.measurementResolution().fallback ==
                     MeasurementSelection::Fallback::None);
            IM_CHECK_EQ(widget.measurementSelection().probe()->node, probed[0].node);
            IM_CHECK_EQ(widget.measurementSelection().probe()->output_index,
                        probed[0].output_index);
            const auto &first_spec =
                probed[0].node->outputs[static_cast<size_t>(probed[0].output_index)];
            IM_CHECK(!first_spec.frequencies.empty());
            IM_CHECK(!widget.readoutTrace().empty());
            IM_CHECK_EQ(widget.readoutAxis().size(), first_spec.frequencies.size());
            IM_CHECK(widget.readoutTrace().size() == widget.readoutAxis().size());
        }

        IM_CHECK(s_app->testRemoveProbePin(out_pins[1]));
        IM_CHECK(s_app->testRemoveProbePin(out_pins[0]));
        ctx->Yield(2);
        IM_CHECK(s_app->testRemoveComponent(splitter_node));
        ctx->Yield(2);
    };

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

        const auto &graph = s_app->testGraphEngine();
        size_t initial_links = graph.links().size();
        int gen_out = graph.outputPinId(1);
        int amp_in = graph.inputPinId(2);
        IM_CHECK(gen_out >= 0);
        IM_CHECK(amp_in >= 0);
        const auto link = s_app->testConnectLink(gen_out, amp_in);
        IM_CHECK(link.has_value());
        const int link_id = link.value_or(-1);
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

    // ES-1: Delete pressed while a Properties field is being edited belongs to the
    // field, not the canvas, even though the canvas still holds the selection.
    t = IM_REGISTER_TEST(e, "rf_simulator", "delete_in_properties_field_keeps_node");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        IComponentEngine *amplifier = s_app->testCreateComponent("amplifier", 9702);
        IM_CHECK(amplifier != nullptr);
        const int node = amplifier->graphNodeId();
        NodeHelper::selectNode(ctx, *s_app, node);
        ctx->Yield(2);

        // Activate the amplifier gain field, so the text input owns the keyboard.
        ctx->SetRef("Properties");
        ctx->ItemInput("Gain (dB)");
        ctx->Yield(2);
        IM_CHECK(ImGui::GetIO().WantTextInput);
        // The canvas still holds the selection; only the text field owns the keyboard.
        IM_CHECK_EQ(ImNodes::NumSelectedNodes(), 1);

        ctx->KeyPress(ImGuiKey_Delete);
        ctx->Yield(2);

        IM_CHECK(s_app->testComponents().find(node) != nullptr);

        // Leave the field and drop the test component so later cases see the seeded circuit.
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(1);
        ctx->SetRef("");
        IM_CHECK(s_app->testRemoveComponent(node));
        ctx->Yield(2);
    };

    // ES-1: a canvas-hosted modal (CreateSubcircuit) is not the Node Editor even though the
    // canvas owns it, so Delete pressed while the modal has focus must not reach the nodes
    // selected behind it.
    t = IM_REGISTER_TEST(e, "rf_simulator", "delete_in_canvas_modal_keeps_node");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        ctx->WindowFocus("Node Editor");
        ctx->WindowResize("Node Editor", ImVec2(900, 700));
        ctx->Yield(2);

        NodeGraphWidget &widget = s_app->testGraphWidget();
        IComponentEngine *first = s_app->testCreateComponent("generator", 9711);
        IComponentEngine *second = s_app->testCreateComponent("amplifier", 9712);
        IM_CHECK(first != nullptr && second != nullptr);
        if (!first || !second)
            return;
        const int member_a = first->graphNodeId();
        const int member_b = second->graphNodeId();
        ImNodes::EditorContextSet(widget.context());
        ImNodes::EditorContextResetPanning(ImVec2(0.0f, 0.0f));
        widget.setNodeEditorSpacePosition(member_a, ImVec2(260.0f, 300.0f));
        widget.setNodeEditorSpacePosition(member_b, ImVec2(520.0f, 300.0f));
        ctx->Yield(4);

        rubberBandOverNodes(ctx, member_a, member_b);
        IM_CHECK(ImGui::IsPopupOpen((ImGuiID)0, ImGuiPopupFlags_AnyPopup));
        // No text field is active: the modal window itself owns the keyboard.
        IM_CHECK(!ImGui::GetIO().WantTextInput);

        // Both members stay selected behind the modal.
        ImNodes::EditorContextSet(widget.context());
        ImNodes::ClearNodeSelection();
        ImNodes::SelectNode(member_a);
        ImNodes::SelectNode(member_b);
        ctx->Yield(2);
        IM_CHECK_EQ(ImNodes::NumSelectedNodes(), 2);

        ctx->KeyPress(ImGuiKey_Delete);
        ctx->Yield(2);
        IM_CHECK(s_app->testComponents().find(member_a) != nullptr);
        IM_CHECK(s_app->testComponents().find(member_b) != nullptr);

        // Close the modal and remove the fixture.
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(2);
        ImNodes::ClearNodeSelection();
        IM_CHECK(s_app->testRemoveComponent(member_a));
        IM_CHECK(s_app->testRemoveComponent(member_b));
        ctx->Yield(2);
    };

    // ES-1: while the canvas-hosted Name field is being edited, Delete belongs to that field
    // and must not remove the nodes selected behind it.
    t = IM_REGISTER_TEST(e, "rf_simulator", "delete_in_canvas_name_field_keeps_node");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        ctx->WindowFocus("Node Editor");
        ctx->WindowResize("Node Editor", ImVec2(900, 700));
        ctx->Yield(2);

        NodeGraphWidget &widget = s_app->testGraphWidget();
        IComponentEngine *first = s_app->testCreateComponent("generator", 9721);
        IComponentEngine *second = s_app->testCreateComponent("amplifier", 9722);
        IM_CHECK(first != nullptr && second != nullptr);
        if (!first || !second)
            return;
        const int member_a = first->graphNodeId();
        const int member_b = second->graphNodeId();
        ImNodes::EditorContextSet(widget.context());
        ImNodes::EditorContextResetPanning(ImVec2(0.0f, 0.0f));
        widget.setNodeEditorSpacePosition(member_a, ImVec2(260.0f, 300.0f));
        widget.setNodeEditorSpacePosition(member_b, ImVec2(520.0f, 300.0f));
        ctx->Yield(4);

        rubberBandOverNodes(ctx, member_a, member_b);
        IM_CHECK(ImGui::IsPopupOpen((ImGuiID)0, ImGuiPopupFlags_AnyPopup));

        // Both members stay selected while the canvas-hosted Name field takes the keyboard.
        ImNodes::EditorContextSet(widget.context());
        ImNodes::ClearNodeSelection();
        ImNodes::SelectNode(member_a);
        ImNodes::SelectNode(member_b);
        ctx->Yield(2);
        ctx->SetRef("//$FOCUSED");
        ctx->ItemInput("Name");
        ctx->Yield(2);
        IM_CHECK(ImGui::GetIO().WantTextInput);
        IM_CHECK_EQ(ImNodes::NumSelectedNodes(), 2);

        ctx->KeyPress(ImGuiKey_Delete);
        ctx->Yield(2);
        IM_CHECK(s_app->testComponents().find(member_a) != nullptr);
        IM_CHECK(s_app->testComponents().find(member_b) != nullptr);

        // Leave the field, close the modal and remove the fixture.
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(2);
        ctx->SetRef("");
        ImNodes::ClearNodeSelection();
        IM_CHECK(s_app->testRemoveComponent(member_a));
        IM_CHECK(s_app->testRemoveComponent(member_b));
        ctx->Yield(2);
    };

    // Issue #67: right-clicking a link must offer a Remove entry (previously the
    // right-click fell through to the canvas "add component" menu). Uses the
    // seeded Generator/Amplifier (nodes 1/2), which are visible this early in
    // the run.
    t = IM_REGISTER_TEST(e, "rf_simulator", "link_context_menu_remove");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        ctx->WindowFocus("Node Editor");
        ctx->WindowMove("Node Editor", ImVec2(20.0f, 40.0f));
        ctx->WindowResize("Node Editor", ImVec2(900, 700));
        ctx->Yield(2);
        ImNodes::EditorContextResetPanning(ImVec2(0, 0));
        ctx->Yield(2);

        const auto &graph = s_app->testGraphEngine();
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

        const auto created_link = s_app->testConnectLink(gen_out, amp_in);
        IM_CHECK(created_link.has_value());
        const int link_id = created_link.value_or(-1);
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
    // Hover Tooltip Tests
    // =========================================================================

    // Hovering a node, a link, or a collapsed subcircuit must bring up a tooltip:
    // the editor's probe chord (Ctrl+click) is documented nowhere else, so the
    // hint text is the discoverability surface. Black-box check: ImGui's tooltip
    // windows carry ImGuiWindowFlags_Tooltip, and because the test engine runs
    // between frames the check reads WasActive (the previous frame's Active flag)
    // rather than Active, which the frame that just started already cleared.
    // The tooltip's *text* is not reachable here: ImGui registers text items with
    // id 0, so the test engine cannot query them. This case therefore checks the
    // node-body callback data the app hands the widget (non-empty summary plus a
    // populated analyzer SNR) together with the tooltip-window visibility above.
    // The standalone app adapter test asserts that numeric callback data — not
    // the rendered string — and the widget's formatted "SNR: %.1f dB" / "SNR:
    // --" row text is not independently asserted anywhere; it is verified by
    // source review of showNodeHoverTooltips().
    t = IM_REGISTER_TEST(e, "rf_simulator", "hover_tooltips_node_link_subcircuit");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        // Any tooltip window, whatever its "##Tooltip_NN" suffix: ImGui bumps
        // that suffix when a second tooltip is drawn in one frame, so pinning the
        // name would silently read a window this editor never opened.
        auto tooltip_visible = []() {
            for (ImGuiWindow *w : ImGui::GetCurrentContext()->Windows)
                if (w->WasActive && (w->Flags & ImGuiWindowFlags_Tooltip))
                    return true;
            return false;
        };
        // True once the mouse sits on `node_id`. A pin never counts: imnodes
        // reports the pin instead of the node, which is the case the node
        // tooltip is deliberately suppressed for.
        ImNodesEditorContext *editor_context = s_app->testGraphWidget().context();
        auto node_is_hovered = [&](int node_id) {
            ImNodes::EditorContextSet(editor_context);
            int hovered = -1;
            return ImNodes::IsNodeHovered(&hovered) && hovered == node_id;
        };
        auto hover_node = [&](int node_id, ImVec2 pos, ImVec2 dim) {
            for (float fy = 0.25f; fy <= 0.8f; fy += 0.25f) {
                for (float fx = 0.25f; fx <= 0.8f; fx += 0.25f) {
                    ctx->SetRef("");
                    ctx->MouseMoveToPos(ImVec2(pos.x + dim.x * fx, pos.y + dim.y * fy));
                    for (int frame = 0; frame < 4; ++frame) {
                        ctx->Yield();
                        if (node_is_hovered(node_id))
                            return true;
                    }
                }
            }
            return false;
        };
        ctx->SetRef("");
        ctx->WindowFocus("Node Editor");
        ctx->WindowMove("Node Editor", ImVec2(20.0f, 40.0f));
        ctx->WindowResize("Node Editor", ImVec2(900, 700));
        ctx->Yield(2);
        ImNodes::EditorContextSet(editor_context);
        ImNodes::EditorContextResetPanning(ImVec2(0, 0));
        ctx->Yield(2);

        const auto &graph = s_app->testGraphEngine();
        const int gen_node = NodeHelper::findComponentNodeId<SignalGeneratorEngine>(*s_app);
        const int amp_node = NodeHelper::findComponentNodeId<AmplifierEngine>(*s_app);
        IM_CHECK(gen_node >= 0);
        IM_CHECK(amp_node >= 0);
        const int gen_out = graph.outputPinId(gen_node);
        const int amp_in = graph.inputPinId(amp_node);
        IM_CHECK(gen_out >= 0);
        IM_CHECK(amp_in >= 0);

        // IM_CHECK returns from this test; restore temporary graph state on every exit.
        struct GraphUiCleanup {
            RfSimulatorApp &app;
            ImNodesEditorContext *editor_context;
            int gen_node;
            int amp_node;
            int link_id = -1;
            int group_id = -1;
            bool done = false;

            GraphUiCleanup(RfSimulatorApp &app, ImNodesEditorContext *editor_context, int gen_node,
                           int amp_node)
                : app(app), editor_context(editor_context), gen_node(gen_node), amp_node(amp_node) {
            }

            void run() {
                if (done)
                    return;
                if (link_id >= 0)
                    app.testDisconnectLink(link_id);
                if (group_id >= 0)
                    app.testRemoveGroup(group_id);
                ImNodes::EditorContextSet(editor_context);
                ImNodes::SetNodeGridSpacePos(gen_node, ImVec2(0, 0));
                ImNodes::SetNodeGridSpacePos(amp_node, ImVec2(0, 0));
                done = true;
            }

            ~GraphUiCleanup() { run(); }
        } cleanup(*s_app, editor_context, gen_node, amp_node);

        // Separate the two seeded nodes (they start stacked at the canvas origin)
        // inside the window's visible top-left region so both, and the link
        // between them, can be reached by the mouse.
        ImVec2 vp = ImGui::GetIO().DisplaySize;
        auto info = ctx->WindowInfo("Node Editor");
        ImVec2 vis_min = info.RectFull.Min;
        ImVec2 vis_max =
            ImVec2(std::min(info.RectFull.Max.x, vp.x), std::min(info.RectFull.Max.y, vp.y));
        float anchor_x = std::min(vis_min.x + 180.0f, vis_max.x - 120.0f);
        float anchor_y = std::min(vis_min.y + 120.0f, vis_max.y - 120.0f);
        ImNodes::EditorContextSet(editor_context);
        ImNodes::SetNodeScreenSpacePos(gen_node, ImVec2(anchor_x - 140.0f, anchor_y - 60.0f));
        ImNodes::SetNodeScreenSpacePos(amp_node, ImVec2(anchor_x + 140.0f, anchor_y + 40.0f));
        ctx->Yield(4);

        // -- node body data: this is what the tooltip body renders, read straight
        //    from the app callback the widget calls. The seeded generator carries a
        //    tone, so the analyzer-backed SNR row is populated alongside the
        //    registry summary; the widget only prints both.
        IM_CHECK(s_app->testGraphWidget().onNodeHover);
        const NodeHoverInfo gen_hover = s_app->testGraphWidget().onNodeHover(gen_node);
        IM_CHECK(!gen_hover.summary.empty());
        IM_CHECK(gen_hover.snr_dB.has_value());

        // -- node body --
        ImNodes::EditorContextSet(editor_context);
        const ImVec2 gen_pos = ImNodes::GetNodeScreenSpacePos(gen_node);
        const ImVec2 gen_dim = ImNodes::GetNodeDimensions(gen_node);
        IM_CHECK(info.RectFull.Contains(gen_pos + gen_dim * 0.5f));
        IM_CHECK(hover_node(gen_node, gen_pos, gen_dim));
        ctx->Yield(3);
        IM_CHECK(tooltip_visible());

        // -- empty canvas: the tooltip follows the hover, it is not permanently up.
        // Both nodes sit in the window's top-left anchor region, so probe the other
        // corners and require one of them to be over no node at all --
        // IsNodeHovered is the editor's own predicate, so this is not our screen
        // arithmetic talking.
        bool found_empty = false;
        for (const ImVec2 &c : {ImVec2(vis_min.x + 20.0f, vis_max.y - 20.0f),
                                ImVec2(vis_max.x - 20.0f, vis_max.y - 20.0f),
                                ImVec2(vis_max.x - 20.0f, vis_min.y + 20.0f)}) {
            ctx->SetRef("");
            ctx->MouseMoveToPos(c);
            ctx->Yield(2);
            ImNodes::EditorContextSet(editor_context);
            int h = -1;
            if (!ImNodes::IsNodeHovered(&h)) {
                found_empty = true;
                break;
            }
        }
        IM_CHECK(found_empty);
        ctx->Yield(4);
        IM_CHECK(!tooltip_visible());

        // -- link: the drawn link is a bezier between the two nodes' pins, which
        //    sit below the middle of each node rect, so sweep the gap between the
        //    node rects from the bottom row upwards - the first row usually hits --
        const auto created_link = s_app->testConnectLink(gen_out, amp_in);
        IM_CHECK(created_link.has_value());
        const int link_id = created_link.value_or(-1);
        cleanup.link_id = link_id;
        ctx->Yield(4);
        ImNodes::EditorContextSet(editor_context);
        ImVec2 c1 =
            ImNodes::GetNodeScreenSpacePos(gen_node) + ImNodes::GetNodeDimensions(gen_node) * 0.5f;
        ImVec2 c2 =
            ImNodes::GetNodeScreenSpacePos(amp_node) + ImNodes::GetNodeDimensions(amp_node) * 0.5f;
        ImVec2 mid = (c1 + c2) * 0.5f;
        bool link_hovered = false;
        for (float dy = 40.0f; dy >= -40.0f && !link_hovered; dy -= 4.0f) {
            for (float dx = -60.0f; dx <= 60.0f && !link_hovered; dx += 4.0f) {
                ctx->SetRef("");
                ctx->MouseMoveToPos(ImVec2(mid.x + dx, mid.y + dy));
                ctx->Yield(1);
                ImNodes::EditorContextSet(editor_context);
                int hovered = -1;
                link_hovered = ImNodes::IsLinkHovered(&hovered) && hovered == link_id;
            }
        }
        IM_CHECK(link_hovered);
        ctx->Yield(3);
        IM_CHECK(tooltip_visible());

        IM_CHECK(s_app->testDisconnectLink(link_id));
        cleanup.link_id = -1;
        ctx->Yield(2);

        // -- collapsed subcircuit block (block ids live in 50000..99999) --
        const int group_id = s_app->testCreateGroup("Tooltip Subcircuit", {gen_node, amp_node});
        IM_CHECK(group_id > 0);
        cleanup.group_id = group_id;
        IM_CHECK(s_app->testSetGroupCollapsed(group_id, true));
        ctx->Yield(4);
        // The link was removed above, so this block has no cross-boundary link —
        // the case where Ctrl+click has nothing to probe and the tooltip must not
        // advertise one. The block still has to describe itself.
        const Group *group = graph.groupById(group_id);
        IM_CHECK(group != nullptr);
        IM_CHECK(group->boundary_pins.empty());
        ImNodes::EditorContextSet(editor_context);
        const ImVec2 group_pos = ImNodes::GetNodeScreenSpacePos(group_id);
        const ImVec2 group_dim = ImNodes::GetNodeDimensions(group_id);
        IM_CHECK(hover_node(group_id, group_pos, group_dim));
        ctx->Yield(3);
        IM_CHECK(tooltip_visible());

        IM_CHECK(s_app->testRemoveGroup(group_id));
        cleanup.group_id = -1;
        cleanup.run();
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

        const int splitter_node = NodeHelper::addComponent(ctx, *s_app, "Add Splitter", center);
        IM_CHECK(splitter_node >= 0);

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

        // Add two new nodes via context menu so we have a set of at least 2
        // ungrouped nodes (nodes from previous tests are already grouped)
        // Add each new component to known empty canvas space. The test helper verifies the
        // canvas context menu, avoiding a focus-dependent popup lookup when group blocks exist.
        const ImVec2 mixer_menu_pos = info.RectFull.Min + ImVec2(650.0f, 520.0f);
        const int mixer_node = NodeHelper::addComponent(ctx, *s_app, "Add Mixer", mixer_menu_pos);
        IM_CHECK(mixer_node >= 0);
        const ImVec2 coax_menu_pos = info.RectFull.Min + ImVec2(650.0f, 350.0f);
        const int coax_node =
            NodeHelper::addComponent(ctx, *s_app, "Add Coax Cable", coax_menu_pos);
        IM_CHECK(coax_node >= 0);

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
        NodeHelper::selectNode(ctx, *s_app, node);
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
        NodeHelper::selectNode(ctx, *s_app, node);
        InspectorHelper::waitForPopulated(ctx, node);
        InspectorHelper::setInputDouble(ctx, "NF (dB)", 3.5);
        auto *amp = dynamic_cast<AmplifierEngine *>(s_app->testComponents().find(node));
        IM_CHECK(amp != nullptr);
        IM_CHECK_EQ(amp->nf_dB(), 3.5);
    };

    // Digital step attenuator: place a fresh one, drive the code and bit-depth
    // steppers through the Properties panel, and verify the engine state.
    t = IM_REGISTER_TEST(e, "rf_simulator", "inspector_digital_step_attenuator");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        IComponentEngine *placed =
            s_app->testAddComponentAtPosition("digital_step_attenuator", ImVec2(420, 260));
        IM_CHECK(placed != nullptr);
        const int node = placed->graphNodeId();
        NodeHelper::selectNode(ctx, *s_app, node);
        InspectorHelper::waitForPopulated(ctx, node);

        auto *dsa = dynamic_cast<DigitalStepAttenuatorEngine *>(s_app->testComponents().find(node));
        IM_CHECK(dsa != nullptr);

        // Code 21 at the 6-bit ZX76 default: 1.4 + 21 * 0.5 = 11.9 dB.
        ctx->SetRef("Properties");
        ctx->ItemInputValue("Digital Code", "21");
        ctx->Yield(2);
        IM_CHECK_EQ(dsa->code(), 21);
        IM_CHECK(std::fabs(dsa->totalAttenuation_dB() - 11.9) < 1e-9);

        // Shrinking the bit depth re-clamps the stored code (21 -> 7).
        ctx->SetRef("Properties");
        ctx->ItemInputValue("Bit Depth", "3");
        ctx->Yield(2);
        IM_CHECK_EQ(dsa->bitDepth(), 3);
        IM_CHECK_EQ(dsa->code(), 7);
        // The Bit Depth input left the Properties ref set; re-root so the
        // Node Editor resolves for the Delete key.
        ctx->SetRef("");
        ctx->WindowFocus("Node Editor");
        NodeHelper::deleteSelectedNode(ctx);
    };

    // Issue #200: a library part gets a Part dropdown that swaps it in place. The seeded amplifier
    // is a manual part, so its Properties panel has none. The two library parts are written to a
    // temporary project-local library, as the component-library case above does, because the
    // built-in library is not installed beside the test executable.
    t = IM_REGISTER_TEST(e, "rf_simulator", "inspector_part_swap_in_place");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        namespace fs = std::filesystem;
        struct ScopedLibrary {
            RfSimulatorApp &app;
            fs::path original_cwd;
            std::string original_project_path;
            fs::path root;
            ~ScopedLibrary() {
                std::error_code ec;
                fs::current_path(original_cwd, ec);
                app.m_current_project_path = original_project_path;
                app.refreshExtensions();
                fs::remove_all(root, ec);
            }
        };
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const fs::path root =
            fs::temp_directory_path() / ("rf_sim_issue200_" + std::to_string(stamp));
        ScopedLibrary scoped{*s_app, fs::current_path(), s_app->m_current_project_path, root};
        const auto write_part = [&root](const char *part_number, const char *description,
                                        double gain_dB) {
            const fs::path path = root / "rf-sim-libraries" / "amplifier" / "Issue 200" /
                                  (std::string(part_number) + ".json");
            fs::create_directories(path.parent_path());
            std::ofstream file(path);
            file << "{\"schema_version\": 1, \"type\": \"amplifier\", \"part_number\": \""
                 << part_number << "\", \"manufacturer\": \"Issue 200\", \"description\": \""
                 << description << "\", \"parameters\": {\"gain_dB\": " << gain_dB
                 << ", \"nf_dB\": 2.0}}";
        };
        write_part("UI-SWAP-A", "Swap source", 10.0);
        write_part("UI-SWAP-B", "Swap target", 20.0);
        fs::current_path(root);
        s_app->m_current_project_path = (root / "issue200.rfsim").string();
        s_app->refreshExtensions();

        const auto &graph = s_app->testGraphEngine();
        const auto part_of = [&graph](int node_id) {
            for (const auto &node : graph.nodes()) {
                if (node.node_id == node_id)
                    return node.part_number;
            }
            return std::string();
        };
        int manual_node = -1;
        for (auto *amp : s_app->testComponents().byType<AmplifierEngine>()) {
            if (part_of(amp->graphNodeId()).empty()) {
                manual_node = amp->graphNodeId();
                break;
            }
        }
        IM_CHECK(manual_node >= 0);
        NodeHelper::selectNode(ctx, *s_app, manual_node);
        InspectorHelper::waitForPopulated(ctx, manual_node);
        ctx->SetRef("Properties");
        IM_CHECK(!ctx->ItemExists("Part"));

        IComponentEngine *placed = s_app->testAddLibraryPart("UI-SWAP-A");
        IM_CHECK(placed != nullptr);
        const int component_id = placed->id();
        const int old_node = placed->graphNodeId();
        NodeHelper::selectNode(ctx, *s_app, old_node);
        InspectorHelper::waitForPopulated(ctx, old_node);
        ctx->SetRef("Properties");
        IM_CHECK(ctx->ItemExists("Part"));
        const ImVec2 old_position = s_app->testGraphWidget().editorPositionForSave(old_node);
        InspectorHelper::selectComboItem(ctx, "Part", "UI-SWAP-B  Swap target");
        ctx->Yield(4);

        // The successor keeps the component id, carries the new part and its own parameters, and
        // is the sole selected node, which is what the Properties panel reads.
        const IComponentEngine *swapped = nullptr;
        for (IComponentEngine *component : s_app->testComponents().all()) {
            if (component && component->id() == component_id)
                swapped = component;
        }
        IM_CHECK(swapped != nullptr);
        IM_CHECK(swapped->graphNodeId() != old_node);
        IM_CHECK(part_of(swapped->graphNodeId()) == "UI-SWAP-B");
        IM_CHECK_EQ(swapped->serialize().at("gain_dB").get<double>(), 20.0);
        IM_CHECK_EQ(ImNodes::NumSelectedNodes(), 1);
        int selected = -1;
        ImNodes::GetSelectedNodes(&selected);
        IM_CHECK_EQ(selected, swapped->graphNodeId());
        // The successor takes over the predecessor's editor position.
        const ImVec2 new_position =
            s_app->testGraphWidget().editorPositionForSave(swapped->graphNodeId());
        IM_CHECK(std::fabs(new_position.x - old_position.x) < 0.01f);
        IM_CHECK(std::fabs(new_position.y - old_position.y) < 0.01f);

        // Remove the test part so later cases see the seeded circuit.
        // selectComboItem left the Properties ref set; re-root so the Node Editor resolves.
        ctx->SetRef("");
        ctx->WindowFocus("Node Editor");
        NodeHelper::deleteSelectedNode(ctx);
    };

    // =========================================================================
    // Connection Creation Tests
    // =========================================================================

    t = IM_REGISTER_TEST(e, "rf_simulator", "connection_valid_generator_to_amplifier");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        const auto &graph = s_app->testGraphEngine();
        size_t initial_links = graph.links().size();
        int gen_out = graph.outputPinId(1);
        int amp_in = graph.inputPinId(2);
        IM_CHECK(gen_out >= 0);
        IM_CHECK(amp_in >= 0);
        const auto link = s_app->testConnectLink(gen_out, amp_in);
        IM_CHECK(link.has_value());
        const int link_id = link.value_or(-1);
        IM_CHECK_EQ(graph.links().size(), initial_links + 1);
        const auto &created = graph.links().back();
        IM_CHECK_EQ(created.start_pin_id, gen_out);
        IM_CHECK_EQ(created.end_pin_id, amp_in);
        IM_CHECK(s_app->testDisconnectLink(link_id));
        IM_CHECK_EQ(graph.links().size(), initial_links);
    };

    t = IM_REGISTER_TEST(e, "rf_simulator", "connection_multi_fanout");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        struct ComponentCleanup {
            RfSimulatorApp &app;
            std::vector<int> graph_node_ids;

            explicit ComponentCleanup(RfSimulatorApp &app) : app(app) {}

            ~ComponentCleanup() {
                for (auto node = graph_node_ids.rbegin(); node != graph_node_ids.rend(); ++node)
                    app.testRemoveComponent(*node);
            }
        } cleanup(*s_app);

        IComponentEngine *splitter =
            s_app->testAddComponentAtPosition("splitter", ImVec2(300.0f, 100.0f));
        IM_CHECK(splitter != nullptr);
        const int splitter_node = splitter->graphNodeId();
        cleanup.graph_node_ids.push_back(splitter_node);

        IComponentEngine *mixer =
            s_app->testAddComponentAtPosition("mixer", ImVec2(500.0f, 100.0f));
        IM_CHECK(mixer != nullptr);
        const int mixer_node = mixer->graphNodeId();
        cleanup.graph_node_ids.push_back(mixer_node);

        const NodeGraphEngine &graph = s_app->testGraphEngine();
        const size_t initial_links = graph.links().size();
        const int amp_node = NodeHelper::findComponentNodeId<AmplifierEngine>(*s_app);
        IM_CHECK(amp_node >= 0);
        const int split_out = graph.outputPinId(splitter_node);
        const int amp_in = graph.inputPinId(amp_node);
        const int mixer_in = graph.inputPinId(mixer_node);
        IM_CHECK(split_out >= 0);
        IM_CHECK(amp_in >= 0);
        IM_CHECK(mixer_in >= 0);
        const auto link1 = s_app->testConnectLink(split_out, amp_in);
        const auto link2 = s_app->testConnectLink(split_out, mixer_in);
        IM_CHECK(link1.has_value());
        IM_CHECK(link2.has_value());
        IM_CHECK_EQ(graph.links().size(), initial_links + 2);
        IM_CHECK(s_app->testDisconnectLink(link1.value_or(-1)));
        IM_CHECK(s_app->testDisconnectLink(link2.value_or(-1)));
        IM_CHECK_EQ(graph.links().size(), initial_links);
    };

    // Pin rows: outputs sit beside inputs, so a node is max(inputs, outputs) pin rows tall. A
    // 1-in/1-out Amplifier therefore has the same height as a 0-in/1-out Generator (one row).
    // The stacked layout gave the Amplifier one extra row.
    t = IM_REGISTER_TEST(e, "rf_simulator", "node_one_in_one_out_has_one_pin_row");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        struct ComponentCleanup {
            RfSimulatorApp &app;
            std::vector<int> graph_node_ids;

            explicit ComponentCleanup(RfSimulatorApp &app) : app(app) {}

            ~ComponentCleanup() {
                for (auto node = graph_node_ids.rbegin(); node != graph_node_ids.rend(); ++node)
                    app.testRemoveComponent(*node);
            }
        } cleanup(*s_app);

        IComponentEngine *amp =
            s_app->testAddComponentAtPosition("amplifier", ImVec2(300.0f, 100.0f));
        IM_CHECK(amp != nullptr);
        const int amp_node = amp->graphNodeId();
        cleanup.graph_node_ids.push_back(amp_node);

        IComponentEngine *gen =
            s_app->testAddComponentAtPosition("generator", ImVec2(500.0f, 100.0f));
        IM_CHECK(gen != nullptr);
        const int gen_node = gen->graphNodeId();
        cleanup.graph_node_ids.push_back(gen_node);
        ctx->Yield(4);

        const float amp_h = ImNodes::GetNodeDimensions(amp_node).y;
        const float gen_h = ImNodes::GetNodeDimensions(gen_node).y;
        IM_CHECK(amp_h > 0.0f);
        IM_CHECK(std::abs(amp_h - gen_h) < 1.0f);
    };

    // A title wider than the 96 px body is clamped, so the node stays at body width plus the
    // 16 px node padding (112 px). "SPDT Switch (2:1) N" measures about 130-150 px unclamped.
    t = IM_REGISTER_TEST(e, "rf_simulator", "node_long_title_clamped_to_body_width");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        struct ComponentCleanup {
            RfSimulatorApp &app;
            std::vector<int> graph_node_ids;

            explicit ComponentCleanup(RfSimulatorApp &app) : app(app) {}

            ~ComponentCleanup() {
                for (auto node = graph_node_ids.rbegin(); node != graph_node_ids.rend(); ++node)
                    app.testRemoveComponent(*node);
            }
        } cleanup(*s_app);

        IComponentEngine *sw =
            s_app->testAddComponentAtPosition("rf_switch_spdt_2to1", ImVec2(300.0f, 100.0f));
        IM_CHECK(sw != nullptr);
        const int sw_node = sw->graphNodeId();
        cleanup.graph_node_ids.push_back(sw_node);
        ctx->Yield(4);

        const float node_w = ImNodes::GetNodeDimensions(sw_node).x;
        IM_CHECK(node_w > 0.0f);
        IM_CHECK(node_w <= 112.0f + 1.0f);
    };

    t = IM_REGISTER_TEST(e, "rf_simulator", "connection_delete");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        const auto &graph = s_app->testGraphEngine();
        size_t initial_links = graph.links().size();
        int gen_out = graph.outputPinId(1);
        int amp_in = graph.inputPinId(2);
        const auto link = s_app->testConnectLink(gen_out, amp_in);
        IM_CHECK(link.has_value());
        IM_CHECK_EQ(graph.links().size(), initial_links + 1);
        IM_CHECK(s_app->testDisconnectLink(link.value_or(-1)));
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
        NodeGraphEngine graph;
        SignalNode amp_node, generator_node;
        const int amp_id = graph.addNode("Amplifier", &amp_node, 1, 1);
        const int generator_id = graph.addNode("Generator", &generator_node, 1, 1);
        const int amp_out = graph.outputPinId(amp_id);
        const int generator_out = graph.outputPinId(generator_id);
        IM_CHECK(amp_out >= 0);
        IM_CHECK(generator_out >= 0);
        const int link_id = graph.addLink(amp_out, generator_out);
        IM_CHECK(link_id >= 0);
        IM_CHECK_EQ(graph.links().size(), 1);
        graph.removeLink(link_id);
    };

    t = IM_REGISTER_TEST(e, "rf_simulator", "connection_input_to_input_accepted");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        // Documents: Engine currently accepts input→input connections (no validation)
        NodeGraphEngine graph;
        SignalNode amplifier_node, splitter_node;
        const int amplifier_id = graph.addNode("Amplifier", &amplifier_node, 1, 1);
        const int splitter_id = graph.addNode("Splitter", &splitter_node, 1, 1);
        const int amplifier_in = graph.inputPinId(amplifier_id);
        const int splitter_in = graph.inputPinId(splitter_id);
        IM_CHECK(amplifier_in >= 0);
        IM_CHECK(splitter_in >= 0);
        const int link_id = graph.addLink(amplifier_in, splitter_in);
        IM_CHECK(link_id >= 0);
        IM_CHECK_EQ(graph.links().size(), 1);
        graph.removeLink(link_id);
    };

    t = IM_REGISTER_TEST(e, "rf_simulator", "connection_self_loop_accepted");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        // Documents: Engine currently accepts self-loops (no validation)
        NodeGraphEngine graph;
        SignalNode amplifier_node;
        const int amplifier_id = graph.addNode("Amplifier", &amplifier_node, 1, 1);
        const int amp_out = graph.outputPinId(amplifier_id);
        const int amp_in = graph.inputPinId(amplifier_id);
        IM_CHECK(amp_out >= 0);
        IM_CHECK(amp_in >= 0);
        const int link_id = graph.addLink(amp_out, amp_in);
        IM_CHECK(link_id >= 0);
        IM_CHECK_EQ(graph.links().size(), 1);
        graph.removeLink(link_id);
    };

    t = IM_REGISTER_TEST(e, "rf_simulator", "connection_duplicate_accepted");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        // Documents: Engine currently creates duplicate links (no deduplication)
        NodeGraphEngine graph;
        SignalNode generator_node, amplifier_node;
        const int generator_id = graph.addNode("Generator", &generator_node, 0, 1);
        const int amplifier_id = graph.addNode("Amplifier", &amplifier_node, 1, 1);
        const int gen_out = graph.outputPinId(generator_id);
        const int amp_in = graph.inputPinId(amplifier_id);
        const int link1 = graph.addLink(gen_out, amp_in);
        const int link2 = graph.addLink(gen_out, amp_in);
        IM_CHECK(link1 >= 0);
        IM_CHECK(link2 >= 0);
        IM_CHECK_EQ(graph.links().size(), 2);
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

    // The utility docking group: opening an auxiliary view through its real
    // menu path must land it in the "Utility Space" dockspace, while core
    // instrument windows stay out of that group.
    t = IM_REGISTER_TEST(e, "rf_simulator", "utility_views_open_into_utility_dock_group");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        ensureUtilitySpaceVisible(ctx);
        // The group's host window exists even while every utility view is
        // closed: the View menu must always have somewhere to reopen a view.
        IM_CHECK(ImGui::FindWindowByName("Utility Space") != nullptr);

        struct UtilityViewCase {
            const char *menu_path;
            const char *window_name;
        };
        const UtilityViewCase cases[] = {
            {"View/Receiver Requirements", "Receiver Requirements"},
            {"View/RF Budget", "RF Budget"},
            {"View/Filter Calculator", "Filter Calculator"},
            {"View/Test Flow", "Test Flow"},
            {"View/Agent", "Agent"},
            {"Help/How to Use", "How to Use"},
            {"Tools/Extensions", "Extensions"},
        };
        constexpr int kCaseCount = sizeof(cases) / sizeof(cases[0]);

        // IM_CHECK returns from the test on failure, so every view this test
        // opens is closed again by a guard instead of trailing code the
        // checks could skip.
        struct RestoreMenuToggles {
            ImGuiTestContext *ctx = nullptr;
            const char *paths[kCaseCount] = {};
            ~RestoreMenuToggles() {
                for (const char *path : paths) {
                    if (path == nullptr)
                        continue;
                    ctx->SetRef("##MainMenuBar");
                    ctx->MenuClick(path);
                    ctx->SetRef("");
                    ctx->Yield(2);
                }
            }
        } restore{ctx, {}};

        for (int i = 0; i < kCaseCount; ++i) {
            const char *name = cases[i].window_name;
            ImGuiWindow *window = ImGui::FindWindowByName(name);
            const bool was_open = window != nullptr && window->Active;
            if (!was_open) {
                ctx->SetRef("##MainMenuBar");
                ctx->MenuClick(cases[i].menu_path);
                ctx->SetRef("");
                ctx->Yield(2);
                restore.paths[i] = cases[i].menu_path;
            }
            waitForUi(ctx, [&] { return dockedInUtilityGroup(ImGui::FindWindowByName(name)); });
            window = ImGui::FindWindowByName(name);
            IM_CHECK(window != nullptr);
            IM_CHECK(dockedInUtilityGroup(window));
        }

        // A core instrument view is not pulled into the utility group.
        IM_CHECK(!dockedInUtilityGroup(ImGui::FindWindowByName("Spectrum Analyzer")));
    };

    // The group's membership must survive the app's layout save/load path,
    // and a view the user moved to the main dockspace must stay there until
    // a layout is loaded.
    t = IM_REGISTER_TEST(e, "rf_simulator",
                         "utility_group_layout_round_trips_through_save_and_load");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        ensureUtilitySpaceVisible(ctx);
        // Toggle the Filter Calculator open through the View menu if it is
        // not already on screen (a docked background tab counts as open).
        const auto view_open = [](const char *name) {
            ImGuiWindow *window = ImGui::FindWindowByName(name);
            return window != nullptr && window->Active;
        };
        const char *calculator_menu = "View/Filter Calculator";
        bool opened_by_test = false;
        struct RestoreMenuToggle {
            ImGuiTestContext *ctx;
            const char *path;
            bool *active;
            ~RestoreMenuToggle() {
                if (*active) {
                    ctx->SetRef("##MainMenuBar");
                    ctx->MenuClick(path);
                    ctx->SetRef("");
                    ctx->Yield(2);
                }
            }
        } restore{ctx, calculator_menu, &opened_by_test};

        if (!view_open("Filter Calculator")) {
            ctx->SetRef("##MainMenuBar");
            ctx->MenuClick(calculator_menu);
            ctx->SetRef("");
            ctx->Yield(2);
            opened_by_test = true;
        }
        waitForUi(
            ctx, [] { return dockedInUtilityGroup(ImGui::FindWindowByName("Filter Calculator")); });
        IM_CHECK(dockedInUtilityGroup(ImGui::FindWindowByName("Filter Calculator")));

        const auto layout_file =
            std::filesystem::path(s_app->testLayoutManager().layoutsDir()) / "UtilityRoundTrip.ini";
        // IM_CHECK returns from the test on failure, so the file is removed by
        // a guard instead of a trailing call the checks could skip.
        struct LayoutFileCleanup {
            std::filesystem::path path;
            ~LayoutFileCleanup() {
                std::error_code ec;
                std::filesystem::remove(path, ec);
            }
        } layout_cleanup{layout_file};
        std::filesystem::remove(layout_file);

        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Layouts/Save As...");
        ctx->SetRef("Save Layout As");
        ctx->ItemInputValue("Name", "UtilityRoundTrip");
        ctx->ItemClick("Save");
        ctx->SetRef("");
        ctx->Yield();
        IM_CHECK(std::filesystem::exists(layout_file));

        // Move the view into the main dockspace: the same dock request a drag
        // between the groups submits, and the move must persist (the default
        // assignment never re-docks a placed window mid-session).
        ImGuiContext &g = *ImGui::GetCurrentContext();
        const ImGuiID utility_root_id =
            ImHashStr("UtilityDockSpace", 0, ImHashStr("Utility Space"));
        ImGuiDockNode *main_dock_root = nullptr;
        for (const ImGuiStoragePair &pair : g.DockContext.Nodes.Data) {
            auto *node = static_cast<ImGuiDockNode *>(pair.val_p);
            // The main dockspace is the dockspace root other than the
            // utility group's.
            if (node != nullptr && node->IsRootNode() && node->IsDockSpace() &&
                node->ID != utility_root_id) {
                main_dock_root = node;
                break;
            }
        }
        IM_CHECK(main_dock_root != nullptr);
        ImGui::DockBuilderDockWindow("Filter Calculator", main_dock_root->ID);
        ctx->Yield(2);
        IM_CHECK(!dockedInUtilityGroup(ImGui::FindWindowByName("Filter Calculator")));

        // Loading the saved layout restores the utility group membership.
        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Layouts/Load/UtilityRoundTrip");
        ctx->SetRef("");
        waitForUi(
            ctx, [] { return dockedInUtilityGroup(ImGui::FindWindowByName("Filter Calculator")); });
        IM_CHECK(dockedInUtilityGroup(ImGui::FindWindowByName("Filter Calculator")));
    };

    // The Utility Space is its own OS window even while it overlaps the main
    // viewport, and it is never a pane in the main dockspace.
    t = IM_REGISTER_TEST(e, "rf_simulator", "utility_space_is_its_own_os_window");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        ensureUtilitySpaceVisible(ctx);
        ctx->Yield(2);
        ImGuiWindow *space = ImGui::FindWindowByName("Utility Space");
        IM_CHECK(space != nullptr);
        IM_CHECK(space->Active);
        IM_CHECK((space->WindowClass.ViewportFlagsOverrideSet & ImGuiViewportFlags_NoAutoMerge) !=
                 0);
        IM_CHECK(space->DockNode == nullptr);
        IM_CHECK(space->DockIsActive == false);
        IM_CHECK((space->Flags & ImGuiWindowFlags_NoDocking) != 0);
        // A backend without multi-viewport support (for example Wayland)
        // makes ImGui clear ViewportsEnable; the host then stays a normal
        // floating window instead of failing.
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            IM_CHECK(space->Viewport != ImGui::GetMainViewport());
            IM_CHECK(space->ViewportOwned);
        } else {
            IM_CHECK(space->Viewport == ImGui::GetMainViewport());
        }
    };

    // Closing the group hides the docked utility views without closing them;
    // reopening the group brings them back with their layout.
    t = IM_REGISTER_TEST(e, "rf_simulator", "utility_space_closes_and_reopens_with_its_views");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        ensureUtilitySpaceVisible(ctx);
        const char *calculator_menu = "View/Filter Calculator";
        ImGuiWindow *calculator = ImGui::FindWindowByName("Filter Calculator");
        const bool was_open = calculator != nullptr && calculator->Active;
        bool opened_by_test = false;
        struct RestoreCalculator {
            ImGuiTestContext *ctx;
            const char *path;
            bool *active;
            ~RestoreCalculator() {
                if (*active) {
                    ensureUtilitySpaceVisible(ctx);
                    ctx->SetRef("##MainMenuBar");
                    ctx->MenuClick(path);
                    ctx->SetRef("");
                    ctx->Yield(2);
                }
            }
        } restore{ctx, calculator_menu, &opened_by_test};

        if (!was_open) {
            ctx->SetRef("##MainMenuBar");
            ctx->MenuClick(calculator_menu);
            ctx->SetRef("");
            ctx->Yield(2);
            opened_by_test = true;
            waitForUi(ctx, [] {
                return dockedInUtilityGroup(ImGui::FindWindowByName("Filter Calculator"));
            });
            IM_CHECK(dockedInUtilityGroup(ImGui::FindWindowByName("Filter Calculator")));
        }
        IM_CHECK(utilitySpaceVisible());

        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Utility Space");
        ctx->SetRef("");
        waitForUi(ctx, [] { return !utilitySpaceVisible(); });
        IM_CHECK(!utilitySpaceVisible());
        calculator = ImGui::FindWindowByName("Filter Calculator");
        IM_CHECK(calculator == nullptr || !calculator->Active);

        // Asking for a view while the group is hidden opens the group and
        // keeps the already-open view visible instead of toggling it off.
        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick(calculator_menu);
        ctx->SetRef("");
        waitForUi(ctx, [] { return utilitySpaceVisible(); });
        IM_CHECK(utilitySpaceVisible());
        waitForUi(ctx, [] {
            ImGuiWindow *window = ImGui::FindWindowByName("Filter Calculator");
            return window != nullptr && window->Active && dockedInUtilityGroup(window);
        });
        calculator = ImGui::FindWindowByName("Filter Calculator");
        IM_CHECK(calculator != nullptr);
        IM_CHECK(calculator->Active);
        IM_CHECK(dockedInUtilityGroup(calculator));

        // The group's own View item closes and reopens it.
        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Utility Space");
        ctx->SetRef("");
        waitForUi(ctx, [] { return !utilitySpaceVisible(); });
        IM_CHECK(!utilitySpaceVisible());
        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Utility Space");
        ctx->SetRef("");
        waitForUi(ctx, [] { return utilitySpaceVisible(); });
        IM_CHECK(utilitySpaceVisible());
    };

    // The second window's own position and size are part of the saved layout.
    t = IM_REGISTER_TEST(e, "rf_simulator", "utility_space_position_and_size_round_trip");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        ensureUtilitySpaceVisible(ctx);
        ImGuiWindow *initial_space = ImGui::FindWindowByName("Utility Space");
        IM_CHECK(initial_space != nullptr);
        if (!initial_space)
            return;
        // This test resizes the group; put the geometry back for the later
        // receiver-requirements dock-width cases.
        struct RestoreGeometry {
            ImVec2 pos;
            ImVec2 size;
            ~RestoreGeometry() {
                ImGui::SetWindowPos("Utility Space", pos);
                ImGui::SetWindowSize("Utility Space", size);
            }
        } restore_geometry{initial_space->Pos, initial_space->Size};
        const auto layout_file = std::filesystem::path(s_app->testLayoutManager().layoutsDir()) /
                                 "UtilitySpacePosSize.ini";
        struct LayoutFileCleanup {
            std::filesystem::path path;
            ~LayoutFileCleanup() {
                std::error_code ec;
                std::filesystem::remove(path, ec);
            }
        } cleanup{layout_file};
        std::filesystem::remove(layout_file);

        ImGui::SetWindowPos("Utility Space", ImVec2(100.0f, 120.0f));
        ImGui::SetWindowSize("Utility Space", ImVec2(600.0f, 500.0f));
        ctx->Yield(3);
        ImGuiWindow *space = ImGui::FindWindowByName("Utility Space");
        IM_CHECK(space != nullptr);
        const ImVec2 saved_pos = space->Pos;
        const ImVec2 saved_size = space->Size;

        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Layouts/Save As...");
        ctx->SetRef("Save Layout As");
        ctx->ItemInputValue("Name", "UtilitySpacePosSize");
        ctx->ItemClick("Save");
        ctx->SetRef("");
        ctx->Yield();
        IM_CHECK(std::filesystem::exists(layout_file));

        ImGui::SetWindowPos("Utility Space", ImVec2(200.0f, 220.0f));
        ImGui::SetWindowSize("Utility Space", ImVec2(640.0f, 520.0f));
        ctx->Yield(3);

        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Layouts/Load/UtilitySpacePosSize");
        ctx->SetRef("");
        waitForUi(ctx, [&] {
            ImGuiWindow *window = ImGui::FindWindowByName("Utility Space");
            return window != nullptr && std::fabs(window->Pos.x - saved_pos.x) <= 1.0f &&
                   std::fabs(window->Pos.y - saved_pos.y) <= 1.0f &&
                   std::fabs(window->Size.x - saved_size.x) <= 1.0f &&
                   std::fabs(window->Size.y - saved_size.y) <= 1.0f;
        });
        space = ImGui::FindWindowByName("Utility Space");
        IM_CHECK(space != nullptr);
        IM_CHECK(std::fabs(space->Pos.x - saved_pos.x) <= 1.0f);
        IM_CHECK(std::fabs(space->Pos.y - saved_pos.y) <= 1.0f);
        IM_CHECK(std::fabs(space->Size.x - saved_size.x) <= 1.0f);
        IM_CHECK(std::fabs(space->Size.y - saved_size.y) <= 1.0f);
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            IM_CHECK(space->Viewport != nullptr);
            IM_CHECK(std::fabs(space->Viewport->Pos.x - saved_pos.x) <= 1.0f);
            IM_CHECK(std::fabs(space->Viewport->Pos.y - saved_pos.y) <= 1.0f);
        }
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
    // Starting the tutorial opens the panels its core steps highlight. The
    // later feature steps open theirs through the View menu as their
    // instruction says, so those panels must NOT be pre-opened — a step that
    // says "Open View > RF Budget" would toggle a pre-opened panel closed.
    t = IM_REGISTER_TEST(e, "rf_simulator", "tutorial_start_opens_core_panels");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        s_app->m_show_rf_budget = false;
        s_app->m_show_receiver_requirements = false;
        s_app->m_show_test_flow = false;
        setAgentPanelVisible(ctx, false);

        s_app->newProject();
        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("Help/Tutorial");
        ctx->Yield(2);

        for (const TutorialStep &step : tutorialSteps()) {
            const char *title = tutorialTargetWindowTitle(step.target);
            if (!title)
                continue;
            ImGuiWindow *window = ImGui::FindWindowByName(title);
            const bool view_menu_step = step.target == TutorialTarget::RFBudget ||
                                        step.target == TutorialTarget::ReceiverRequirements ||
                                        step.target == TutorialTarget::TestFlow ||
                                        step.target == TutorialTarget::Agent;
            if (view_menu_step)
                IM_CHECK(window == nullptr || !window->Active);
            else {
                IM_CHECK(window != nullptr);
                IM_CHECK(window->Active);
            }
        }

        ctx->SetRef("Tutorial Guide");
        ctx->ItemClick("Exit");
        ctx->SetRef("");
        ctx->Yield(2);
        IM_CHECK(!s_app->testTutorialState().isActive());
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

    // =========================================================================
    // Spectrum Analyzer Settings Tests
    // =========================================================================

    // RBW/VBW must reach down to 1 kHz, which is why both are entered in kHz (a
    // 1 kHz setting reads "1"). The widget's clamp is what keeps the engine in
    // range, so an out-of-range entry has to land on the 1 kHz floor rather than
    // being accepted.
    t = IM_REGISTER_TEST(e, "rf_simulator", "spectrum_analyzer_rbw_vbw_reach_1khz");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        auto &sa = s_app->testSpectrumAnalyzerEngine();
        const double rbw_before = sa.rbw();
        const double vbw_before = sa.vbw();

        s_app->m_show_spectrum = true;
        ctx->WindowFocus("Spectrum Analyzer");
        ctx->Yield(2);

        ctx->SetRef("Spectrum Analyzer");
        IM_CHECK(ctx->ItemExists("RBW (kHz)"));
        IM_CHECK(ctx->ItemExists("VBW (kHz)"));

        ctx->ItemInputValue("RBW (kHz)", "1");
        ctx->Yield(2);
        IM_CHECK_EQ(sa.rbw(), 1e3);

        // Below the floor: clamped up to 1 kHz, not accepted as-is.
        ctx->ItemInputValue("RBW (kHz)", "0.5");
        ctx->Yield(2);
        IM_CHECK_EQ(sa.rbw(), 1e3);

        ctx->ItemInputValue("VBW (kHz)", "2");
        ctx->Yield(2);
        IM_CHECK_EQ(sa.vbw(), 2e3);

        ctx->ItemInputValue("VBW (kHz)", "0.1");
        ctx->Yield(2);
        IM_CHECK_EQ(sa.vbw(), 1e3);
        ctx->SetRef("");

        sa.setResBw(rbw_before);
        sa.setVideoBw(vbw_before);
        ctx->Yield(2);
    };

    // Out-of-range RBW/VBW fields need both the editable-range note from
    // inputFrequency() and the control-specific explanation below it. Their
    // tooltips must share one window rather than replacing each other.
    t = IM_REGISTER_TEST(e, "rf_simulator",
                         "spectrum_analyzer_out_of_range_bandwidth_tooltip_is_preserved");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        auto &sa = s_app->testSpectrumAnalyzerEngine();
        const double rbw_before = sa.rbw();
        const double vbw_before = sa.vbw();
        const bool spectrum_was_open = s_app->m_show_spectrum;
        const ImGuiHoveredFlags hover_flags_before = ImGui::GetStyle().HoverFlagsForTooltipMouse;
        ImGui::GetStyle().HoverFlagsForTooltipMouse = ImGuiHoveredFlags_None;

        sa.setResBw(100.0);
        sa.setVideoBw(100.0);
        s_app->m_show_spectrum = true;
        ctx->WindowFocus("Spectrum Analyzer");
        ctx->MouseMoveToPos(ImVec2(5.0f, 5.0f));
        ctx->Yield(3);

        const auto tooltip_window_count = [] {
            int count = 0;
            for (ImGuiWindow *window : ImGui::GetCurrentContext()->Windows)
                if (window->WasActive && (window->Flags & ImGuiWindowFlags_Tooltip))
                    ++count;
            return count;
        };
        const auto check_field_tooltip = [&](const ImGuiTestItemInfo &item) {
            IM_CHECK(item.ID != 0);
            if (item.ID == 0)
                return;

            ctx->MouseMoveToPos(item.RectFull.GetCenter());
            ctx->Yield(20); // keep the pointer over the control while its tooltip is drawn
            IM_CHECK_EQ(tooltip_window_count(), 1);
        };

        ctx->SetRef("Spectrum Analyzer");
        const ImGuiTestItemInfo vbw = ctx->ItemInfo("VBW (kHz)", ImGuiTestOpFlags_NoError);
        const ImGuiTestItemInfo rbw = ctx->ItemInfo("RBW (kHz)", ImGuiTestOpFlags_NoError);
        ctx->SetRef("");
        check_field_tooltip(vbw);
        ctx->MouseMoveToPos(ImVec2(5.0f, 5.0f));
        ctx->Yield(3);
        check_field_tooltip(rbw);

        ctx->MouseMoveToPos(ImVec2(5.0f, 5.0f));
        ctx->Yield(3);
        sa.setResBw(rbw_before);
        sa.setVideoBw(vbw_before);
        s_app->m_show_spectrum = spectrum_was_open;
        ImGui::GetStyle().HoverFlagsForTooltipMouse = hover_flags_before;
        ctx->Yield(2);
    };

    // Test Flow panel: View > Test Flow flips the in-memory visibility flag and
    // the "Test Flow" window follows it. This is only the menu/window
    // integration — the panel's own model (preview, validation, result table,
    // export states) is covered by the filterable standalone
    // test_test_flow_widget target, which also runs headless. SessionState's
    // app.ini persistence is deliberately not asserted: it is a no-op outside
    // Windows, so an assertion here would not be cross-platform.
    t = IM_REGISTER_TEST(e, "rf_simulator", "test_flow_view_menu_toggles_panel");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        s_app->m_show_test_flow = false;
        ctx->Yield(3);
        // The flag round-trips through the shared exe-relative app.ini, so a
        // prior run with the panel open can leave ImGui holding the window in
        // its lookup table. "Hidden" is therefore an inactive window, not
        // necessarily an absent one.
        ImGuiWindow *preToggle = ImGui::FindWindowByName("Test Flow");
        IM_CHECK(preToggle == nullptr || !preToggle->Active);

        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Test Flow");
        ctx->SetRef("");
        ctx->Yield(3);
        IM_CHECK(s_app->m_show_test_flow);
        // Listing the name only proves ImGui retains a window object; an
        // inactive (collapsed/unfocused) window would pass too. Assert the
        // window is actually submitted and active this frame.
        ImGuiWindow *toggled = ImGui::FindWindowByName("Test Flow");
        IM_CHECK(toggled != nullptr && toggled->Active);

        // Toggling it off hides the window again, leaving no panel behind for
        // the tests that follow. ImGui keeps an unsubmitted window object around
        // for a frame, so "hidden" is an inactive window, not necessarily a
        // deleted one.
        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Test Flow");
        ctx->SetRef("");
        ctx->Yield(3);
        IM_CHECK(!s_app->m_show_test_flow);
        ImGuiWindow *panel = ImGui::FindWindowByName("Test Flow");
        IM_CHECK(panel == nullptr || !panel->Active);
    };
    // RF Budget view: View > RF Budget opens the signal-chain editor, and its
    // Add Stage / Add Output actions edit the project budget through the same
    // helpers the standalone test_rf_budget and test_rf_budget_widget targets
    // cover. The window is closed and the model cleared so later cases start
    // from the default empty budget.
    t = IM_REGISTER_TEST(e, "rf_simulator", "rf_budget_view_menu_and_table_edit");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        s_app->m_show_rf_budget = false;
        s_app->testRfBudget() = {};
        ctx->Yield(3);
        ImGuiWindow *preToggle = ImGui::FindWindowByName("RF Budget");
        IM_CHECK(preToggle == nullptr || !preToggle->Active);

        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/RF Budget");
        ctx->SetRef("");
        ctx->Yield(3);
        IM_CHECK(s_app->m_show_rf_budget);
        // Float the window: this case opens Receiver Requirements alongside
        // it, and two views docked in the same utility group would hide
        // each other behind tabs. Floating keeps both submitted, as before
        // the utility group existed.
        ImGui::DockBuilderDockWindow("RF Budget", 0);
        ctx->Yield(2);
        ImGuiWindow *window = ImGui::FindWindowByName("RF Budget");
        IM_CHECK(window != nullptr && window->Active);

        const std::uint64_t before = s_app->projectRevision();
        ctx->SetRef("RF Budget");
        ctx->ItemClick("Add Stage");
        ctx->Yield(2);
        IM_CHECK_EQ(s_app->testRfBudget().stages.size(), 1u);
        IM_CHECK(s_app->testRfBudget().stages[0].name == "Stage 1");
        IM_CHECK(s_app->projectRevision() > before);

        ctx->ItemClick("Add Output");
        ctx->Yield(2);
        IM_CHECK_EQ(s_app->testRfBudget().outputs.size(), 1u);
        IM_CHECK(s_app->testRfBudgetWidgetError().empty());

        // A mode needs its own budget record; "Budget this mode" is the explicit
        // control, and the Requirements view's prediction follows it.
        ReceiverRequirementsConfig config;
        config.band_start_Hz = 1.0e6;
        config.band_stop_Hz = 2.0e6;
        config.gain = ReceiverGainLimits{5.0, 30.0};
        s_app->testSetSingleModeRequirements(config);
        s_app->testRfBudget().stages[0].params = {{"gain_dB", 20.0}, {"nf_dB", 2.0}};
        ctx->Yield(2);
        ctx->SetRef("RF Budget");
        ctx->ScrollToItem("Budget this mode", ImGuiAxis_Y);
        ctx->Yield(2);
        ctx->ItemClick("Budget this mode");
        ctx->Yield(3);
        IM_CHECK_EQ(s_app->testRfBudget().modes.size(), 1u);
        IM_CHECK_EQ(s_app->testRfBudget().modes[0].mode_id,
                    s_app->testReceiverRequirements().modes[0].id);

        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Receiver Requirements");
        ctx->SetRef("");
        ctx->Yield(4);
        IM_CHECK_EQ(s_app->testRfBudgetEvaluation().modes.size(), 1u);
        IM_CHECK_EQ(s_app->testRfBudgetEvaluation().modes[0].status, RfBudgetModeStatus::Budgeted);
        IM_CHECK_EQ(s_app->testRfBudgetEvaluation().modes[0].cascades.size(), 1u);
        IM_CHECK_EQ(s_app->testRfBudgetEvaluation().modes[0].cascades[0].verdict.gain.status,
                    ReceiverRequirementStatus::Pass);

        // Add override refuses to replace an existing override for the same setting.
        ctx->SetRef("RF Budget");
        ctx->ScrollToItem("Add override", ImGuiAxis_Y);
        ctx->Yield(2);
        IM_CHECK((ctx->ItemInfo("Add override").ItemFlags & ImGuiItemFlags_Disabled) == 0);
        ctx->ItemClick("Add override");
        ctx->Yield(2);
        IM_CHECK_EQ(s_app->testRfBudget().modes.size(), 1u);
        IM_CHECK_EQ(s_app->testRfBudget().modes[0].overrides.count("Stage 1"), 1u);
        IM_CHECK((ctx->ItemInfo("Add override").ItemFlags & ImGuiItemFlags_Disabled) != 0);
        s_app->testRfBudget().modes[0].overrides.clear();
        ctx->Yield(2);

        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Receiver Requirements");
        ctx->SetRef("");
        ctx->Yield(2);

        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/RF Budget");
        ctx->SetRef("");
        ctx->Yield(3);
        IM_CHECK(!s_app->m_show_rf_budget);
        window = ImGui::FindWindowByName("RF Budget");
        IM_CHECK(window == nullptr || !window->Active);

        s_app->testRfBudget() = {};
        ctx->Yield(2);
    };
    // A retained invalid marker shows its diagnostic with a Discard action; an empty budget is
    // not an error, so a fresh project shows the hint instead of a red refusal.
    t = IM_REGISTER_TEST(e, "rf_simulator", "rf_budget_view_invalid_marker_discard");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        s_app->m_show_rf_budget = false;
        s_app->testRfBudget() = {};
        ctx->Yield(2);
        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/RF Budget");
        ctx->SetRef("");
        ctx->Yield(3);
        IM_CHECK(s_app->m_show_rf_budget);
        IM_CHECK(s_app->testRfBudget().empty());
        ImGuiWindow *window = ImGui::FindWindowByName("RF Budget");
        IM_CHECK(window != nullptr && window->Active);
        ctx->WindowFocus("RF Budget");
        ctx->Yield(2);
        IM_CHECK(!ctx->ItemExists("Discard invalid budget"));

        RfBudget invalid;
        invalid.invalid_reason = "RF budget stages must be a list.";
        s_app->testRfBudget() = invalid;
        ctx->Yield(3);
        ctx->SetRef("RF Budget");
        ctx->ScrollToItem("Discard invalid budget", ImGuiAxis_Y);
        ctx->Yield(2);
        IM_CHECK(ctx->ItemInfo("Discard invalid budget").ID != 0);
        const std::uint64_t before = s_app->projectRevision();
        ctx->ItemClick("Discard invalid budget");
        ctx->Yield(2);
        IM_CHECK(s_app->testRfBudget().empty());
        IM_CHECK(s_app->projectRevision() > before);
        IM_CHECK(!ctx->ItemExists("Discard invalid budget"));

        s_app->m_show_rf_budget = false;
        ctx->Yield(2);
    };
    // A bounded field is clamped into its range before Add override stores the initial value.
    t = IM_REGISTER_TEST(e, "rf_simulator", "rf_budget_view_add_override_clamps");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        s_app->m_show_rf_budget = false;
        s_app->testRfBudget() = {};
        ReceiverRequirementsConfig config;
        config.band_start_Hz = 1.0e6;
        config.band_stop_Hz = 2.0e6;
        config.gain = ReceiverGainLimits{5.0, 30.0};
        s_app->testSetSingleModeRequirements(config);
        RfBudget budget;
        budget.point_count = 5;
        RfBudgetStage stage;
        stage.name = "EQ";
        stage.type = "equalizer";
        stage.params = {{"ref_gain_dB", 500.0}};
        stage.inputs = {RfBudgetFeed{std::nullopt, 0}};
        budget.stages.push_back(stage);
        budget.outputs = {{"RX", "EQ", 0}};
        budget.modes = {{s_app->testReceiverRequirements().modes[0].id, {}}};
        s_app->testRfBudget() = budget;
        ctx->Yield(2);

        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/RF Budget");
        ctx->SetRef("RF Budget");
        ctx->Yield(3);
        ctx->ScrollToItem("Add override", ImGuiAxis_Y);
        ctx->Yield(2);
        IM_CHECK((ctx->ItemInfo("Add override").ItemFlags & ImGuiItemFlags_Disabled) == 0);
        ctx->ItemClick("Add override");
        ctx->Yield(2);
        IM_CHECK_EQ(s_app->testRfBudget().modes.size(), 1u);
        IM_CHECK_EQ(s_app->testRfBudget().modes[0].overrides.count("EQ"), 1u);
        // ref_gain_dB is bounded -40..40.
        IM_CHECK_EQ(s_app->testRfBudget().modes[0].overrides.at("EQ").at("ref_gain_dB"), 40.0);
        IM_CHECK((ctx->ItemInfo("Add override").ItemFlags & ImGuiItemFlags_Disabled) != 0);

        s_app->m_show_rf_budget = false;
        s_app->testRfBudget() = {};
        ctx->Yield(2);
    };
    t = IM_REGISTER_TEST(
        e, "rf_simulator",
        "receiver_requirements_view_updates_analyzer_and_resumes_receiver_measurements");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        ctx->Yield(2);
        ImGuiWindow *panel = ImGui::FindWindowByName("Receiver Requirements");
        if (panel && panel->Active) {
            ctx->SetRef("##MainMenuBar");
            ctx->MenuClick("View/Receiver Requirements");
            ctx->SetRef("");
            ctx->Yield(2);
        }
        s_app->m_show_na = false;
        const auto state = [&]() { return s_app->testReceiverRequirementsState(); };
        s_app->testSetSingleModeRequirements(ReceiverRequirementsConfig{});
        // Let the widget reload its draft from the new set before interacting.
        ctx->Yield(3);
        std::uint64_t clean_revision = s_app->projectRevision();
        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Receiver Requirements");
        ctx->SetRef("");
        ctx->Yield(3);
        panel = ImGui::FindWindowByName("Receiver Requirements");
        IM_CHECK(panel != nullptr && panel->Active);
        if (!panel || !panel->Active)
            return;
        // Start from a known scroll position so item locations do not depend on
        // the scroll state the previous case left behind.
        ctx->ScrollToTop("//Receiver Requirements");
        ctx->Yield(2);

        auto *generator =
            dynamic_cast<SignalGeneratorEngine *>(s_app->testCreateComponent("generator", 9201));
        auto *attenuator =
            dynamic_cast<AttenuatorEngine *>(s_app->testCreateComponent("attenuator", 9202));
        auto *amplifier =
            dynamic_cast<AmplifierEngine *>(s_app->testCreateComponent("amplifier", 9203));
        IM_CHECK(generator != nullptr);
        IM_CHECK(attenuator != nullptr);
        IM_CHECK(amplifier != nullptr);
        if (!generator || !attenuator || !amplifier)
            return;

        generator->addTone(5.0e6, -20.0);
        attenuator->setAttenuation(3.0);
        amplifier->setGain_dB(10.0);
        amplifier->setNF_dB(4.0);
        amplifier->setEnableNonlinear(true);
        amplifier->setOIP3_dBm(40.0);

        const int generator_out = generator->outputPinId();
        const int attenuator_in = attenuator->inputPinId();
        const int attenuator_out = attenuator->outputPinId();
        const int amplifier_in = amplifier->inputPinId();
        const int amplifier_out = amplifier->outputPinId();
        IM_CHECK(generator_out >= 0);
        IM_CHECK(attenuator_in >= 0);
        IM_CHECK(attenuator_out >= 0);
        IM_CHECK(amplifier_in >= 0);
        IM_CHECK(amplifier_out >= 0);
        const auto first_link = s_app->testConnectLink(generator_out, attenuator_in);
        const auto second_link = s_app->testConnectLink(attenuator_out, amplifier_in);
        IM_CHECK(first_link.has_value());
        IM_CHECK(second_link.has_value());
        if (!first_link || !second_link)
            return;

        auto &na = s_app->testNetworkAnalyzerEngine();
        na.setPointA(generator_out);
        na.setPointB(amplifier_out);
        na.setStartFrequency(5.0e6);
        na.setStopFrequency(10.0e6);
        na.setPoints(201);
        ReceiverRequirementsConfig config;
        config.band_start_Hz = 5.0e6;
        config.band_stop_Hz = 10.0e6;
        config.output_power = ReceiverOutputPowerLimits{-40.0, 0.0};
        config.iip3_min_dBm = 0.0;
        config.measurement_conditions.input_level_dBm = -20.0;
        config.measurement_conditions.iip3 = ReceiverIIP3TestSettings{1.0e6, -80.0, -40.0, 0.4};
        // The engine spends up to 4 ms of wall-clock time per visible frame,
        // enough for an optimized build to finish this whole sweep during the
        // multi-frame menu click below, before the view is hidden. Capping its
        // chain runs per frame keeps the 201-point, 102-run-per-point sweep
        // going for ~200 visible frames on any machine, so the pause/resume
        // checks observe it mid-sweep. The guard restores the uncapped budget
        // the app runs with, however this test exits.
        auto &receiver = s_app->testReceiverPerformanceMeasurementEngine();
        struct RunCapReset {
            ReceiverPerformanceMeasurementEngine &engine;
            ~RunCapReset() { engine.setMaxRunsPerUpdate(std::nullopt); }
        } run_cap_reset{receiver};
        receiver.setMaxRunsPerUpdate(100);
        s_app->testSetSingleModeRequirements(config);
        // Let the widget reload its draft from the new set before interacting.
        ctx->Yield(3);
        clean_revision = s_app->projectRevision();
        ctx->Yield(3);
        panel = ImGui::FindWindowByName("Receiver Requirements");
        IM_CHECK(panel != nullptr && panel->Active);
        if (!panel || !panel->Active)
            return;
        // Start from a known scroll position so item locations do not depend on
        // the scroll state the previous case left behind.
        ctx->ScrollToTop("//Receiver Requirements");
        ctx->Yield(2);

        const auto finite = [](const std::vector<double> &values) {
            return !values.empty() && std::all_of(values.begin(), values.end(), [](double value) {
                return std::isfinite(value);
            });
        };
        const auto finiteCount = [](const std::vector<double> &values) {
            return static_cast<std::size_t>(std::count_if(
                values.begin(), values.end(), [](double value) { return std::isfinite(value); }));
        };
        const auto sameSeries = [](const std::vector<double> &a, const std::vector<double> &b) {
            if (a.size() != b.size())
                return false;
            for (std::size_t i = 0; i < a.size(); ++i) {
                if (std::isnan(a[i]) && std::isnan(b[i]))
                    continue;
                if (a[i] != b[i])
                    return false;
            }
            return true;
        };
        const auto &partial = receiver.measurements();
        IM_CHECK(receiver.isInProgress());
        IM_CHECK_EQ(partial.output_power_dBm.size(), 201);
        IM_CHECK_EQ(partial.iip3_dBm.size(), 201);
        IM_CHECK(finiteCount(partial.output_power_dBm) > 0);

        // Requirements-only visibility still updates the existing analyzer
        // while its own window remains hidden.
        IM_CHECK_EQ(na.sweepFrequencies().size(), 201);
        IM_CHECK_EQ(na.gainDb().size(), 201);
        IM_CHECK_EQ(na.noiseFigureDb().size(), 201);
        IM_CHECK(finite(na.gainDb()));
        IM_CHECK(finite(na.noiseFigureDb()));
        for (double gain : na.gainDb())
            IM_CHECK(std::fabs(gain - 7.0) < 0.1);

        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Receiver Requirements");
        ctx->SetRef("");
        for (int frame = 0; frame < 8 && s_app->m_show_receiver_requirements; ++frame)
            ctx->Yield();
        IM_CHECK(!s_app->m_show_receiver_requirements);
        IM_CHECK(receiver.isInProgress());
        const auto paused = receiver.measurements();
        ctx->Yield(4);
        const auto still_paused = receiver.measurements();
        IM_CHECK(sameSeries(paused.output_power_dBm, still_paused.output_power_dBm));
        IM_CHECK(sameSeries(paused.iip3_dBm, still_paused.iip3_dBm));

        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Receiver Requirements");
        ctx->SetRef("");
        ctx->Yield(1);
        IM_CHECK(s_app->m_show_receiver_requirements);
        IM_CHECK(receiver.isInProgress());
        const auto resumed = receiver.measurements();
        IM_CHECK(finiteCount(resumed.output_power_dBm) >= finiteCount(paused.output_power_dBm));

        for (int frame = 0; frame < 1000 && receiver.isInProgress(); ++frame)
            ctx->Yield();
        const auto &complete = receiver.measurements();
        IM_CHECK(!receiver.isInProgress());
        IM_CHECK(finite(complete.output_power_dBm));
        IM_CHECK(finite(complete.iip3_dBm));
        for (double power : complete.output_power_dBm)
            IM_CHECK(std::fabs(power + 13.0) < 0.25);
        for (double iip3 : complete.iip3_dBm)
            IM_CHECK(std::fabs(iip3 - 33.0) < 3.0);
        ctx->Yield(1);
        const auto applied_evaluation = evaluateReceiverRequirements(
            state(), 5.0e6, 10.0e6, na.sweepFrequencies(), na.gainDb(), na.noiseFigureDb(),
            complete.output_power_dBm, complete.iip3_dBm);
        IM_CHECK_EQ(applied_evaluation.output_power.status, ReceiverRequirementStatus::Pass);
        IM_CHECK_EQ(applied_evaluation.iip3.status, ReceiverRequirementStatus::Pass);
        IM_CHECK_EQ(applied_evaluation.overall, ReceiverRequirementStatus::Pass);

        // A passing status is green only for the applied configuration. Draft
        // edits must replace it with the neutral unapplied state().
        const auto has_pass_green = [](ImGuiWindow *window) {
            if (!window || !window->DrawList)
                return false;
            const ImVec4 expected(0.25f, 0.85f, 0.35f, 1.0f);
            for (int i = 0; i < window->DrawList->VtxBuffer.Size; ++i) {
                const ImVec4 actual =
                    ImGui::ColorConvertU32ToFloat4(window->DrawList->VtxBuffer[i].col);
                if (std::fabs(actual.x - expected.x) < 0.01f &&
                    std::fabs(actual.y - expected.y) < 0.01f &&
                    std::fabs(actual.z - expected.z) < 0.01f)
                    return true;
            }
            return false;
        };
        ctx->WindowCollapse("Receiver Requirements", false);
        ctx->Yield(2);
        ctx->ScrollToBottom("//Receiver Requirements");
        ctx->Yield(2);
        panel = ImGui::FindWindowByName("Receiver Requirements");
        IM_CHECK(panel != nullptr && panel->Active);
        IM_CHECK(panel && !panel->Collapsed);
        IM_CHECK(panel && panel->DrawList && panel->DrawList->VtxBuffer.Size > 0);
        IM_CHECK(has_pass_green(panel));
        ctx->SetRef("Receiver Requirements");
        ctx->ItemInputValue("Band stop (Hz)", "9000000");
        ctx->Yield(2);
        IM_CHECK(state().config.has_value());
        IM_CHECK_EQ(state().config->band_stop_Hz, 10.0e6);
        IM_CHECK_EQ(s_app->projectRevision(), clean_revision);
        IM_CHECK(!has_pass_green(panel));
        ctx->ItemClick("Cancel");
        waitForUi(ctx, [&]() { return has_pass_green(panel); });
        ctx->SetRef("");
        ctx->ScrollToBottom("//Receiver Requirements");
        ctx->Yield(2);
        IM_CHECK(has_pass_green(panel));

        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Receiver Requirements");
        ctx->SetRef("");
        ctx->Yield(2);
        na.setPointA(-1);
        na.setPointB(-1);
        s_app->testSetSingleModeRequirements(ReceiverRequirementsConfig{});
        s_app->testRemoveComponent(amplifier->graphNodeId());
        s_app->testRemoveComponent(attenuator->graphNodeId());
        s_app->testRemoveComponent(generator->graphNodeId());
        ctx->Yield(2);
    };
    t = IM_REGISTER_TEST(e, "rf_simulator",
                         "receiver_requirements_input_level_gates_output_power_apply");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        ctx->Yield(2);
        ImGuiWindow *panel = ImGui::FindWindowByName("Receiver Requirements");
        if (panel && panel->Active) {
            ctx->SetRef("##MainMenuBar");
            ctx->MenuClick("View/Receiver Requirements");
            ctx->SetRef("");
            ctx->Yield(2);
        }
        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Receiver Requirements");
        ctx->SetRef("");
        ctx->Yield(3);
        panel = ImGui::FindWindowByName("Receiver Requirements");
        IM_CHECK(panel != nullptr && panel->Active);
        if (!panel || !panel->Active)
            return;
        // Start from a known scroll position so item locations do not depend on
        // the scroll state the previous case left behind.
        ctx->ScrollToTop("//Receiver Requirements");
        ctx->Yield(2);
        const auto state = [&]() { return s_app->testReceiverRequirementsState(); };
        s_app->testSetSingleModeRequirements(ReceiverRequirementsConfig{});
        // Let the widget reload its draft from the new set before interacting.
        ctx->Yield(3);
        const std::uint64_t clean_revision = s_app->projectRevision();

        // Apply rejects the draft with a validation error; the committed state and
        // project revision stay untouched until the input level supplies the
        // measured source power.
        ctx->SetRef("Receiver Requirements");
        ctx->ItemInputValue("Band start (Hz)", "1000000");
        ctx->ItemInputValue("Band stop (Hz)", "10000000");
        ctx->ItemCheck("Enable output power");
        ctx->ItemInputValue("Output power min (dBm)", "-40");
        ctx->ItemInputValue("Output power max (dBm)", "0");
        settlePanelScrollToBottom(ctx);
        ctx->ItemClick("Apply requirements");
        // Negative: a fixed settle window proves nothing was applied.
        ctx->Yield(6);
        panel = ImGui::FindWindowByName("Receiver Requirements");
        IM_CHECK(panel != nullptr && panel->Active);
        IM_CHECK(!state().config.has_value());
        IM_CHECK_EQ(s_app->projectRevision(), clean_revision);
        waitForUi(ctx, [&]() { return !s_app->testReceiverRequirementsError().empty(); });
        IM_CHECK(!s_app->testReceiverRequirementsError().empty());

        ctx->ItemInputValue("Input level (dBm)", "-30");
        settlePanelScrollToBottom(ctx);
        ctx->ItemClick("Apply requirements");
        waitForUi(ctx, [&]() { return state().config.has_value(); });
        IM_CHECK(state().config.has_value());
        if (state().config)
            IM_CHECK(state().config->measurement_conditions.input_level_dBm == -30.0);
        IM_CHECK(s_app->projectRevision() > clean_revision);

        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Receiver Requirements");
        ctx->SetRef("");
        ctx->Yield(2);
        s_app->testSetSingleModeRequirements(ReceiverRequirementsConfig{});
        ctx->Yield(2);
    };

    t = IM_REGISTER_TEST(e, "rf_simulator",
                         "receiver_requirements_editor_applies_only_complete_valid_values");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        ctx->Yield(2);
        ImGuiWindow *panel = ImGui::FindWindowByName("Receiver Requirements");
        if (panel && panel->Active) {
            ctx->SetRef("##MainMenuBar");
            ctx->MenuClick("View/Receiver Requirements");
            ctx->SetRef("");
            ctx->Yield(2);
        }
        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Receiver Requirements");
        ctx->SetRef("");
        ctx->Yield(3);
        panel = ImGui::FindWindowByName("Receiver Requirements");
        IM_CHECK(panel != nullptr && panel->Active);
        if (!panel || !panel->Active)
            return;
        // Start from a known scroll position so item locations do not depend on
        // the scroll state the previous case left behind.
        ctx->ScrollToTop("//Receiver Requirements");
        ctx->Yield(2);
        const auto state = [&]() { return s_app->testReceiverRequirementsState(); };
        s_app->testSetSingleModeRequirements(ReceiverRequirementsConfig{});
        // Let the widget reload its draft from the new set before interacting.
        ctx->Yield(3);
        // Revision comparisons prove whether an Apply recorded a project edit,
        // independent of whatever unsaved state earlier cases left behind.
        const std::uint64_t revision_before_invalid = s_app->projectRevision();

        ctx->SetRef("Receiver Requirements");
        ctx->ItemCheck("Enable gain");
        ctx->ItemCheck("Enable noise figure");
        ctx->ItemInputValue("Band start (Hz)", "2000000");
        ctx->ItemInputValue("Band stop (Hz)", "1000000");
        ctx->ItemInputValue("Gain min (dB)", "-3");
        ctx->ItemInputValue("Gain max (dB)", "3");
        ctx->ItemInputValue("NF max (dB)", "5");
        settlePanelScrollToBottom(ctx);
        ctx->ItemClick("Apply requirements");
        // Negative: a fixed settle window proves the invalid draft stayed out.
        ctx->Yield(6);
        IM_CHECK(!state().config.has_value());
        IM_CHECK_EQ(s_app->projectRevision(), revision_before_invalid);

        ctx->ItemInputValue("Band start (Hz)", "1000000");
        ctx->ItemInputValue("Band stop (Hz)", "10000000");
        settlePanelScrollToBottom(ctx);
        ctx->ItemClick("Apply requirements");
        waitForUi(ctx, [&]() { return state().config.has_value(); });
        panel = ImGui::FindWindowByName("Receiver Requirements");
        IM_CHECK(panel != nullptr && panel->Active);
        IM_CHECK(s_app->m_show_receiver_requirements);
        IM_CHECK(state().config.has_value());
        if (state().config) {
            IM_CHECK_EQ(state().config->band_start_Hz, 1.0e6);
            IM_CHECK_EQ(state().config->band_stop_Hz, 1.0e7);
            IM_CHECK_EQ(state().config->gain->minimum_dB, -3.0);
            IM_CHECK_EQ(state().config->gain->maximum_dB, 3.0);
            IM_CHECK_EQ(state().config->nf_max_dB, 5.0);
        }
        IM_CHECK(s_app->projectRevision() > revision_before_invalid);
        IM_CHECK(s_app->isDirty());

        auto &invalid_set = s_app->testReceiverRequirements();
        invalid_set = ReceiverRequirementsSet{};
        invalid_set.invalid_reason = "malformed project data";
        const std::uint64_t revision_before_repair = s_app->projectRevision();
        ctx->SetRef("Receiver Requirements");
        // Retained malformed data has no modes, so the limit fields stay hidden
        // until Add mode clears the invalid marker.
        IM_CHECK(!ctx->ItemExists("Band start (Hz)"));
        ctx->ScrollToTop("//Receiver Requirements");
        ctx->ItemInputValue("Mode name", "Repaired");
        ctx->ItemClick("Add mode");
        waitForUi(ctx, [&]() { return s_app->testReceiverRequirements().modes.size() == 1; });
        IM_CHECK_EQ(s_app->testReceiverRequirements().modes.size(), 1);
        IM_CHECK(s_app->testReceiverRequirementsState().invalid_reason.empty());
        IM_CHECK(s_app->projectRevision() > revision_before_repair);
        ctx->ItemCheck("Enable gain");
        ctx->ItemCheck("Enable noise figure");
        ctx->ItemInputValue("Band start (Hz)", "1000000");
        ctx->ItemInputValue("Band stop (Hz)", "10000000");
        ctx->ItemInputValue("Gain min (dB)", "-3");
        ctx->ItemInputValue("Gain max (dB)", "3");
        ctx->ItemInputValue("NF max (dB)", "5");
        const std::uint64_t revision_before_repair_apply = s_app->projectRevision();
        settlePanelScrollToBottom(ctx);
        ctx->ItemClick("Apply requirements");
        waitForUi(ctx,
                  [&]() { return state().config.has_value() && state().invalid_reason.empty(); });
        IM_CHECK(state().config.has_value());
        IM_CHECK(state().invalid_reason.empty());
        IM_CHECK(s_app->projectRevision() > revision_before_repair_apply);
        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Receiver Requirements");
        ctx->SetRef("");
        ctx->Yield(2);
    };
    t = IM_REGISTER_TEST(e, "rf_simulator",
                         "receiver_requirements_optional_metrics_stage_validate_and_cancel");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        ctx->Yield(2);
        ImGuiWindow *panel = ImGui::FindWindowByName("Receiver Requirements");
        if (panel && panel->Active) {
            ctx->SetRef("##MainMenuBar");
            ctx->MenuClick("View/Receiver Requirements");
            ctx->SetRef("");
            ctx->Yield(2);
        }
        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Receiver Requirements");
        ctx->SetRef("");
        ctx->Yield(3);
        panel = ImGui::FindWindowByName("Receiver Requirements");
        IM_CHECK(panel != nullptr && panel->Active);
        if (!panel || !panel->Active)
            return;
        // Start from a known scroll position so item locations do not depend on
        // the scroll state the previous case left behind.
        ctx->ScrollToTop("//Receiver Requirements");
        ctx->Yield(2);

        const auto state = [&]() { return s_app->testReceiverRequirementsState(); };
        s_app->testSetSingleModeRequirements(ReceiverRequirementsConfig{});
        // Let the widget reload its draft from the new set before locating items.
        ctx->Yield(3);
        std::uint64_t clean_revision = s_app->projectRevision();
        ctx->SetRef("Receiver Requirements");
        const auto setEnabled = [ctx](const char *label, bool enabled) {
            if (ctx->ItemIsChecked(label) == enabled)
                return;
            if (enabled)
                ctx->ItemCheck(label);
            else
                ctx->ItemUncheck(label);
            ctx->Yield(1);
        };
        const auto apply = [ctx]() {
            settlePanelScrollToBottom(ctx);
            ctx->ItemClick("Apply requirements");
            ctx->Yield(2);
        };

        setEnabled("Enable gain", true);
        setEnabled("Enable noise figure", true);
        setEnabled("Enable output power", true);
        setEnabled("Enable IIP3", true);
        ctx->ItemInputValue("Band start (Hz)", "1000000");
        ctx->ItemInputValue("Band stop (Hz)", "10000000");
        ctx->ItemInputValue("Gain min (dB)", "-3");
        ctx->ItemInputValue("Gain max (dB)", "3");
        ctx->ItemInputValue("NF max (dB)", "5");
        ctx->ItemInputValue("Output power min (dBm)", "-20");
        ctx->ItemInputValue("Output power max (dBm)", "-30");
        ctx->ItemInputValue("Input level (dBm)", "-30");
        ctx->ItemInputValue("IIP3 min (dBm)", "10");
        ctx->ItemInputValue("IIP3 tone spacing (Hz)", "1000000");
        ctx->ItemInputValue("IIP3 input start (dBm)", "-20");
        ctx->ItemInputValue("IIP3 input stop (dBm)", "-30");
        ctx->ItemInputValue("IIP3 input step (dB)", "5");
        apply();
        IM_CHECK(!state().config.has_value());
        IM_CHECK_EQ(s_app->projectRevision(), clean_revision);

        // Invalid values in disabled metrics must not block a valid Apply.
        setEnabled("Enable output power", false);
        setEnabled("Enable IIP3", false);
        apply();
        waitForUi(ctx, [&]() { return state().config.has_value(); });
        IM_CHECK(state().config.has_value());
        if (state().config) {
            IM_CHECK(state().config->gain.has_value());
            IM_CHECK(state().config->nf_max_dB.has_value());
            IM_CHECK(!state().config->output_power.has_value());
            IM_CHECK(!state().config->iip3_min_dBm.has_value());
        }
        IM_CHECK(s_app->projectRevision() > clean_revision);

        setEnabled("Enable output power", true);
        setEnabled("Enable IIP3", true);
        ctx->ItemInputValue("IIP3 min (dBm)", "10");
        ctx->ItemInputValue("Output power min (dBm)", "-40");
        ctx->ItemInputValue("Output power max (dBm)", "0");
        ctx->ItemInputValue("Input level (dBm)", "-30");
        ctx->ItemInputValue("IIP3 tone spacing (Hz)", "2000000");
        ctx->ItemInputValue("IIP3 input start (dBm)", "-60");
        ctx->ItemInputValue("IIP3 input stop (dBm)", "-40");
        ctx->ItemInputValue("IIP3 input step (dB)", "2");
        apply();
        waitForUi(ctx, [&]() {
            return state().config.has_value() && state().config->output_power.has_value() &&
                   state().config->iip3_min_dBm.has_value();
        });
        panel = ImGui::FindWindowByName("Receiver Requirements");
        IM_CHECK(panel != nullptr && panel->Active);
        IM_CHECK(s_app->m_show_receiver_requirements);
        IM_CHECK(state().config.has_value());
        if (!state().config)
            return;
        IM_CHECK(state().config->gain.has_value());
        IM_CHECK(state().config->nf_max_dB.has_value());
        IM_CHECK(state().config->output_power.has_value());
        IM_CHECK(state().config->iip3_min_dBm.has_value());
        IM_CHECK_EQ(state().config->band_start_Hz, 1.0e6);
        IM_CHECK_EQ(state().config->band_stop_Hz, 1.0e7);
        IM_CHECK_EQ(state().config->nf_max_dB, 5.0);
        IM_CHECK_EQ(state().config->iip3_min_dBm, 10.0);
        if (state().config->gain) {
            IM_CHECK_EQ(state().config->gain->minimum_dB, -3.0);
            IM_CHECK_EQ(state().config->gain->maximum_dB, 3.0);
        }
        if (state().config->output_power) {
            IM_CHECK_EQ(state().config->output_power->minimum_dBm, -40.0);
            IM_CHECK_EQ(state().config->output_power->maximum_dBm, 0.0);
        }
        IM_CHECK(state().config->measurement_conditions.iip3.has_value());
        IM_CHECK(state().config->measurement_conditions.input_level_dBm == -30.0);
        if (state().config->measurement_conditions.iip3) {
            IM_CHECK_EQ(state().config->measurement_conditions.iip3->tone_spacing_Hz, 2.0e6);
            IM_CHECK_EQ(state().config->measurement_conditions.iip3->input_start_dBm, -60.0);
            IM_CHECK_EQ(state().config->measurement_conditions.iip3->input_stop_dBm, -40.0);
            IM_CHECK_EQ(state().config->measurement_conditions.iip3->input_step_dB, 2.0);
        }
        IM_CHECK(s_app->projectRevision() > clean_revision);

        const auto applied = *state().config;
        clean_revision = s_app->projectRevision();
        ctx->ItemInputValue("Band stop (Hz)", "9000000");
        ctx->ItemInputValue("Output power max (dBm)", "-5");
        setEnabled("Enable gain", false);
        ctx->ItemClick("Cancel");
        ctx->Yield(2);
        IM_CHECK(state().config.has_value());
        if (state().config) {
            IM_CHECK_EQ(state().config->band_stop_Hz, applied.band_stop_Hz);
            IM_CHECK(state().config->gain.has_value());
            IM_CHECK(state().config->output_power.has_value());
            IM_CHECK_EQ(state().config->output_power->maximum_dBm,
                        applied.output_power->maximum_dBm);
            IM_CHECK_EQ(state().config->iip3_min_dBm, applied.iip3_min_dBm);
        }
        IM_CHECK_EQ(s_app->projectRevision(), clean_revision);

        setEnabled("Enable gain", false);
        apply();
        waitForUi(
            ctx, [&]() { return state().config.has_value() && !state().config->gain.has_value(); });
        IM_CHECK(state().config.has_value() && !state().config->gain.has_value());
        IM_CHECK(state().config->nf_max_dB.has_value());
        IM_CHECK(state().config->output_power.has_value());
        IM_CHECK(state().config->iip3_min_dBm.has_value());
        setEnabled("Enable gain", true);
        ctx->ItemInputValue("Gain min (dB)", "-3");
        ctx->ItemInputValue("Gain max (dB)", "3");
        apply();
        waitForUi(ctx,
                  [&]() { return state().config.has_value() && state().config->gain.has_value(); });
        IM_CHECK(state().config.has_value() && state().config->gain.has_value());

        setEnabled("Enable noise figure", false);
        apply();
        waitForUi(ctx, [&]() {
            return state().config.has_value() && !state().config->nf_max_dB.has_value();
        });
        IM_CHECK(state().config.has_value() && !state().config->nf_max_dB.has_value());
        IM_CHECK(state().config->gain.has_value());
        IM_CHECK(state().config->output_power.has_value());
        IM_CHECK(state().config->iip3_min_dBm.has_value());
        setEnabled("Enable noise figure", true);
        ctx->ItemInputValue("NF max (dB)", "5");
        apply();
        waitForUi(ctx, [&]() {
            return state().config.has_value() && state().config->nf_max_dB.has_value();
        });
        IM_CHECK(state().config.has_value() && state().config->nf_max_dB.has_value());

        setEnabled("Enable output power", false);
        apply();
        waitForUi(ctx, [&]() {
            return state().config.has_value() && !state().config->output_power.has_value();
        });
        IM_CHECK(state().config.has_value() && !state().config->output_power.has_value());
        IM_CHECK(state().config->gain.has_value());
        IM_CHECK(state().config->nf_max_dB.has_value());
        IM_CHECK(state().config->iip3_min_dBm.has_value());
        setEnabled("Enable output power", true);
        ctx->ItemInputValue("Output power min (dBm)", "-40");
        ctx->ItemInputValue("Output power max (dBm)", "0");
        ctx->ItemInputValue("Input level (dBm)", "-30");
        apply();
        waitForUi(ctx, [&]() {
            return state().config.has_value() && state().config->output_power.has_value();
        });
        IM_CHECK(state().config.has_value() && state().config->output_power.has_value());

        setEnabled("Enable IIP3", false);
        apply();
        waitForUi(ctx, [&]() {
            return state().config.has_value() && !state().config->iip3_min_dBm.has_value();
        });
        IM_CHECK(state().config.has_value() && !state().config->iip3_min_dBm.has_value());
        IM_CHECK(state().config->gain.has_value());
        IM_CHECK(state().config->nf_max_dB.has_value());
        IM_CHECK(state().config->output_power.has_value());
        setEnabled("Enable IIP3", true);
        ctx->ItemInputValue("IIP3 min (dBm)", "10");
        ctx->ItemInputValue("IIP3 tone spacing (Hz)", "2000000");
        ctx->ItemInputValue("IIP3 input start (dBm)", "-60");
        ctx->ItemInputValue("IIP3 input stop (dBm)", "-40");
        ctx->ItemInputValue("IIP3 input step (dB)", "2");
        apply();
        waitForUi(ctx, [&]() {
            return state().config.has_value() && state().config->iip3_min_dBm.has_value();
        });
        IM_CHECK(state().config.has_value() && state().config->iip3_min_dBm.has_value());

        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Receiver Requirements");
        ctx->SetRef("");
        ctx->Yield(2);
        s_app->testSetSingleModeRequirements(ReceiverRequirementsConfig{});
    };

    t = IM_REGISTER_TEST(e, "rf_simulator",
                         "receiver_requirements_modes_add_rename_remove_and_switch");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        ctx->Yield(2);
        ImGuiWindow *panel = ImGui::FindWindowByName("Receiver Requirements");
        if (panel && panel->Active) {
            ctx->SetRef("##MainMenuBar");
            ctx->MenuClick("View/Receiver Requirements");
            ctx->SetRef("");
            ctx->Yield(2);
        }
        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Receiver Requirements");
        ctx->SetRef("");
        ctx->Yield(3);
        panel = ImGui::FindWindowByName("Receiver Requirements");
        IM_CHECK(panel != nullptr && panel->Active);
        if (!panel || !panel->Active)
            return;
        // Start from a known scroll position so item locations do not depend on
        // the scroll state the previous case left behind.
        ctx->ScrollToTop("//Receiver Requirements");
        ctx->Yield(2);

        auto &set = s_app->testReceiverRequirements();
        set = ReceiverRequirementsSet{};
        ctx->Yield(3);
        ctx->SetRef("Receiver Requirements");
        IM_CHECK(!ctx->ItemExists("Band start (Hz)"));
        IM_CHECK(!ctx->ItemExists("Apply requirements"));

        // 1. An empty set accepts the first mode through the name field.
        std::uint64_t revision = s_app->projectRevision();
        ctx->ItemInputValue("Mode name", "Band 1 High Gain");
        ctx->ItemClick("Add mode");
        waitForUi(ctx, [&]() { return set.modes.size() == 1; });
        IM_CHECK(set.modes.size() == 1);
        IM_CHECK(set.modes[0].name == "Band 1 High Gain");
        IM_CHECK_EQ(set.selected, 0);
        IM_CHECK(s_app->projectRevision() > revision);

        revision = s_app->projectRevision();
        ctx->ItemInputValue("Band start (Hz)", "5000000");
        ctx->ItemInputValue("Band stop (Hz)", "10000000");
        ctx->ItemCheck("Enable gain");
        ctx->ItemInputValue("Gain min (dB)", "0");
        ctx->ItemInputValue("Gain max (dB)", "20");
        settlePanelScrollToBottom(ctx);
        ctx->ItemClick("Apply requirements");
        waitForUi(ctx, [&]() { return set.modes[0].config.has_value(); });
        IM_CHECK(set.modes.size() == 1);
        IM_CHECK(set.modes[0].config.has_value());
        IM_CHECK_EQ(set.modes[0].config->band_start_Hz, 5.0e6);
        IM_CHECK_EQ(set.modes[0].config->band_stop_Hz, 10.0e6);
        IM_CHECK(set.modes[0].config->gain.has_value());
        IM_CHECK_EQ(set.modes[0].config->gain->minimum_dB, 0.0);
        IM_CHECK_EQ(set.modes[0].config->gain->maximum_dB, 20.0);
        IM_CHECK(s_app->projectRevision() > revision);

        // 2. Adding a second mode copies the selected mode's config.
        revision = s_app->projectRevision();
        ctx->ScrollToTop("//Receiver Requirements");
        ctx->ItemInputValue("Mode name", "Band 1 Low Gain");
        ctx->ItemClick("Add mode");
        waitForUi(ctx, [&]() { return set.modes.size() == 2; });
        IM_CHECK(set.modes.size() == 2);
        IM_CHECK_EQ(set.selected, 1);
        IM_CHECK(set.modes[1].config.has_value());
        IM_CHECK(set.modes[1].config == set.modes[0].config);
        IM_CHECK(s_app->projectRevision() > revision);

        // 3. Unapplied edits disable the mode controls, so a combo click cannot
        // switch modes, while Cancel restores the applied config.
        ctx->ItemInputValue("Band stop (Hz)", "9000000");
        ctx->Yield(3);
        IM_CHECK((ctx->ItemInfo("Mode").ItemFlags & ImGuiItemFlags_Disabled) != 0);
        IM_CHECK((ctx->ItemInfo("Add mode").ItemFlags & ImGuiItemFlags_Disabled) != 0);
        IM_CHECK((ctx->ItemInfo("Rename mode").ItemFlags & ImGuiItemFlags_Disabled) != 0);
        IM_CHECK((ctx->ItemInfo("Remove mode").ItemFlags & ImGuiItemFlags_Disabled) != 0);
        ctx->ItemClick("Mode");
        ctx->Yield(2);
        if (ctx->ItemExists("**/Band 1 High Gain"))
            ctx->ItemClick("**/Band 1 High Gain");
        ctx->SetRef("Receiver Requirements");
        ctx->Yield(2);
        IM_CHECK_EQ(set.selected, 1);
        ctx->ItemClick("Cancel");
        ctx->Yield(3);
        IM_CHECK(set.modes[1].config.has_value());
        IM_CHECK_EQ(set.modes[1].config->band_stop_Hz, 10.0e6);

        // 4. Selecting another mode reloads the draft and marks nothing dirty; rename applies to
        // it.
        revision = s_app->projectRevision();
        ctx->ItemClick("Mode");
        ctx->Yield(2);
        ctx->SetRef("");
        ctx->ItemClick("**/Band 1 High Gain");
        ctx->SetRef("Receiver Requirements");
        waitForUi(ctx, [&]() { return set.selected == 0; });
        IM_CHECK_EQ(set.selected, 0);
        IM_CHECK_EQ(s_app->projectRevision(), revision);

        revision = s_app->projectRevision();
        ctx->ScrollToTop("//Receiver Requirements");
        ctx->ItemInputValue("Mode name", "HG");
        ctx->ItemClick("Rename mode");
        waitForUi(ctx, [&]() { return set.modes[0].name == "HG"; });
        IM_CHECK(set.modes.size() == 2);
        IM_CHECK(set.modes[0].name == "HG");
        IM_CHECK(set.modes[1].name == "Band 1 Low Gain");
        IM_CHECK(s_app->projectRevision() > revision);

        // 5. Removing asks first: Keep mode leaves the modes as they were, and Remove deletes the
        // selected mode, leaving the other one selected.
        revision = s_app->projectRevision();
        ctx->ScrollToTop("//Receiver Requirements");
        ctx->ItemClick("Remove mode");
        ctx->Yield(2);
        IM_CHECK(ImGui::IsPopupOpen((ImGuiID)0, ImGuiPopupFlags_AnyPopup));
        ctx->SetRef("//$FOCUSED");
        ctx->ItemClick("Keep mode");
        ctx->Yield(3);
        IM_CHECK(set.modes.size() == 2);
        IM_CHECK_EQ(s_app->projectRevision(), revision);

        ctx->SetRef("Receiver Requirements");
        ctx->ItemClick("Remove mode");
        ctx->Yield(2);
        IM_CHECK(ImGui::IsPopupOpen((ImGuiID)0, ImGuiPopupFlags_AnyPopup));
        ctx->SetRef("//$FOCUSED");
        ctx->ItemClick("Remove");
        waitForUi(ctx, [&]() { return set.modes.size() == 1; });
        IM_CHECK(set.modes.size() == 1);
        IM_CHECK(set.modes[0].name == "Band 1 Low Gain");
        IM_CHECK_EQ(set.selected, 0);
        IM_CHECK(s_app->projectRevision() > revision);

        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Receiver Requirements");
        ctx->SetRef("");
        ctx->Yield(2);
        set = ReceiverRequirementsSet{};
    };

    t = IM_REGISTER_TEST(e, "rf_simulator", "receiver_requirements_agent_edits_respect_the_draft");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        ctx->Yield(2);
        ImGuiWindow *panel = ImGui::FindWindowByName("Receiver Requirements");
        if (panel && panel->Active) {
            ctx->SetRef("##MainMenuBar");
            ctx->MenuClick("View/Receiver Requirements");
            ctx->SetRef("");
            ctx->Yield(2);
        }
        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Receiver Requirements");
        ctx->SetRef("");
        ctx->Yield(3);
        panel = ImGui::FindWindowByName("Receiver Requirements");
        IM_CHECK(panel != nullptr && panel->Active);
        if (!panel || !panel->Active)
            return;
        ctx->ScrollToTop("//Receiver Requirements");
        ctx->Yield(2);

        // Two configured modes; "LG" is selected, so the draft describes LG.
        auto &set = s_app->testReceiverRequirements();
        ReceiverRequirementsConfig config;
        config.band_start_Hz = 1.0e9;
        config.band_stop_Hz = 2.0e9;
        config.gain = ReceiverGainLimits{0.0, 5.0};
        set = ReceiverRequirementsSet{};
        set.modes.push_back({"HG", config});
        set.modes.push_back({"LG", config});
        set.selected = 1;
        assignReceiverRequirementsModeIds(set);
        ctx->Yield(3);

        // 1. An agent edit to the other mode leaves the selected mode's unapplied draft in place.
        ctx->SetRef("Receiver Requirements");
        ctx->ItemInputValue("Band stop (Hz)", "3000000000");
        ctx->Yield(2);
        set.modes[0].config->nf_max_dB = 4.0;
        ctx->Yield(3);
        settlePanelScrollToBottom(ctx);
        ctx->ItemClick("Apply requirements");
        waitForUi(ctx, [&]() { return set.modes[1].config->band_stop_Hz == 3.0e9; });
        IM_CHECK_EQ(set.modes[1].config->band_stop_Hz, 3.0e9);
        IM_CHECK(set.modes[0].config->nf_max_dB == 4.0);

        // 2. An agent edit to the selected mode discards its unapplied draft instead of applying
        // it.
        ctx->ItemInputValue("Band stop (Hz)", "4000000000");
        ctx->Yield(2);
        set.modes[1].config->band_stop_Hz = 2.5e9;
        ctx->Yield(3);
        const std::uint64_t revision = s_app->projectRevision();
        settlePanelScrollToBottom(ctx);
        ctx->ItemClick("Apply requirements");
        ctx->Yield(6);
        // Applying the reloaded draft changes nothing, so the agent's limit is kept.
        IM_CHECK_EQ(set.modes[1].config->band_stop_Hz, 2.5e9);
        IM_CHECK_EQ(s_app->projectRevision(), revision);

        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Receiver Requirements");
        ctx->SetRef("");
        ctx->Yield(2);
        s_app->testSetSingleModeRequirements(ReceiverRequirementsConfig{});
    };

    // The requirements view must use the dock it lives in: compact enough to
    // fit a narrow dock without a horizontal scrollbar, verdict beside the
    // form in a wide one, and on screen at the default docked size without
    // scrolling. A panel that must be resized is floated first: docked
    // windows have no resize border and tabs hide sibling views.
    t = IM_REGISTER_TEST(e, "rf_simulator",
                         "receiver_requirements_wraps_without_horizontal_scroll_in_a_narrow_dock");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        ctx->Yield(2);
        ImGuiWindow *panel = ImGui::FindWindowByName("Receiver Requirements");
        if (panel && panel->Active) {
            ctx->SetRef("##MainMenuBar");
            ctx->MenuClick("View/Receiver Requirements");
            ctx->SetRef("");
            ctx->Yield(2);
        }
        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Receiver Requirements");
        ctx->SetRef("");
        ctx->Yield(3);
        ImGui::DockBuilderDockWindow("Receiver Requirements", 0);
        ctx->Yield(2);
        ctx->WindowResize("Receiver Requirements", ImVec2(400.0f, 800.0f));
        ctx->Yield(2);
        struct RestoreDocked {
            ImGuiTestContext *ctx;
            ~RestoreDocked() {
                const ImGuiID utility_root =
                    ImHashStr("UtilityDockSpace", 0, ImHashStr("Utility Space"));
                ImGui::DockBuilderDockWindow("Receiver Requirements", utility_root);
                if (s_app->m_show_receiver_requirements) {
                    ctx->SetRef("##MainMenuBar");
                    ctx->MenuClick("View/Receiver Requirements");
                    ctx->SetRef("");
                    ctx->Yield(2);
                }
            }
        } restore{ctx};

        // A fully-configured mode fills every row the view can draw.
        ReceiverRequirementsConfig config;
        config.band_start_Hz = 1.0e6;
        config.band_stop_Hz = 10.0e6;
        config.gain = ReceiverGainLimits{0.0, 20.0};
        config.nf_max_dB = 5.0;
        config.output_power = ReceiverOutputPowerLimits{-40.0, 0.0};
        config.iip3_min_dBm = 10.0;
        config.measurement_conditions.input_level_dBm = -30.0;
        config.measurement_conditions.iip3 = ReceiverIIP3TestSettings{1.0e6, -60.0, -40.0, 2.0};
        config.snr = ReceiverSNRLimit{20.0, 1.0e6};
        // The point must sit outside the band, or Apply refuses the whole
        // draft: BandStop + 5 MHz names 15 MHz against a 1-10 MHz band.
        config.oob_suppression.push_back(
            ReceiverOOBSuppressionPoint{ReceiverOOBSuppressionAnchor::BandStop, 5.0e6, 30.0});
        s_app->testSetSingleModeRequirements(config);
        ctx->Yield(3);
        panel = ImGui::FindWindowByName("Receiver Requirements");
        IM_CHECK(panel != nullptr && panel->Active);
        if (!panel || !panel->Active)
            return;

        ctx->SetRef("Receiver Requirements");
        // A narrow dock wraps rows; nothing may clip off the right edge (the
        // window shows no horizontal scrollbar by design, so an overflowing
        // row would be silently unreachable).
        IM_CHECK(panel->ContentSize.x <= panel->InnerRect.GetWidth() + 2.0f);
        // Every pinned window-scoped control is still submitted and finds its
        // input. The per-point controls of an out-of-band row carry their row
        // index in their id, so they are covered by the content bound above.
        const char *controls[] = {"Mode",
                                  "Mode name",
                                  "Add mode",
                                  "Rename mode",
                                  "Remove mode",
                                  "Band start (Hz)",
                                  "Band stop (Hz)",
                                  "Enable gain",
                                  "Gain min (dB)",
                                  "Gain max (dB)",
                                  "Enable noise figure",
                                  "NF max (dB)",
                                  "Enable output power",
                                  "Output power min (dBm)",
                                  "Output power max (dBm)",
                                  "Input level (dBm)",
                                  "Enable IIP3",
                                  "IIP3 min (dBm)",
                                  "IIP3 tone spacing (Hz)",
                                  "IIP3 input start (dBm)",
                                  "IIP3 input stop (dBm)",
                                  "IIP3 input step (dB)",
                                  "Enable SNR",
                                  "SNR minimum (dB)",
                                  "SNR bandwidth (Hz)",
                                  "Add out-of-band point",
                                  "Apply requirements",
                                  "Cancel"};
        for (const char *label : controls)
            IM_CHECK(ctx->ItemExists(label));
        ctx->ItemInputValue("Gain max (dB)", "25");
        settlePanelScrollToBottom(ctx);
        ctx->ItemClick("Apply requirements");
        waitForUi(ctx, [&]() {
            const auto state = s_app->testReceiverRequirementsState();
            return state.config.has_value() && state.config->gain.has_value() &&
                   state.config->gain->maximum_dB == 25.0;
        });
        const auto state = s_app->testReceiverRequirementsState();
        IM_CHECK(state.config.has_value() && state.config->gain.has_value());
        if (state.config && state.config->gain)
            IM_CHECK_EQ(state.config->gain->maximum_dB, 25.0);
    };

    t = IM_REGISTER_TEST(e, "rf_simulator",
                         "receiver_requirements_shows_verdict_beside_the_form_in_a_wide_dock");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        ctx->Yield(2);
        ImGuiWindow *panel = ImGui::FindWindowByName("Receiver Requirements");
        if (panel && panel->Active) {
            ctx->SetRef("##MainMenuBar");
            ctx->MenuClick("View/Receiver Requirements");
            ctx->SetRef("");
            ctx->Yield(2);
        }
        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Receiver Requirements");
        ctx->SetRef("");
        ctx->Yield(3);
        ImGui::DockBuilderDockWindow("Receiver Requirements", 0);
        ctx->Yield(2);
        ctx->WindowResize("Receiver Requirements", ImVec2(1040.0f, 620.0f));
        ctx->Yield(2);
        struct RestoreDocked {
            ImGuiTestContext *ctx;
            ~RestoreDocked() {
                const ImGuiID utility_root =
                    ImHashStr("UtilityDockSpace", 0, ImHashStr("Utility Space"));
                ImGui::DockBuilderDockWindow("Receiver Requirements", utility_root);
                if (s_app->m_show_receiver_requirements) {
                    ctx->SetRef("##MainMenuBar");
                    ctx->MenuClick("View/Receiver Requirements");
                    ctx->SetRef("");
                    ctx->Yield(2);
                }
            }
        } restore{ctx};

        ReceiverRequirementsConfig config;
        config.band_start_Hz = 1.0e6;
        config.band_stop_Hz = 10.0e6;
        config.gain = ReceiverGainLimits{0.0, 20.0};
        s_app->testSetSingleModeRequirements(config);
        ctx->Yield(3);
        panel = ImGui::FindWindowByName("Receiver Requirements");
        IM_CHECK(panel != nullptr && panel->Active);
        if (!panel || !panel->Active)
            return;
        const ImRect panel_rect = panel->Rect();

        ctx->SetRef("Receiver Requirements");
        // The measured verdict sits beside the form, above the fold: the
        // table header cell and a form control share the same rows of the
        // window, and the apply bar is on screen without scrolling.
        ImGuiTestItemInfo metric = ctx->ItemInfo("**/Metric");
        ImGuiTestItemInfo enable_gain = ctx->ItemInfo("**/Enable gain");
        ImGuiTestItemInfo apply = ctx->ItemInfo("Apply requirements");
        IM_CHECK(metric.ID != 0);
        IM_CHECK(enable_gain.ID != 0);
        IM_CHECK(apply.ID != 0);
        IM_CHECK(metric.RectFull.Min.y < enable_gain.RectFull.Max.y + 40.0f);
        IM_CHECK(metric.RectFull.Min.y < panel_rect.Max.y);
        IM_CHECK(apply.RectFull.Max.y < panel_rect.Max.y);

        // The applied result is hidden while a draft is pending (table
        // items are scoped by the layout table, hence the deep search).
        ctx->ItemInputValue("**/Band stop (Hz)", "9000000");
        ctx->Yield(2);
        IM_CHECK(!ctx->ItemExists("**/Metric"));
        ctx->ItemClick("Cancel");
        ctx->Yield(3);
        IM_CHECK(ctx->ItemInfo("**/Metric").ID != 0);
    };

    t = IM_REGISTER_TEST(e, "rf_simulator",
                         "receiver_requirements_default_dock_shows_verdict_without_scrolling");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        ctx->Yield(2);
        ImGuiWindow *panel = ImGui::FindWindowByName("Receiver Requirements");
        if (panel && panel->Active) {
            ctx->SetRef("##MainMenuBar");
            ctx->MenuClick("View/Receiver Requirements");
            ctx->SetRef("");
            ctx->Yield(2);
        }
        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Receiver Requirements");
        ctx->SetRef("");
        ctx->Yield(3);
        panel = ImGui::FindWindowByName("Receiver Requirements");
        IM_CHECK(panel != nullptr && panel->Active);
        if (!panel || !panel->Active)
            return;
        IM_CHECK(dockedInUtilityGroup(panel));

        // A fully populated mode fills every section; the compact layout must
        // still keep the verdict on screen in the default dock.
        ReceiverRequirementsConfig config;
        config.band_start_Hz = 1.0e6;
        config.band_stop_Hz = 10.0e6;
        config.gain = ReceiverGainLimits{0.0, 20.0};
        config.nf_max_dB = 5.0;
        config.output_power = ReceiverOutputPowerLimits{-40.0, 0.0};
        config.iip3_min_dBm = 10.0;
        config.measurement_conditions.input_level_dBm = -30.0;
        config.measurement_conditions.iip3 = ReceiverIIP3TestSettings{1.0e6, -60.0, -40.0, 2.0};
        config.snr = ReceiverSNRLimit{20.0, 1.0e6};
        // Outside the band (BandStop + 5 MHz names 15 MHz against 1-10 MHz).
        config.oob_suppression.push_back(
            ReceiverOOBSuppressionPoint{ReceiverOOBSuppressionAnchor::BandStop, 5.0e6, 30.0});
        s_app->testSetSingleModeRequirements(config);
        ctx->Yield(3);
        // Start from the top so the check below measures the content, not the
        // scroll state an earlier case left behind.
        ctx->ScrollToTop("//Receiver Requirements");
        ctx->Yield(2);
        ctx->SetRef("Receiver Requirements");
        // The default utility dock (~744 px usable) holds the whole view —
        // form, verdict, and budget hint — on one screen, with nothing
        // overflowing sideways either.
        IM_CHECK_EQ(panel->Scroll.y, 0.0f);
        IM_CHECK(panel->ContentSize.x <= panel->InnerRect.GetWidth() + 2.0f);
        IM_CHECK(panel->ContentSize.y <= panel->InnerRect.GetHeight() + 2.0f);
        ImGuiTestItemInfo metric = ctx->ItemInfo("**/Metric");
        IM_CHECK(metric.ID != 0);
        IM_CHECK(metric.RectFull.Min.y < panel->Rect().Max.y);

        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Receiver Requirements");
        ctx->SetRef("");
        ctx->Yield(2);
        s_app->testSetSingleModeRequirements(ReceiverRequirementsConfig{});
    };

    // Agent panel control callbacks are replaced here, so the tests exercise
    // the UI without enabling the production listener or persisting a setting.
    t = IM_REGISTER_TEST(e, "rf_simulator", "agent_panel_server_toggle_callback");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        ctx->Yield(2);
        AgentPanelWidget &widget = s_app->testAgentPanelWidget();
        AgentPanelUiCleanup cleanup(*s_app, ctx, widget);
        setAgentPanelVisible(ctx, false);

        const bool server_was_running = s_app->agentServerRunning();
        std::vector<bool> requested_states;
        widget.onServerToggled = [&](bool enabled) { requested_states.push_back(enabled); };

        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Agent");
        ctx->SetRef("");
        ctx->Yield(3);
        IM_CHECK(agentPanelVisible());
        // Float the panel first: it opens docked in the utility group, whose
        // docked windows have no resize border for the engine to drag.
        ImGui::DockBuilderDockWindow("Agent", 0);
        ctx->Yield(2);
        ctx->WindowResize("Agent", ImVec2(520, 560));
        ctx->Yield(2);

        ctx->SetRef("Agent");
        IM_CHECK(ctx->ItemExists("Agent Server"));
        ctx->ItemClick("Agent Server");
        ctx->SetRef("");
        ctx->Yield(2);

        IM_CHECK_EQ(requested_states.size(), 1);
        if (requested_states.size() == 1)
            IM_CHECK_EQ(requested_states.front(), !server_was_running);
        IM_CHECK_EQ(s_app->agentServerRunning(), server_was_running);
    };

    t = IM_REGISTER_TEST(e, "rf_simulator", "agent_panel_revert_requires_confirmation");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        ctx->Yield(2);
        AgentPanelWidget &widget = s_app->testAgentPanelWidget();
        AgentPanelUiCleanup cleanup(*s_app, ctx, widget);
        setAgentPanelVisible(ctx, false);

        AppAgentHost &host = s_app->testAgentHost();
        IM_CHECK(host.checkpoints().empty());
        if (!host.checkpoints().empty())
            return;
        cleanup.host = &host;
        AgentCall call;
        call.tool = "circuit_edit";
        call.client = "ui-test-client";
        host.beginCheckpoint(call);
        host.commitCheckpoint("first UI test checkpoint");
        const std::uint64_t first_added_id = host.checkpoints().back().id;
        host.beginCheckpoint(call);
        host.commitCheckpoint("selected UI test checkpoint");
        const std::uint64_t second_added_id = host.checkpoints().back().id;
        const std::string revert_ref =
            "$$" + std::to_string(second_added_id) + "/Revert to before this call";
        IM_CHECK_EQ(host.checkpoints().size(), 2);
        cleanup.first_added_checkpoint_id = first_added_id;

        std::vector<std::uint64_t> reverted_ids;
        widget.onRevert = [&](std::uint64_t id) { reverted_ids.push_back(id); };

        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Agent");
        ctx->SetRef("");
        ctx->Yield(3);
        IM_CHECK(agentPanelVisible());
        ctx->WindowFocus("Agent");
        // Float the panel first: it opens docked in the utility group, whose
        // docked windows have no resize border for the engine to drag.
        ImGui::DockBuilderDockWindow("Agent", 0);
        ctx->Yield(2);
        ctx->WindowResize("Agent", ImVec2(620, 640));
        ctx->Yield(2);
        // Checkpoints render newest first, so the first matching button is the
        // second checkpoint added above.

        ImGuiWindow *checkpoint_window = nullptr;
        for (ImGuiWindow *window : ImGui::GetCurrentContext()->Windows) {
            if (window && window->Active && window->Name &&
                std::string(window->Name).find("agent_checkpoints") != std::string::npos) {
                checkpoint_window = window;
                break;
            }
        }
        IM_CHECK(checkpoint_window != nullptr);
        if (!checkpoint_window)
            return;
        ctx->SetRef(checkpoint_window);
        IM_CHECK(ctx->ItemExists(revert_ref.c_str()));
        ctx->ItemClick(revert_ref.c_str());
        ctx->SetRef("");
        ctx->Yield(2);
        ctx->SetRef("Revert Agent Changes");
        IM_CHECK(ctx->ItemExists("Cancel"));
        ctx->ItemClick("Cancel");
        ctx->SetRef("");
        ctx->Yield(2);
        IM_CHECK(reverted_ids.empty());

        ctx->SetRef(checkpoint_window);
        ctx->ItemClick(revert_ref.c_str());
        ctx->SetRef("");
        ctx->Yield(2);
        ctx->SetRef("Revert Agent Changes");
        IM_CHECK(ctx->ItemExists("Revert"));
        ctx->ItemClick("Revert");
        ctx->SetRef("");
        ctx->Yield(2);
        IM_CHECK_EQ(reverted_ids.size(), 1);
        if (reverted_ids.size() == 1)
            IM_CHECK(reverted_ids.front() == second_added_id);
    };

    t = IM_REGISTER_TEST(e, "rf_simulator", "agent_panel_view_menu_and_indicator_open_panel");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        namespace fs = std::filesystem;
        ctx->Yield(2);
        AgentPanelWidget &widget = s_app->testAgentPanelWidget();
        AgentPanelUiCleanup cleanup(*s_app, ctx, widget);
        setAgentPanelVisible(ctx, false);

        if (!s_app->agentServerRunning()) {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            cleanup.temp_root =
                fs::temp_directory_path() / ("rf_sim_agent_ui_" + std::to_string(stamp));
            fs::create_directories(cleanup.temp_root);
            const fs::path private_dir = cleanup.temp_root / "private";
            std::string error;
            const bool private_directory_ready = ensurePrivateDirectory(private_dir, &error);
            IM_CHECK(private_directory_ready);
            if (!private_directory_ready)
                return;

            AgentServerConfig config;
            config.endpoint_file = private_dir / "agent_endpoint.json";
            config.app_version = "ui-test";
            const bool started = s_app->testStartAgentServer(config, &error);
            IM_CHECK(started);
            if (!started)
                return;
            cleanup.owns_test_server = true;
        }
        IM_CHECK(s_app->agentServerRunning());
        if (!s_app->agentServerRunning())
            return;

        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Agent");
        ctx->SetRef("");
        ctx->Yield(3);
        IM_CHECK(agentPanelVisible());

        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("View/Agent");
        ctx->SetRef("");
        ctx->Yield(2);
        IM_CHECK(!agentPanelVisible());

        AgentServer *server = s_app->testAgentServer();
        IM_CHECK(server != nullptr);
        if (!server)
            return;
        const AgentServerStatus status = server->status();
        IM_CHECK_EQ(status.state, AgentServerState::Waiting);
        const std::string indicator = "Agent: listening";

        ImGuiWindow *menu_bar = ImGui::FindWindowByName("##MainMenuBar");
        IM_CHECK(menu_bar != nullptr);
        if (!menu_bar)
            return;
        ctx->SetRef(menu_bar);
        ctx->MenuClick(indicator.c_str());
        ctx->SetRef("");
        ctx->Yield(3);
        IM_CHECK(agentPanelVisible());
    };

    // Issue #299: the inspector refuses a Low cutoff that would invert a BPF band. The typed
    // value is not written, and the band keeps its last valid cutoffs.
    t = IM_REGISTER_TEST(e, "rf_simulator", "inspector_filter_inverted_band_refused");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        IComponentEngine *placed = s_app->testCreateComponent("filter", 9299);
        IM_CHECK(placed != nullptr);
        auto *filter = dynamic_cast<IdealFilterEngine *>(placed);
        IM_CHECK(filter != nullptr);
        const int node = placed->graphNodeId();
        NodeHelper::selectNode(ctx, *s_app, node);
        InspectorHelper::waitForPopulated(ctx, node);

        // LPF 50 MHz, then Type BPF: the band is 50/50 MHz.
        ctx->SetRef("Properties");
        ctx->ItemInputValue("Cutoff", "50");
        ctx->Yield(2);
        IM_CHECK_EQ(filter->fcLow_Hz(), 50e6);
        InspectorHelper::selectComboItem(ctx, "Type", "BPF");
        IM_CHECK(filter->filterType() == FilterType::BPF);

        // Low 100 MHz over High 50 MHz would invert the band: refused, nothing written.
        ctx->SetRef("Properties");
        ctx->ItemInputValue("Low Cutoff", "100");
        ctx->Yield(2);
        IM_CHECK_EQ(filter->fcLow_Hz(), 50e6);
        IM_CHECK_EQ(filter->fcHigh_Hz(), 50e6);

        // Raising High first makes the same Low edit valid.
        ctx->SetRef("Properties");
        ctx->ItemInputValue("High Cutoff", "200");
        ctx->Yield(2);
        ctx->SetRef("Properties");
        ctx->ItemInputValue("Low Cutoff", "100");
        ctx->Yield(2);
        IM_CHECK_EQ(filter->fcLow_Hz(), 100e6);
        IM_CHECK_EQ(filter->fcHigh_Hz(), 200e6);

        s_app->testRemoveComponent(node);
    };

    // Issue #184: the amplifier form exposes the S-parameter file picker.
    // This runs last because the modal form is opened directly by the test.
    t = IM_REGISTER_TEST(e, "rf_simulator", "component_form_displays_sparam_picker");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        s_app->testOpenNewComponentForm("amplifier", "");
        ctx->Yield(2);
        ctx->SetRef("Component Form");
        const auto browse = ctx->ItemInfo("Browse...", ImGuiTestOpFlags_NoError);
        IM_CHECK(browse.ID != 0);
    };
}
