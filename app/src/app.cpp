#include "app.h"
#include "attenuator_engine.h"
#include "coax_cable_engine.h"
#include "combiner_engine.h"
#include "imgui.h"
#include "imnodes.h"
#include "logging_core.h"
#include "logging_widget.h"
#include "pfb_channelizer_engine.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <portable-file-dialogs.h>
#include <typeindex>
#include <unordered_map>
// Keep only filesystem-safe characters for path segments: [A-Za-z0-9-_ ].
// Strips everything else (incl. /, \\, and . which eliminates .. risks).
// Trims leading/trailing spaces. Returns fallback if result is empty.
static std::string sanitizePathSegment(const std::string &s, const std::string &fallback) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == ' ')
            out.push_back(c);
    }
    size_t start = out.find_first_not_of(' ');
    if (start == std::string::npos)
        return fallback;
    size_t end = out.find_last_not_of(' ');
    out = out.substr(start, end - start + 1);
    return out.empty() ? fallback : out;
}

RfSimulatorApp::RfSimulatorApp() : m_components(m_graph_engine, m_view_manager) {
    m_graph_widget = std::make_unique<NodeGraphWidget>(m_graph_engine);
    m_graph_widget->onAddGenerator = [this](ImVec2 pos) {
        auto &comp = m_components.add<SignalGeneratorEngine>(m_next_component_id++, m_graph_engine);
        ImNodes::EditorContextSet(m_graph_widget->context());
        ImNodes::SetNodeEditorSpacePos(comp.graphNodeId(), pos);
        markDirty();
    };
    m_graph_widget->onAddAmplifier = [this](ImVec2 pos) {
        auto &comp = m_components.add<AmplifierEngine>(m_next_component_id++, m_graph_engine);
        ImNodes::EditorContextSet(m_graph_widget->context());
        ImNodes::SetNodeEditorSpacePos(comp.graphNodeId(), pos);
        markDirty();
    };
    m_graph_widget->onAddSplitter = [this](ImVec2 pos) {
        auto &comp = m_components.add<SplitterEngine>(m_next_component_id++, m_graph_engine);
        ImNodes::EditorContextSet(m_graph_widget->context());
        ImNodes::SetNodeEditorSpacePos(comp.graphNodeId(), pos);
        markDirty();
    };
    m_graph_widget->onAddMixer = [this](ImVec2 pos) {
        auto &comp = m_components.add<MixerEngine>(m_next_component_id++, m_graph_engine);
        ImNodes::EditorContextSet(m_graph_widget->context());
        ImNodes::SetNodeEditorSpacePos(comp.graphNodeId(), pos);
        markDirty();
    };

    m_graph_widget->onAddAdc = [this](ImVec2 pos) {
        auto &comp = m_components.add<AdcEngine>(m_next_component_id++, m_graph_engine);
        ImNodes::EditorContextSet(m_graph_widget->context());
        ImNodes::SetNodeEditorSpacePos(comp.graphNodeId(), pos);
        markDirty();
    };
    m_graph_widget->onAddPFB = [this](ImVec2 pos) {
        auto &pfb = m_components.add<PFBChannelizerEngine>(m_next_component_id++, m_graph_engine);
        ImNodes::EditorContextSet(m_graph_widget->context());
        ImNodes::SetNodeEditorSpacePos(pfb.graphNodeId(), pos);
        m_iq_widgets.push_back(std::make_unique<IQPlotWidget>(pfb));
        m_show_iq_pfbs.push_back(
            m_state.loadBool("WindowState", ("IQPlot_" + std::to_string(pfb.id())).c_str(), true));
        m_pfb_grid_widgets.push_back(std::make_unique<PFBChannelizerWidget>(pfb));
        m_show_pfb_grids.push_back(
            m_state.loadBool("WindowState", ("PFBGrid_" + std::to_string(pfb.id())).c_str(), true));
        markDirty();
    };
    m_graph_widget->onAddCoaxCable = [this](ImVec2 pos) {
        auto &comp = m_components.add<CoaxCableEngine>(m_next_component_id++, m_graph_engine);
        ImNodes::EditorContextSet(m_graph_widget->context());
        ImNodes::SetNodeEditorSpacePos(comp.graphNodeId(), pos);
        markDirty();
    };
    m_graph_widget->onAddEqualizer = [this](ImVec2 pos) {
        auto &comp = m_components.add<EqualizerEngine>(m_next_component_id++, m_graph_engine);
        ImNodes::EditorContextSet(m_graph_widget->context());
        ImNodes::SetNodeEditorSpacePos(comp.graphNodeId(), pos);
    };
    m_graph_widget->onAddIdealFilter = [this](ImVec2 pos) {
        auto &comp = m_components.add<IdealFilterEngine>(m_next_component_id++, m_graph_engine);
        ImNodes::EditorContextSet(m_graph_widget->context());
        ImNodes::SetNodeEditorSpacePos(comp.graphNodeId(), pos);
        markDirty();
    };
    m_graph_widget->onAddAttenuator = [this](ImVec2 pos) {
        auto &comp = m_components.add<AttenuatorEngine>(m_next_component_id++, m_graph_engine);
        ImNodes::EditorContextSet(m_graph_widget->context());
        ImNodes::SetNodeEditorSpacePos(comp.graphNodeId(), pos);
        markDirty();
    };
    m_graph_widget->onAddCombiner = [this](ImVec2 pos) {
        auto &comp = m_components.add<CombinerEngine>(m_next_component_id++, m_graph_engine);
        ImNodes::EditorContextSet(m_graph_widget->context());
        ImNodes::SetNodeEditorSpacePos(comp.graphNodeId(), pos);
        markDirty();
    };
    m_graph_widget->onNodeMoved = [this]() { markDirty(); };
    m_graph_widget->onLinkChanged = [this]() { markDirty(); };
    m_graph_widget->onRemoveNode = [this](int id) {
        markDirty();
        m_components.remove(id);
        auto pfb_vec = m_components.byType<PFBChannelizerEngine>();
        m_iq_widgets.clear();
        m_show_iq_pfbs.clear();
        m_pfb_grid_widgets.clear();
        m_show_pfb_grids.clear();
        for (auto *pfb : pfb_vec) {
            m_iq_widgets.push_back(std::make_unique<IQPlotWidget>(*pfb));
            m_show_iq_pfbs.push_back(m_state.loadBool(
                "WindowState", ("IQPlot_" + std::to_string(pfb->id())).c_str(), true));
            m_pfb_grid_widgets.push_back(std::make_unique<PFBChannelizerWidget>(*pfb));
            m_show_pfb_grids.push_back(m_state.loadBool(
                "WindowState", ("PFBGrid_" + std::to_string(pfb->id())).c_str(), true));
        }
    };
    m_graph_widget->onNodeHover = [this](int id) { return m_components.hoverSummary(id); };
    m_graph_widget->onDuplicateNode = [this](int id) { duplicateComponent(id); };

    m_components.add<SignalGeneratorEngine>(m_next_component_id++, m_graph_engine)
        .addTone(100e6, -20.0);
    m_components.add<AmplifierEngine>(m_next_component_id++, m_graph_engine);

    m_inspector_panel = std::make_unique<InspectorPanel>(m_graph_engine, m_components);
    m_inspector_panel->onRemoveNode = [this](int graph_node_id) {
        if (m_graph_widget->onRemoveNode)
            m_graph_widget->onRemoveNode(graph_node_id);
    };

    m_inspector_panel->setViewToggles({&m_show_log, &m_show_spectrum, &m_show_properties,
                                       nullptr, // iq_plot (per-PFB toggles used instead)
                                       &m_show_node_editor});
    m_inspector_panel->onParamChange = [this]() { markDirty(); };

    refreshExtensions();

    m_library_browser = std::make_unique<LibraryBrowserWidget>(m_library);
    m_library_browser->onInsert = [this](const ComponentDefinition &def) {
        auto *engine =
            m_library.instantiate(def, m_next_component_id++, m_components, m_graph_engine);
        if (engine)
            markDirty();
    };

    m_library_browser->onNewComponent = [this]() { openNewComponentForm("amplifier"); };
    m_library_browser->onEditComponent = [this](const ComponentDefinition &def) {
        openEditComponentForm(def);
    };

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
    m_show_help = m_state.loadBool("WindowState", "Help", false);
}

