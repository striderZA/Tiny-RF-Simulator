#include "app.h"
#include "imgui.h"
#include "imnodes.h"
#include "logging_core.h"
#include "logging_widget.h"
#include "pfb_channelizer_engine.h"
#include "coax_cable_engine.h"
#include <portable-file-dialogs.h>
#include <algorithm>
#include <fstream>
#include <functional>
#include <typeindex>
#include <unordered_map>

RfSimulatorApp::RfSimulatorApp()
    : m_components(m_graph_engine, m_view_manager) {
    m_graph_widget = std::make_unique<NodeGraphWidget>(m_graph_engine);
    m_graph_widget->onAddGenerator = [this]() {
        m_components.add<SignalGeneratorEngine>(m_next_component_id++, m_graph_engine);
        markDirty();
    };
    m_graph_widget->onAddAmplifier = [this]() {
        m_components.add<AmplifierEngine>(m_next_component_id++, m_graph_engine);
        markDirty();
    };
    m_graph_widget->onAddSplitter = [this]() {
        m_components.add<SplitterEngine>(m_next_component_id++, m_graph_engine);
        markDirty();
    };
    m_graph_widget->onAddMixer = [this]() {
        m_components.add<MixerEngine>(m_next_component_id++, m_graph_engine);
        markDirty();
    };
    m_graph_widget->onAddSParamComponent = [this]() {
        m_components.add<SParamEngine>(m_next_component_id++, m_graph_engine, "");
        markDirty();
    };
    m_graph_widget->onAddAdc = [this]() {
        m_components.add<AdcEngine>(m_next_component_id++, m_graph_engine);
        markDirty();
    };
    m_graph_widget->onAddPFB = [this]() {
        auto& pfb = m_components.add<PFBChannelizerEngine>(m_next_component_id++, m_graph_engine);
        m_iq_widgets.push_back(std::make_unique<IQPlotWidget>(pfb));
        m_show_iq_pfbs.push_back(
            m_state.loadBool("WindowState", ("IQPlot_" + std::to_string(pfb.id())).c_str(), true));
        m_pfb_grid_widgets.push_back(std::make_unique<PFBChannelizerWidget>(pfb));
        m_show_pfb_grids.push_back(
            m_state.loadBool("WindowState", ("PFBGrid_" + std::to_string(pfb.id())).c_str(), true));
        markDirty();
    };
    m_graph_widget->onAddCoaxCable = [this]() {
        m_components.add<CoaxCableEngine>(m_next_component_id++, m_graph_engine);
        markDirty();
    };
    m_graph_widget->onAddIdealFilter = [this]() {
        m_components.add<IdealFilterEngine>(m_next_component_id++, m_graph_engine);
        markDirty();
    };    
    m_graph_widget->onLinkChanged = [this]() { markDirty(); };
    m_graph_widget->onRemoveNode = [this](int id) {
        markDirty();
        m_components.remove(id);
        auto pfb_vec = m_components.byType<PFBChannelizerEngine>();
        m_iq_widgets.clear();
        m_show_iq_pfbs.clear();
        m_pfb_grid_widgets.clear();
        m_show_pfb_grids.clear();
        for (auto* pfb : pfb_vec) {
            m_iq_widgets.push_back(std::make_unique<IQPlotWidget>(*pfb));
            m_show_iq_pfbs.push_back(
                m_state.loadBool("WindowState", ("IQPlot_" + std::to_string(pfb->id())).c_str(), true));
            m_pfb_grid_widgets.push_back(std::make_unique<PFBChannelizerWidget>(*pfb));
            m_show_pfb_grids.push_back(
                m_state.loadBool("WindowState", ("PFBGrid_" + std::to_string(pfb->id())).c_str(), true));
        }
    };
    m_graph_widget->onNodeHover = [this](int id) {
        return m_components.hoverSummary(id);
    };

    m_components.add<SignalGeneratorEngine>(m_next_component_id++, m_graph_engine).addTone(100e6, -20.0);
    m_components.add<AmplifierEngine>(m_next_component_id++, m_graph_engine);

    m_inspector_panel = std::make_unique<InspectorPanel>(m_graph_engine, m_components);
    m_inspector_panel->onRemoveNode = [this](int graph_node_id) {
        if (m_graph_widget->onRemoveNode)
            m_graph_widget->onRemoveNode(graph_node_id);
    };

    m_inspector_panel->setViewToggles({
        &m_show_log,
        &m_show_spectrum,
        &m_show_properties,
        nullptr, // iq_plot (per-PFB toggles used instead)
        &m_show_node_editor
    });
    m_inspector_panel->onParamChange = [this]() { markDirty(); };

    m_spectrum_widget = std::make_unique<SpectrumAnalyzerWidget>(m_spectrum_engine, m_view_manager);

    // Ensure all engine nodes are registered with the widget's imnodes context
    // so saveProject() can read node positions (GetNodeEditorSpacePos) without
    // crashing even before the first render frame.
    m_graph_widget->syncNodesFromEngine();

    load_window_states();
}