void RfSimulatorApp::duplicateComponent(int graph_node_id) {
    IComponentEngine *src = m_components.find(graph_node_id);
    if (!src)
        return;

    // Capture source position before creating the new node
    ImNodes::EditorContextSet(m_graph_widget->context());
    ImVec2 src_pos = ImNodes::GetNodeEditorSpacePos(graph_node_id);
    constexpr float OFFSET = 40.0f;

    // Capture source part number for copying to the duplicate
    std::string src_part_number;
    for (const auto &gn : m_graph_engine.nodes()) {
        if (gn.node_id == graph_node_id) {
            src_part_number = gn.part_number;
            break;
        }
    }

    // Helper: create a new engine of type T, copy params via serialize/deserialize,
    // position it offset from the source, and return a reference to the new engine.
    auto dup = [&](auto *typed_src) -> decltype(typed_src) {
        using T = std::remove_pointer_t<decltype(typed_src)>;
        auto &new_eng = m_components.add<T>(m_next_component_id++, m_graph_engine);
        new_eng.deserialize(typed_src->serialize());
        int new_nid = new_eng.graphNodeId();
        // Register with imnodes pool and set position
        ImNodes::SetNodeEditorSpacePos(new_nid, ImVec2(src_pos.x + OFFSET, src_pos.y + OFFSET));
        // Copy library part number
        if (!src_part_number.empty())
            m_graph_engine.setNodePartNumber(new_nid, src_part_number);
        return &new_eng;
    };

    if (auto *e = dynamic_cast<SignalGeneratorEngine *>(src)) {
        dup(e);
    } else if (auto *e = dynamic_cast<AmplifierEngine *>(src)) {
        dup(e);
    } else if (auto *e = dynamic_cast<SplitterEngine *>(src)) {
        dup(e);
    } else if (auto *e = dynamic_cast<MixerEngine *>(src)) {
        dup(e);
    } else if (auto *e = dynamic_cast<AdcEngine *>(src)) {
        dup(e);
    } else if (auto *e = dynamic_cast<PFBChannelizerEngine *>(src)) {
        auto *new_pfb = dup(e);
        // PFB also needs IQ plot widget and grid widget (same as onAddPFB)
        m_iq_widgets.push_back(std::make_unique<IQPlotWidget>(*new_pfb));
        m_show_iq_pfbs.push_back(m_state.loadBool(
            "WindowState", ("IQPlot_" + std::to_string(new_pfb->id())).c_str(), true));
        m_pfb_grid_widgets.push_back(std::make_unique<PFBChannelizerWidget>(*new_pfb));
        m_show_pfb_grids.push_back(m_state.loadBool(
            "WindowState", ("PFBGrid_" + std::to_string(new_pfb->id())).c_str(), true));
    } else if (auto *e = dynamic_cast<CoaxCableEngine *>(src)) {
        dup(e);
    } else if (auto *e = dynamic_cast<EqualizerEngine *>(src)) {
        dup(e);
    } else if (auto *e = dynamic_cast<IdealFilterEngine *>(src)) {
        dup(e);
    } else if (auto *e = dynamic_cast<AttenuatorEngine *>(src)) {
        dup(e);
    } else if (auto *e = dynamic_cast<CombinerEngine *>(src)) {
        dup(e);
    }

    markDirty();
}