void RfSimulatorApp::load_window_states() {
    m_show_log = m_state.loadBool("WindowState", "Log", true);
    m_show_spectrum = m_state.loadBool("WindowState", "SpectrumAnalyzer", true);
    m_show_properties = m_state.loadBool("WindowState", "Properties", true);
    m_show_node_editor = m_state.loadBool("WindowState", "NodeEditor", true);
}

void RfSimulatorApp::newProject() {
    // Remove all links from the graph engine first
    m_graph_engine.removeAllLinks();

    // Remove all components — ComponentRegistry handles cleanup
    // Collect IDs first to avoid iterator invalidation
    std::vector<int> ids;
    for (auto* comp : m_components.all())
        ids.push_back(comp->graphNodeId());
    for (int id : ids)
        m_components.remove(id);

    // Clear probes
    m_graph_engine.clearProbes();
    m_spectrum_widget->setProbeLabels({});

    // Reset IQ / PFB widgets
    m_iq_widgets.clear();
    m_show_iq_pfbs.clear();
    m_pfb_grid_widgets.clear();
    m_show_pfb_grids.clear();

    // Reset graph counters
    m_graph_engine.setNextIds(1, 100, 1000);
    m_graph_engine.setNextGroupId(50000);
    m_graph_engine.setNextBoundaryPinId(100000);

    m_next_component_id = 100;
    m_current_project_path.clear();
    m_dirty = false;
}

void RfSimulatorApp::saveProject(const std::string& path) {
    nlohmann::json root;
    root["version"] = 1;

    auto pos = path.find_last_of("\\/");
    std::string fname = (pos != std::string::npos) ? path.substr(pos + 1) : path;
    auto dot = fname.find_last_of('.');
    root["name"] = (dot != std::string::npos) ? fname.substr(0, dot) : fname;

    // Type to name mapping for readable type names
    static const std::unordered_map<std::type_index, std::string> s_type_names = {
        {std::type_index(typeid(SignalGeneratorEngine)), "SignalGenerator"},
        {std::type_index(typeid(AmplifierEngine)), "Amplifier"},
        {std::type_index(typeid(SplitterEngine)), "Splitter"},
        {std::type_index(typeid(MixerEngine)), "Mixer"},
        {std::type_index(typeid(SParamEngine)), "SParam"},
        {std::type_index(typeid(AdcEngine)), "ADC"},
        {std::type_index(typeid(PFBChannelizerEngine)), "PFBChannelizer"},
        {std::type_index(typeid(CoaxCableEngine)), "CoaxCable"},
        {std::type_index(typeid(IdealFilterEngine)), "IdealFilter"},
    };

    // Save components by iterating the registry
    nlohmann::json comps_arr = nlohmann::json::array();
    for (auto* comp : m_components.all()) {
        nlohmann::json cj;
        auto it = s_type_names.find(std::type_index(typeid(*comp)));
        cj["type"] = (it != s_type_names.end()) ? it->second : "Unknown";
        cj["params"] = comp->serialize();

        // Save node position via imnodes
        int nid = comp->graphNodeId();
        ImNodes::EditorContextSet(m_graph_widget->context());
        ImVec2 pos_n = ImNodes::GetNodeEditorSpacePos(nid);
        cj["pos"]["x"] = pos_n.x;
        cj["pos"]["y"] = pos_n.y;

        comps_arr.push_back(cj);
    }
    root["components"] = comps_arr;

    // Save links as component-index + port pairs (not raw pin IDs)
    nlohmann::json links_arr = nlohmann::json::array();
    // Build a map: pin_id \u2192 {comp_index, port, is_output}
    struct PinInfo { size_t comp; int port; bool is_output; };
    std::unordered_map<int, PinInfo> pin_map;
    for (size_t i = 0; i < m_components.size(); ++i) {
        auto* comp = m_components.all()[i];
        int nid = comp->graphNodeId();
        for (const auto& gn : m_graph_engine.nodes()) {
            if (gn.node_id == nid) {
                for (size_t p = 0; p < gn.input_pin_ids.size(); ++p)
                    pin_map[gn.input_pin_ids[p]] = {i, (int)p, false};
                for (size_t p = 0; p < gn.output_pin_ids.size(); ++p)
                    pin_map[gn.output_pin_ids[p]] = {i, (int)p, true};
                break;
            }
        }
    }
    for (const auto& link : m_graph_engine.links()) {
        auto from_it = pin_map.find(link.start_pin_id);
        auto to_it = pin_map.find(link.end_pin_id);
        if (from_it == pin_map.end() || to_it == pin_map.end()) continue;
        nlohmann::json lj;
        lj["from"] = from_it->second.comp;
        lj["from_port"] = from_it->second.port;
        lj["to"] = to_it->second.comp;
        lj["to_port"] = to_it->second.port;
        links_arr.push_back(lj);
    }
    root["links"] = links_arr;

    // Save probes as component-index + port
    nlohmann::json probes_arr = nlohmann::json::array();
    for (int probe_pin : m_graph_engine.probePins()) {
        auto it = pin_map.find(probe_pin);
        if (it != pin_map.end()) {
            nlohmann::json pj;
            pj["comp"] = it->second.comp;
            pj["port"] = it->second.port;
            pj["is_output"] = it->second.is_output;
            probes_arr.push_back(pj);
        }
    }
    root["probe_pins"] = probes_arr;

    // Save groups
    nlohmann::json groups_arr = nlohmann::json::array();
    // Build node_id \u2192 comp_index map
    std::unordered_map<int, size_t> nid_to_comp;
    for (size_t i = 0; i < m_components.size(); ++i)
        nid_to_comp[m_components.all()[i]->graphNodeId()] = i;

    for (const auto& g : m_graph_engine.groups()) {
        nlohmann::json gj;
        gj["name"] = g.name;
        gj["collapsed"] = g.collapsed;
        gj["member_components"] = nlohmann::json::array();
        for (int member_nid : g.member_node_ids) {
            auto it = nid_to_comp.find(member_nid);
            if (it != nid_to_comp.end())
                gj["member_components"].push_back(it->second);
        }
        groups_arr.push_back(gj);
    }
    root["groups"] = groups_arr;

    // Window state
    root["window_state"]["log"] = m_show_log;
    root["window_state"]["spectrum_analyzer"] = m_show_spectrum;
    root["window_state"]["properties"] = m_show_properties;
    root["window_state"]["node_editor"] = m_show_node_editor;

    // Graph state counters (for later additions)
    root["graph_state"]["next_component_id"] = m_next_component_id;

    std::ofstream out(path);
    if (!out) {
        LOG_ERROR("Failed to open project file for writing: %s", path.c_str());
        return;
    }
    out << root.dump(2);
    out.close();

    m_current_project_path = path;
    m_dirty = false;
    LOG_INFO("Saved project to %s", path.c_str());
}