void RfSimulatorApp::newProject() {
    // Remove all links from the graph engine first
    m_graph_engine.removeAllLinks();

    // Remove all components — ComponentRegistry handles cleanup
    // Collect IDs first to avoid iterator invalidation
    std::vector<int> ids;
    for (auto *comp : m_components.all())
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
    m_graph_widget->clearPositionCache();
    refreshExtensions();

    m_dirty = false;
}

void RfSimulatorApp::testMakeDirty() { markDirty(); }

void RfSimulatorApp::markDirty() { m_dirty = true; }
void RfSimulatorApp::refreshExtensions() {
    namespace fs = std::filesystem;

    fs::path project_root = m_current_project_path.empty()
                                ? fs::current_path()
                                : fs::path(m_current_project_path).parent_path();
    if (project_root.empty())
        project_root = fs::current_path();

    m_extension_manager.rescan(project_root);
    m_library = ComponentLibrary{};

#ifdef _WIN32
    const char *home = std::getenv("USERPROFILE");
#else
    const char *home = std::getenv("HOME");
#endif
    if (home) {
        m_library.scan((fs::path(home) / ".rf-sim" / "libraries").string());
    }
    if (fs::exists("rf-sim-libraries")) {
        m_library.scan("rf-sim-libraries");
    }
    if (fs::exists("component_data/library")) {
        m_library.scan("component_data/library");
    }

    for (const auto *pack : m_extension_manager.dataPacks()) {
        for (const auto &root : pack->library_roots)
            m_library.scan(root.string());
    }
}

std::vector<ExtensionMenuEntry>
RfSimulatorApp::externalToolActions(const ExtensionManifest &manifest) const {
    if (!manifest.menus.empty())
        return manifest.menus;
    return {ExtensionMenuEntry{"tools", manifest.name}};
}

void RfSimulatorApp::runExternalTool(const ExtensionManifest &manifest,
                                     std::string_view action_label) {
    namespace fs = std::filesystem;

    const std::string effective_action_label =
        action_label.empty() ? manifest.name : std::string(action_label);
    const fs::path project_root = m_current_project_path.empty()
                                      ? fs::current_path()
                                      : fs::path(m_current_project_path).parent_path();
    const fs::path selected_path =
        m_current_project_path.empty() ? fs::path{} : fs::path(m_current_project_path);
    const fs::path work_dir = fs::temp_directory_path() / "rf-sim-extension-run" / manifest.id;
    const fs::path result_path = work_dir / "result.json";
    std::error_code ec;
    fs::remove(result_path, ec);

    const ExternalToolRequest request{
        "1", effective_action_label, project_root, selected_path, work_dir, result_path};

    const auto result = m_external_tool_runner.run(manifest, request);
    m_extension_result_message = result.ok ? "Extension run succeeded: " + manifest.name
                                           : "Extension run failed: " + result.message;
    if (result.ok)
        refreshExtensions();
}