void RfSimulatorApp::loadProject(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        LOG_ERROR("Failed to open project file: %s", path.c_str());
        return;
    }
    nlohmann::json root;
    try {
        in >> root;
    } catch (const nlohmann::json::exception& e) {
        LOG_ERROR("Invalid project file: %s", e.what());
        return;
    }

    newProject();

    // Map: type string \u2192 factory lambda
    std::vector<nlohmann::json::iterator> comp_order;
    auto& comps = root["components"];
    for (auto it = comps.begin(); it != comps.end(); ++it)
        comp_order.push_back(it);

    // Create components in saved order
    std::vector<int> new_node_ids; // maps saved index \u2192 new graph node ID
    for (auto& it : comp_order) {
        auto& cj = *it;
        std::string type = cj.value("type", "");
        auto& params = cj["params"];

        IComponentEngine* comp = nullptr;
        if (type == "SignalGenerator") {
            auto& ref = m_components.add<SignalGeneratorEngine>(m_next_component_id++, m_graph_engine);
            ref.deserialize(params);
            comp = &ref;
        } else if (type == "Amplifier") {
            auto& ref = m_components.add<AmplifierEngine>(m_next_component_id++, m_graph_engine);
            ref.deserialize(params);
            comp = &ref;
        } else if (type == "Mixer") {
            auto& ref = m_components.add<MixerEngine>(m_next_component_id++, m_graph_engine);
            ref.deserialize(params);
            comp = &ref;
        } else if (type == "Splitter") {
            auto& ref = m_components.add<SplitterEngine>(m_next_component_id++, m_graph_engine);
            ref.deserialize(params);
            comp = &ref;
        } else if (type == "SParam") {
            auto& ref = m_components.add<SParamEngine>(m_next_component_id++, m_graph_engine, "");
            ref.deserialize(params);
            comp = &ref;
        } else if (type == "ADC") {
            auto& ref = m_components.add<AdcEngine>(m_next_component_id++, m_graph_engine);
            ref.deserialize(params);
            comp = &ref;
        } else if (type == "PFBChannelizer") {
            auto& ref = m_components.add<PFBChannelizerEngine>(m_next_component_id++, m_graph_engine);
            ref.deserialize(params);
            // Restore IQ plot + PFB grid widgets for this PFB
            m_iq_widgets.push_back(std::make_unique<IQPlotWidget>(ref));
            m_show_iq_pfbs.push_back(true);
            m_pfb_grid_widgets.push_back(std::make_unique<PFBChannelizerWidget>(ref));
            m_show_pfb_grids.push_back(true);
            comp = &ref;
        } else if (type == "CoaxCable") {
            auto& ref = m_components.add<CoaxCableEngine>(m_next_component_id++, m_graph_engine);
            ref.deserialize(params);
            comp = &ref;
        } else if (type == "IdealFilter") {
            auto& ref = m_components.add<IdealFilterEngine>(m_next_component_id++, m_graph_engine);
            ref.deserialize(params);
            comp = &ref;
        } else {
            LOG_WARN("Unknown component type in project file: %s", type.c_str());
            new_node_ids.push_back(-1);
            continue;
        }

        new_node_ids.push_back(comp ? comp->graphNodeId() : -1);

        // Restore position
        if (comp && cj.contains("pos")) {
            ImNodes::EditorContextSet(m_graph_widget->context());
            ImNodes::SetNodeEditorSpacePos(
                comp->graphNodeId(),
                ImVec2(cj["pos"].value("x", 0.0f), cj["pos"].value("y", 0.0f)));
        }
    }

    // Restore links (saved as component-index + port pairs)
    auto& saved_links = root["links"];
    for (const auto& lj : saved_links) {
        int from_idx = lj.value("from", -1);
        int to_idx = lj.value("to", -1);
        int from_port = lj.value("from_port", 0);
        int to_port = lj.value("to_port", 0);
        if (from_idx < 0 || to_idx < 0 ||
            static_cast<size_t>(from_idx) >= m_components.size() ||
            static_cast<size_t>(to_idx) >= m_components.size())
            continue;

        auto* from_comp = m_components.all()[from_idx];
        auto* to_comp = m_components.all()[to_idx];

        int start_pin = from_comp->outputPinId(from_port);
        int end_pin = to_comp->inputPinId(to_port);
        if (start_pin >= 0 && end_pin >= 0)
            m_graph_engine.addLink(start_pin, end_pin);
    }

    // Restore probes
    auto& saved_probes = root["probe_pins"];
    for (const auto& pj : saved_probes) {
        int comp_idx = pj.value("comp", -1);
        int port = pj.value("port", 0);
        bool is_output = pj.value("is_output", true);
        if (comp_idx < 0 || static_cast<size_t>(comp_idx) >= m_components.size())
            continue;
        auto* comp = m_components.all()[comp_idx];
        int pin = is_output ? comp->outputPinId(port) : comp->inputPinId(port);
        if (pin >= 0) m_graph_engine.addProbePin(pin);
    }

    // Restore groups
    auto& saved_groups = root["groups"];
    for (const auto& gj : saved_groups) {
        std::string name = gj.value("name", "Group");
        std::vector<int> member_ids;
        for (const auto& mj : gj["member_components"]) {
            int comp_idx = mj.get<int>();
            if (comp_idx >= 0 && static_cast<size_t>(comp_idx) < new_node_ids.size() &&
                new_node_ids[comp_idx] >= 0) {
                member_ids.push_back(new_node_ids[comp_idx]);
            }
        }
        if (member_ids.size() >= 2) {
            int gid = m_graph_engine.addGroup(name, member_ids);
            bool collapsed = gj.value("collapsed", true);
            if (gid >= 0)
                m_graph_engine.setGroupCollapsed(gid, collapsed);
        }
    }

    // Restore window state
    auto& ws = root["window_state"];
    if (!ws.is_null()) {
        m_show_log = ws.value("log", true);
        m_show_spectrum = ws.value("spectrum_analyzer", true);
        m_show_properties = ws.value("properties", true);
        m_show_node_editor = ws.value("node_editor", true);
    }

    // Restore graph state counters
    auto& gs = root["graph_state"];
    if (!gs.is_null()) {
        m_next_component_id = gs.value("next_component_id", m_next_component_id);
    }

    m_current_project_path = path;
    m_dirty = false;
    LOG_INFO("Loaded project from %s", path.c_str());
}