void RfSimulatorApp::drawExtensionsPanel() {
    if (!m_show_extensions)
        return;

    if (ImGui::Begin("Extensions", &m_show_extensions)) {
        if (ImGui::Button("Refresh"))
            refreshExtensions();
        if (!m_extension_result_message.empty())
            ImGui::TextWrapped("%s", m_extension_result_message.c_str());
        else
            ImGui::TextDisabled("No extension actions run yet.");

        ImGui::Separator();

        for (const auto &record : m_extension_manager.all()) {
            const bool has_manifest = record.manifest.has_value();
            const std::string label =
                has_manifest ? record.manifest->name : record.manifest_path.filename().string();
            const char *status = record.status == ExtensionStatusKind::Ok ? "Ok"
                                 : record.status == ExtensionStatusKind::Incompatible
                                     ? "Incompatible"
                                     : "Invalid";

            ImGui::Text("%s [%s]", label.c_str(), status);
            if (has_manifest && record.status == ExtensionStatusKind::Ok &&
                record.manifest->kind == ExtensionKind::ExternalTool) {
                const auto actions = externalToolActions(*record.manifest);
                for (std::size_t i = 0; i < actions.size(); ++i) {
                    ImGui::SameLine();
                    const std::string button_label = record.manifest->menus.empty()
                                                         ? "Run##" + record.manifest->id
                                                         : actions[i].label + "##" +
                                                               record.manifest->id + "-" +
                                                               std::to_string(i);
                    if (ImGui::Button(button_label.c_str()))
                        runExternalTool(*record.manifest, actions[i].label);
                }
            }

            if (!record.issues.empty()) {
                ImGui::Indent();
                for (const auto &issue : record.issues)
                    ImGui::TextWrapped("%s: %s", issue.field.c_str(), issue.message.c_str());
                ImGui::Unindent();
            }
        }
    }
    ImGui::End();
}