void RfSimulatorApp::openFileDialog() {
    auto result = pfd::open_file("Open Project", ".",
        {"RF Simulator Project (*.rfsim)", "*.rfsim", "All Files", "*"}).result();
    if (!result.empty())
        loadProject(result[0]);
}

void RfSimulatorApp::saveFileDialog() {
    auto result = pfd::save_file("Save Project As", ".",
        {"RF Simulator Project (*.rfsim)", "*.rfsim"}).result();
    if (!result.empty())
        saveProject(result);
}

void RfSimulatorApp::update_dsp() {
    std::unordered_map<int, std::function<void()>> updates;

    for (auto* comp : m_components.all()) {
        int N = comp->numInputPins();
        for (int k = 0; k < N; ++k) {
            int pid = comp->inputPinId(k);
            if (pid >= 0) {
                auto* source = m_graph_engine.getSourceForInput(pid);
                comp->node().inputs[k] = source ? &source->outputs[0] : nullptr;
            } else if (static_cast<size_t>(k) < comp->node().inputs.size()) {
                comp->node().inputs[k] = nullptr;
            }
        }
        updates[comp->graphNodeId()] = [comp]() { comp->update(0.0); };
    }

    auto order = m_graph_engine.topologicalOrder();
    for (int node_id : order) {
        auto it = updates.find(node_id);
        if (it != updates.end())
            it->second();
    }

    // Update spectrum view based on first active probe
    auto probed_nodes = m_graph_engine.probedSignalNodes();
    std::vector<std::string> probe_labels;
    for (auto* pn : probed_nodes) {
        std::string label;
        for (const auto& node : m_graph_engine.nodes()) {
            if (node.signal_node == pn) {
                label = node.label + " OUT";
                break;
            }
        }
        probe_labels.push_back(label);
    }
    m_spectrum_widget->setProbeLabels(probe_labels);

    for (auto* node : m_view_manager.nodes()) {
        if (node) {
            node->view_enabled = std::find(probed_nodes.begin(), probed_nodes.end(), node) != probed_nodes.end();
        }
    }

    // Sync PFB pointers to spectrum analyzer and inspector panel
    auto pfb_ptrs = m_components.byType<PFBChannelizerEngine>();
    std::vector<PFBChannelizerEngine*> pfb_vec(pfb_ptrs.begin(), pfb_ptrs.end());
    m_spectrum_widget->setPFBs(pfb_vec);
    m_inspector_panel->setPFBs(pfb_vec);
}

void RfSimulatorApp::draw_ui() {
    ImGuiIO &io = ImGui::GetIO();
    (void)io;

    // Execute pending action deferred from Unsaved Changes popup
    // Only fires if the user saved or discarded (m_dirty == false).
    // If save dialog was cancelled, m_dirty is still true and the action is dropped.
    if (m_pending_action != PendingAction::None) {
        auto action = m_pending_action;
        m_pending_action = PendingAction::None;
        if (m_dirty) {
            // User cancelled the save dialog; drop the pending action entirely
            LOG_INFO("Unsaved changes still present - pending action dropped");
        } else {
            switch (action) {
                case PendingAction::New:  newProject(); break;
                case PendingAction::Open: openFileDialog(); break;
                case PendingAction::Exit: std::exit(0); break;
                default: break;
            }
        }
    }

    // File menu bar
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New", "Ctrl+N")) {
                if (m_dirty) { m_pending_action = PendingAction::New; ImGui::OpenPopup("Unsaved Changes"); }
                else newProject();
            }
            if (ImGui::MenuItem("Open...", "Ctrl+O")) {
                if (m_dirty) { m_pending_action = PendingAction::Open; ImGui::OpenPopup("Unsaved Changes"); }
                else openFileDialog();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save", "Ctrl+S")) {
                if (!m_current_project_path.empty()) saveProject(m_current_project_path);
                else saveFileDialog();
            }
            if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S"))
                saveFileDialog();
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) {
                if (m_dirty) { m_pending_action = PendingAction::Exit; ImGui::OpenPopup("Unsaved Changes"); }
                else std::exit(0);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Log", nullptr, &m_show_log);
            ImGui::MenuItem("Spectrum Analyzer", nullptr, &m_show_spectrum);
            ImGui::MenuItem("Properties", nullptr, &m_show_properties);
            ImGui::MenuItem("Node Editor", nullptr, &m_show_node_editor);
            ImGui::EndMenu();
        }
        // Title / project name on the right
        {
            float tw = ImGui::GetContentRegionAvail().x;
            ImGui::SameLine(tw - 300.0f);
            if (!m_current_project_path.empty()) {
                auto p = m_current_project_path.find_last_of("\\/");
                std::string fname = (p != std::string::npos) ? m_current_project_path.substr(p + 1) : m_current_project_path;
                ImGui::Text("%s%s", m_dirty ? "* " : "", fname.c_str());
            } else if (m_dirty) {
                ImGui::Text("*Untitled");
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%.1f FPS", io.Framerate);
        }
        ImGui::EndMainMenuBar();
    }

    // Keyboard shortcuts (skip while editing text fields)
    if (!io.WantTextInput) {
        if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S)) {
            if (!m_current_project_path.empty()) saveProject(m_current_project_path);
            else saveFileDialog();
        }
        if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S))
            saveFileDialog();
        if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_O))
            openFileDialog();
        if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_N))
            newProject();
    }

    // Unsaved Changes popup
    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("You have unsaved changes. Save before continuing?");
        if (ImGui::Button("Save", ImVec2(120, 0))) {
            if (!m_current_project_path.empty()) saveProject(m_current_project_path);
            else {
                auto path = pfd::save_file("Save Project As", ".",
                    {"RF Simulator Project (*.rfsim)", "*.rfsim"}).result();
                if (!path.empty()) saveProject(path);
            }
            ImGui::CloseCurrentPopup();
            // m_pending_action stays set, executes next frame if save succeeded (m_dirty == false)
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard", ImVec2(120, 0))) {
            m_dirty = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            m_pending_action = PendingAction::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (m_show_node_editor)
        m_graph_widget->draw("Node Editor", &m_show_node_editor);

    if (m_show_spectrum)
        m_spectrum_widget->draw("Spectrum Analyzer", &m_show_spectrum);

    for (size_t i = 0; i < m_iq_widgets.size(); ++i) {
        if (m_show_iq_pfbs[i]) {
            std::string label = "IQ Plot - PFB " + std::to_string(i);
            bool show = m_show_iq_pfbs[i];
            m_iq_widgets[i]->draw(label.c_str(), &show);
            m_show_iq_pfbs[i] = show;
        }
    }

    for (size_t i = 0; i < m_pfb_grid_widgets.size(); ++i) {
        if (m_show_pfb_grids[i]) {
            std::string label = "Channelizer Grid - PFB " + std::to_string(i);
            bool show = m_show_pfb_grids[i];
            m_pfb_grid_widgets[i]->draw(label.c_str(), &show);
            m_show_pfb_grids[i] = show;
        }
    }

    for (size_t i = 0; i < m_generator_widgets.size(); ++i) {
        m_generator_widgets[i]->draw("Generators");
    }

    if (m_show_properties && m_inspector_panel)
        m_inspector_panel->draw("Properties", &m_show_properties);

    if (m_show_log)
        m_log_widget.draw("Log", &m_show_log);
}

RfSimulatorApp::~RfSimulatorApp() {
    m_state.saveBool("WindowState", "Log", m_show_log);
    m_state.saveBool("WindowState", "SpectrumAnalyzer", m_show_spectrum);
    m_state.saveBool("WindowState", "Properties", m_show_properties);
    auto pfb_vec = m_components.byType<PFBChannelizerEngine>();
    for (size_t i = 0; i < m_show_iq_pfbs.size() && i < pfb_vec.size(); ++i) {
        std::string key = "IQPlot_" + std::to_string(pfb_vec[i]->id());
        m_state.saveBool("WindowState", key.c_str(), m_show_iq_pfbs[i]);
    }
    auto pfb_vec_save = m_components.byType<PFBChannelizerEngine>();
    for (size_t i = 0; i < m_show_pfb_grids.size() && i < pfb_vec_save.size(); ++i) {
        std::string key = "PFBGrid_" + std::to_string(pfb_vec_save[i]->id());
        m_state.saveBool("WindowState", key.c_str(), m_show_pfb_grids[i]);
    }
    m_state.saveBool("WindowState", "NodeEditor", m_show_node_editor);
}