void RfSimulatorApp::saveProject(const std::string &path) {
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
        {std::type_index(typeid(AttenuatorEngine)), "Attenuator"},
        {std::type_index(typeid(CombinerEngine)), "Combiner"},
        {std::type_index(typeid(EqualizerEngine)), "Equalizer"},
        {std::type_index(typeid(AdcEngine)), "ADC"},
        {std::type_index(typeid(PFBChannelizerEngine)), "PFBChannelizer"},
        {std::type_index(typeid(CoaxCableEngine)), "CoaxCable"},
        {std::type_index(typeid(IdealFilterEngine)), "IdealFilter"},
    };

    // Ensure all engine nodes are registered with the imnodes context
    // so GetNodeEditorSpacePos() doesn't assert on node IDs added without
    // a prior render frame (e.g. via newProject then programmatic add).
    m_graph_widget->syncNodesFromEngine();

    // Save components by iterating the registry
    nlohmann::json comps_arr = nlohmann::json::array();
    for (auto *comp : m_components.all()) {
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

        // Save library part number if set
        for (const auto &gn : m_graph_engine.nodes()) {
            if (gn.node_id == nid && !gn.part_number.empty()) {
                cj["part_number"] = gn.part_number;
                break;
            }
        }

        comps_arr.push_back(cj);
    }
    root["components"] = comps_arr;

    // Save links as component-index + port pairs (not raw pin IDs)
    nlohmann::json links_arr = nlohmann::json::array();
    // Build a map: pin_id \u2192 {comp_index, port, is_output}
    struct PinInfo {
        size_t comp;
        int port;
        bool is_output;
    };
    std::unordered_map<int, PinInfo> pin_map;
    for (size_t i = 0; i < m_components.size(); ++i) {
        auto *comp = m_components.all()[i];
        int nid = comp->graphNodeId();
        for (const auto &gn : m_graph_engine.nodes()) {
            if (gn.node_id == nid) {
                for (size_t p = 0; p < gn.input_pin_ids.size(); ++p)
                    pin_map[gn.input_pin_ids[p]] = {i, (int)p, false};
                for (size_t p = 0; p < gn.output_pin_ids.size(); ++p)
                    pin_map[gn.output_pin_ids[p]] = {i, (int)p, true};
                break;
            }
        }
    }
    for (const auto &link : m_graph_engine.links()) {
        auto from_it = pin_map.find(link.start_pin_id);
        auto to_it = pin_map.find(link.end_pin_id);
        if (from_it == pin_map.end() || to_it == pin_map.end())
            continue;
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

    for (const auto &g : m_graph_engine.groups()) {
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

void RfSimulatorApp::loadProject(const std::string &path) {
    std::ifstream in(path);
    if (!in) {
        LOG_ERROR("Failed to open project file: %s", path.c_str());
        return;
    }
    nlohmann::json root;
    try {
        in >> root;
    } catch (const nlohmann::json::exception &e) {
        LOG_ERROR("Invalid project file: %s", e.what());
        return;
    }

    newProject();

    // Map: type string \u2192 factory lambda
    std::vector<nlohmann::json::iterator> comp_order;
    auto &comps = root["components"];
    for (auto it = comps.begin(); it != comps.end(); ++it)
        comp_order.push_back(it);

    // Create components in saved order
    std::vector<int> new_node_ids; // maps saved index \u2192 new graph node ID
    for (auto &it : comp_order) {
        auto &cj = *it;
        std::string type = cj.value("type", "");
        auto &params = cj["params"];

        IComponentEngine *comp = nullptr;
        if (type == "SignalGenerator") {
            auto &ref =
                m_components.add<SignalGeneratorEngine>(m_next_component_id++, m_graph_engine);
            ref.deserialize(params);
            comp = &ref;
        } else if (type == "Amplifier") {
            auto &ref = m_components.add<AmplifierEngine>(m_next_component_id++, m_graph_engine);
            ref.deserialize(params);
            comp = &ref;
        } else if (type == "Mixer") {
            auto &ref = m_components.add<MixerEngine>(m_next_component_id++, m_graph_engine);
            ref.deserialize(params);
            comp = &ref;
        } else if (type == "Splitter") {
            auto &ref = m_components.add<SplitterEngine>(m_next_component_id++, m_graph_engine);
            ref.deserialize(params);
            comp = &ref;
        } else if (type == "Attenuator") {
            auto &ref = m_components.add<AttenuatorEngine>(m_next_component_id++, m_graph_engine);
            ref.deserialize(params);
            comp = &ref;
        } else if (type == "Combiner") {
            auto &ref = m_components.add<CombinerEngine>(m_next_component_id++, m_graph_engine);
            ref.deserialize(params);
            comp = &ref;
        } else if (type == "Equalizer") {
            auto &ref = m_components.add<EqualizerEngine>(m_next_component_id++, m_graph_engine);
            ref.deserialize(params);
            comp = &ref;
        } else if (type == "ADC") {
            auto &ref = m_components.add<AdcEngine>(m_next_component_id++, m_graph_engine);
            ref.deserialize(params);
            comp = &ref;
        } else if (type == "PFBChannelizer") {
            auto &ref =
                m_components.add<PFBChannelizerEngine>(m_next_component_id++, m_graph_engine);
            ref.deserialize(params);
            // Restore IQ plot + PFB grid widgets for this PFB
            m_iq_widgets.push_back(std::make_unique<IQPlotWidget>(ref));
            m_show_iq_pfbs.push_back(true);
            m_pfb_grid_widgets.push_back(std::make_unique<PFBChannelizerWidget>(ref));
            m_show_pfb_grids.push_back(true);
            comp = &ref;
        } else if (type == "CoaxCable") {
            auto &ref = m_components.add<CoaxCableEngine>(m_next_component_id++, m_graph_engine);
            ref.deserialize(params);
            comp = &ref;
        } else if (type == "IdealFilter") {
            auto &ref = m_components.add<IdealFilterEngine>(m_next_component_id++, m_graph_engine);
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
            ImNodes::SetNodeEditorSpacePos(comp->graphNodeId(), ImVec2(cj["pos"].value("x", 0.0f),
                                                                       cj["pos"].value("y", 0.0f)));
        }

        // Restore library part number
        if (comp && cj.contains("part_number"))
            m_graph_engine.setNodePartNumber(comp->graphNodeId(),
                                             cj["part_number"].get<std::string>());
    }
    // After restoring all positions, inform the widget so subsequent
    // syncNodesFromEngine calls (e.g. from saveProject) don't reset them.
    m_graph_widget->markNodesRegistered();

    // Restore links (saved as component-index + port pairs)
    auto &saved_links = root["links"];
    for (const auto &lj : saved_links) {
        int from_idx = lj.value("from", -1);
        int to_idx = lj.value("to", -1);
        int from_port = lj.value("from_port", 0);
        int to_port = lj.value("to_port", 0);
        if (from_idx < 0 || to_idx < 0 || static_cast<size_t>(from_idx) >= new_node_ids.size() ||
            static_cast<size_t>(to_idx) >= new_node_ids.size())
            continue;

        int from_node = new_node_ids[from_idx];
        int to_node = new_node_ids[to_idx];
        if (from_node < 0 || to_node < 0)
            continue;

        auto *from_comp = m_components.find(from_node);
        auto *to_comp = m_components.find(to_node);
        if (!from_comp || !to_comp)
            continue;

        int start_pin = from_comp->outputPinId(from_port);
        int end_pin = to_comp->inputPinId(to_port);
        if (start_pin >= 0 && end_pin >= 0)
            m_graph_engine.addLink(start_pin, end_pin);
    }

    // Restore probes
    auto &saved_probes = root["probe_pins"];
    for (const auto &pj : saved_probes) {
        int comp_idx = pj.value("comp", -1);
        int port = pj.value("port", 0);
        bool is_output = pj.value("is_output", true);
        if (comp_idx < 0 || static_cast<size_t>(comp_idx) >= m_components.size())
            continue;
        auto *comp = m_components.all()[comp_idx];
        int pin = is_output ? comp->outputPinId(port) : comp->inputPinId(port);
        if (pin >= 0)
            m_graph_engine.addProbePin(pin);
    }

    // Restore groups
    auto &saved_groups = root["groups"];
    for (const auto &gj : saved_groups) {
        std::string name = gj.value("name", "Group");
        std::vector<int> member_ids;
        for (const auto &mj : gj["member_components"]) {
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
    auto &ws = root["window_state"];
    if (!ws.is_null()) {
        m_show_log = ws.value("log", true);
        m_show_spectrum = ws.value("spectrum_analyzer", true);
        m_show_properties = ws.value("properties", true);
        m_show_node_editor = ws.value("node_editor", true);
    }

    // Restore graph state counters
    auto &gs = root["graph_state"];
    if (!gs.is_null()) {
        m_next_component_id = gs.value("next_component_id", m_next_component_id);
    }

    m_current_project_path = path;
    refreshExtensions();

    m_dirty = false;
    LOG_INFO("Loaded project from %s", path.c_str());
}

void RfSimulatorApp::openFileDialog() {
    auto result = pfd::open_file("Open Project", ".",
                                 {"RF Simulator Project (*.rfsim)", "*.rfsim", "All Files", "*"})
                      .result();
    if (!result.empty())
        loadProject(result[0]);
}

void RfSimulatorApp::saveFileDialog() {
    auto result =
        pfd::save_file("Save Project As", ".", {"RF Simulator Project (*.rfsim)", "*.rfsim"})
            .result();
    if (!result.empty())
        saveProject(result);
}

void RfSimulatorApp::openNewComponentForm(const std::string &type) {
    const auto *descriptor = ComponentTypeRegistry::instance().find(type);
    if (!descriptor)
        return;
    m_component_form_is_edit = false;
    m_component_form_destination_root.clear();
    m_component_form_model = std::make_unique<ComponentFormModel>(*descriptor);
    m_component_form_widget = std::make_unique<ComponentFormWidget>(*m_component_form_model);
    m_component_form_error.clear();
    m_show_component_form = true;
}

void RfSimulatorApp::openEditComponentForm(const ComponentDefinition &def) {
    const auto *descriptor = ComponentTypeRegistry::instance().find(def.type);
    if (!descriptor)
        return;
    m_component_form_is_edit = true;
    m_component_form_model = std::make_unique<ComponentFormModel>(*descriptor);
    m_component_form_model->loadFrom(def);
    m_component_form_widget = std::make_unique<ComponentFormWidget>(*m_component_form_model);
    m_component_form_error.clear();
    m_show_component_form = true;
}

bool RfSimulatorApp::saveComponentForm() {
    namespace fs = std::filesystem;
    auto &model = *m_component_form_model;
    auto def = model.buildDefinition();

    std::string root;
    if (m_component_form_is_edit) {
        // Overwrite in place — no rename-on-identity-change.
        def.source_path = model.sourcePath();
    } else {
        root = m_component_form_destination_root;
        if (root.empty()) {
            m_component_form_error = "Choose a destination (Project or Global) before saving.";
            return false;
        }
        std::string safe_man = sanitizePathSegment(def.manufacturer, "unknown");
        std::string safe_pn = sanitizePathSegment(def.part_number, "component");
        fs::path dir = fs::path(root) / def.type / safe_man;
        def.source_path = (dir / (safe_pn + ".json")).string();
        if (fs::exists(def.source_path)) {
            m_component_form_error = "A component already exists at " + def.source_path;
            return false;
        }
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (ec) {
            m_component_form_error = "Could not create directory: " + dir.string();
            return false;
        }
    }

    // Copy S-param file into place next to the destination JSON, if one was picked.
    if (!model.sparamSourcePath().empty()) {
        fs::path dest_dir = fs::path(def.source_path).parent_path();
        fs::path dest_sparam = dest_dir / def.data_files[0].path;
        std::error_code ec;
        fs::copy_file(model.sparamSourcePath(), dest_sparam, fs::copy_options::overwrite_existing,
                      ec);
        if (ec) {
            m_component_form_error = "Failed to copy S-parameter file: " + ec.message();
            return false;
        }
    }

    nlohmann::json j;
    j["schema_version"] = def.schema_version;
    j["type"] = def.type;
    j["part_number"] = def.part_number;
    j["manufacturer"] = def.manufacturer;
    j["description"] = def.description;
    j["parameters"] = def.parameters;
    if (!def.test_conditions.empty())
        j["test_conditions"] = def.test_conditions;
    j["notes"] = def.notes;
    if (!def.data_files.empty()) {
        j["data_files"] = nlohmann::json::array();
        for (const auto &df : def.data_files)
            j["data_files"].push_back({{"type", df.type}, {"path", df.path}});
    }

    std::ofstream out(def.source_path);
    if (!out.is_open()) {
        m_component_form_error = "Could not write " + def.source_path;
        return false;
    }
    out << j.dump(2);
    out.close();

    def.issues = m_library.validate(def.type, def.parameters);
    m_library.upsert(def);
    return true;
}

void RfSimulatorApp::drawComponentFormModal() {
    if (!m_show_component_form)
        return;
    ImGui::OpenPopup("Component Form");
    ImGui::SetNextWindowSize(ImVec2(480, 520), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Component Form", &m_show_component_form)) {
        ImGui::TextUnformatted(m_component_form_is_edit ? "Edit Component" : "New Component");
        ImGui::Separator();
        if (!m_component_form_is_edit) {
            const char *type_names[] = {"amplifier", "attenuator", "splitter", "filter",
                                        "mixer",     "equalizer",  "combiner", "adc"};
            static int type_idx = 0;
            for (int i = 0; i < 8; ++i)
                if (m_component_form_model->descriptor().type == type_names[i])
                    type_idx = i;
            if (ImGui::Combo("Type", &type_idx, type_names, 8))
                openNewComponentForm(type_names[type_idx]);

            const char *roots[] = {"Project (./rf-sim-libraries)", "Global (~/.rf-sim/libraries)"};
            static int root_idx = 0;
            ImGui::Combo("Save To", &root_idx, roots, 2);
            const char *home = std::getenv(
#ifdef _WIN32
                "USERPROFILE"
#else
                "HOME"
#endif
            );
            m_component_form_destination_root =
                root_idx == 0 ? "rf-sim-libraries"
                : home        ? (std::filesystem::path(home) / ".rf-sim" / "libraries").string()
                              : "rf-sim-libraries";
            ImGui::Separator();
        }

        bool save_clicked = m_component_form_widget->draw(m_library);
        if (!m_component_form_error.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s",
                               m_component_form_error.c_str());

        if (save_clicked) {
            if (saveComponentForm()) {
                m_show_component_form = false;
                markDirty();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            m_show_component_form = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void RfSimulatorApp::update_dsp() {
    std::unordered_map<int, std::function<void()>> updates;

    for (auto *comp : m_components.all()) {
        int N = comp->numInputPins();
        for (int k = 0; k < N; ++k) {
            int pid = comp->inputPinId(k);
            if (pid >= 0) {
                auto *source = m_graph_engine.getSourceForInput(pid);
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
    for (auto *pn : probed_nodes) {
        std::string label;
        for (const auto &node : m_graph_engine.nodes()) {
            if (node.signal_node == pn) {
                label = node.label + " OUT";
                break;
            }
        }
        probe_labels.push_back(label);
    }
    m_spectrum_widget->setProbeLabels(probe_labels);

    for (auto *node : m_view_manager.nodes()) {
        if (node) {
            node->view_enabled =
                std::find(probed_nodes.begin(), probed_nodes.end(), node) != probed_nodes.end();
        }
    }

    // Sync PFB pointers to spectrum analyzer and inspector panel
    auto pfb_ptrs = m_components.byType<PFBChannelizerEngine>();
    std::vector<PFBChannelizerEngine *> pfb_vec(pfb_ptrs.begin(), pfb_ptrs.end());
    m_spectrum_widget->setPFBs(pfb_vec);
    m_inspector_panel->setPFBs(pfb_vec);
    m_inspector_panel->setPFBWindowVisibility(&m_show_iq_pfbs, &m_show_pfb_grids);
}

void RfSimulatorApp::draw_ui() {
    ImGuiIO &io = ImGui::GetIO();
    (void)io;

    // Reset the unsaved dialog flag at the start of each frame.
    // The menu handlers below set it to true only when m_dirty is set.
    m_show_unsaved_dialog = false;

    // File menu bar
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New", "Ctrl+N")) {
                if (m_dirty) {
                    m_pending_action = PendingAction::New;
                    m_show_unsaved_dialog = true;
                } else
                    newProject();
            }
            if (ImGui::MenuItem("Open...", "Ctrl+O")) {
                if (m_dirty) {
                    m_pending_action = PendingAction::Open;
                    m_show_unsaved_dialog = true;
                } else
                    openFileDialog();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save", "Ctrl+S")) {
                if (!m_current_project_path.empty())
                    saveProject(m_current_project_path);
                else
                    saveFileDialog();
            }
            if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S"))
                saveFileDialog();
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) {
                if (m_dirty) {
                    m_pending_action = PendingAction::Exit;
                    m_show_unsaved_dialog = true;
                } else
                    std::exit(0);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Log", nullptr, &m_show_log);
            ImGui::MenuItem("Spectrum Analyzer", nullptr, &m_show_spectrum);
            ImGui::MenuItem("Properties", nullptr, &m_show_properties);
            ImGui::MenuItem("Node Editor", nullptr, &m_show_node_editor);
            ImGui::MenuItem("Component Library", nullptr, &m_show_library);
            ImGui::Separator();
            if (ImGui::BeginMenu("Layouts")) {
                if (ImGui::MenuItem("Save As...")) {
                    m_layout_name_buf[0] = '\0';
                    m_show_save_layout_dialog = true;
                }
                auto layout_names = m_layout_manager.listNamedLayouts();
                if (ImGui::BeginMenu("Load", !layout_names.empty())) {
                    for (const auto &name : layout_names) {
                        if (ImGui::MenuItem(name.c_str()))
                            m_layout_manager.loadNamedLayout(name);
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem("Manage..."))
                    m_show_manage_layouts_dialog = true;
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Tools")) {
            ImGui::MenuItem("Extensions", nullptr, &m_show_extensions);
            ImGui::Separator();
            for (const auto *tool : m_extension_manager.externalTools()) {
                if (!tool)
                    continue;
                for (const auto &action : externalToolActions(*tool)) {
                    if (action.location == "tools" && ImGui::MenuItem(action.label.c_str()))
                        runExternalTool(*tool, action.label);
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("How to Use", "F1"))
                m_show_help = !m_show_help;
            ImGui::EndMenu();
        }
        // Title / project name on the right
        {
            float tw = ImGui::GetContentRegionAvail().x;
            ImGui::SameLine(tw - 300.0f);
            if (!m_current_project_path.empty()) {
                auto p = m_current_project_path.find_last_of("\\/");
                std::string fname = (p != std::string::npos) ? m_current_project_path.substr(p + 1)
                                                             : m_current_project_path;
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
            if (!m_current_project_path.empty())
                saveProject(m_current_project_path);
            else
                saveFileDialog();
        }
        if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S))
            saveFileDialog();
        if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_O)) {
            if (m_dirty) {
                m_pending_action = PendingAction::Open;
                m_show_unsaved_dialog = true;
            } else
                openFileDialog();
        }
        if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_N)) {
            if (m_dirty) {
                m_pending_action = PendingAction::New;
                m_show_unsaved_dialog = true;
            } else
                newProject();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F1))
            m_show_help = !m_show_help;
    }

    // Unsaved Changes popup — use a bool flag instead of OpenPopup/BeginPopupModal,
    // which can be unreliable when called from inside a menu bar context.
    if (m_show_unsaved_dialog) {
        ImGui::OpenPopup("Unsaved Changes");
    }
    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("You have unsaved changes. Save before continuing?");
        if (ImGui::Button("Save", ImVec2(120, 0))) {
            if (!m_current_project_path.empty())
                saveProject(m_current_project_path);
            else {
                auto path = pfd::save_file("Save Project As", ".",
                                           {"RF Simulator Project (*.rfsim)", "*.rfsim"})
                                .result();
                if (!path.empty())
                    saveProject(path);
            }
            if (!m_dirty) {
                // Save succeeded — execute the pending action now
                auto action = m_pending_action;
                m_pending_action = PendingAction::None;
                m_show_unsaved_dialog = false;
                ImGui::CloseCurrentPopup();
                switch (action) {
                case PendingAction::New:
                    newProject();
                    break;
                case PendingAction::Open:
                    openFileDialog();
                    break;
                case PendingAction::Exit:
                    std::exit(0);
                    break;
                default:
                    break;
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard", ImVec2(120, 0))) {
            auto action = m_pending_action;
            m_pending_action = PendingAction::None;
            m_show_unsaved_dialog = false;
            ImGui::CloseCurrentPopup();
            // Execute immediately — user chose to discard
            switch (action) {
            case PendingAction::New:
                newProject();
                break;
            case PendingAction::Open:
                openFileDialog();
                break;
            case PendingAction::Exit:
                std::exit(0);
                break;
            default:
                break;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            m_pending_action = PendingAction::None;
            m_show_unsaved_dialog = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Save Layout As popup
    if (m_show_save_layout_dialog) {
        ImGui::OpenPopup("Save Layout As");
    }
    if (ImGui::BeginPopupModal("Save Layout As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Name", m_layout_name_buf, sizeof(m_layout_name_buf));
        bool can_save = m_layout_name_buf[0] != '\0';
        if (!can_save)
            ImGui::BeginDisabled();
        if (ImGui::Button("Save", ImVec2(120, 0))) {
            m_layout_manager.saveNamedLayout(m_layout_name_buf);
            m_layout_name_buf[0] = '\0';
            m_show_save_layout_dialog = false;
            ImGui::CloseCurrentPopup();
        }
        if (!can_save)
            ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            m_layout_name_buf[0] = '\0';
            m_show_save_layout_dialog = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Manage Layouts popup
    if (m_show_manage_layouts_dialog) {
        ImGui::OpenPopup("Manage Layouts");
    }
    if (ImGui::BeginPopupModal("Manage Layouts", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        auto names = m_layout_manager.listNamedLayouts();
        if (names.empty())
            ImGui::TextDisabled("No saved layouts.");
        for (const auto &name : names) {
            ImGui::PushID(name.c_str());
            if (m_rename_target == name) {
                ImGui::SetNextItemWidth(160);
                ImGui::InputText("##rename", m_rename_buf, sizeof(m_rename_buf));
                ImGui::SameLine();
                if (ImGui::Button("OK")) {
                    if (m_rename_buf[0] != '\0') {
                        if (m_layout_manager.renameNamedLayout(name, m_rename_buf))
                            m_rename_target.clear();
                        else
                            LOG_ERROR("Failed to rename layout '%s' to '%s'", name.c_str(),
                                      m_rename_buf);
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("X"))
                    m_rename_target.clear();
            } else {
                ImGui::Text("%s", name.c_str());
                ImGui::SameLine();
                if (ImGui::Button("Load"))
                    m_layout_manager.loadNamedLayout(name);
                ImGui::SameLine();
                if (ImGui::Button("Rename")) {
                    m_rename_target = name;
                    std::snprintf(m_rename_buf, sizeof(m_rename_buf), "%s", name.c_str());
                }
                ImGui::SameLine();
                if (ImGui::Button("Delete"))
                    m_layout_manager.deleteNamedLayout(name);
            }
            ImGui::PopID();
        }
        ImGui::Separator();
        if (ImGui::Button("Close", ImVec2(120, 0))) {
            m_rename_target.clear();
            m_show_manage_layouts_dialog = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    drawComponentFormModal();

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

    if (m_show_library && m_library_browser) {
        m_library_browser->draw("Component Library", &m_show_library);
    }
    drawExtensionsPanel();

    if (m_show_log)
        m_log_widget.draw("Log", &m_show_log);

    if (m_show_help)
        m_help_widget.draw("How to Use", &m_show_help);
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
    m_state.saveBool("WindowState", "Help", m_show_help);
}
