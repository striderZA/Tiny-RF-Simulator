#include "app.h"
#include "imgui.h"
#include <portable-file-dialogs.h>
#include "imnodes.h"
#include "logging_core.h"
#include "logging_widget.h"
#include "pfb_channelizer_engine.h"
#include <algorithm>
#include <fstream>
#include <functional>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <windows.h>

RfSimulatorApp::RfSimulatorApp() {
    m_graph_widget = std::make_unique<NodeGraphWidget>(m_graph_engine);
    m_graph_widget->onAddGenerator = [this]() { addGenerator(); };
    m_graph_widget->onAddAmplifier = [this]() { addAmplifier(); };
    m_graph_widget->onAddSplitter = [this]() { addSplitter(); };
    m_graph_widget->onAddMixer = [this]() { addMixer(); };
    m_graph_widget->onAddSParamAmp = [this]() { addSParamAmp(); };
    m_graph_widget->onAddSParamFilter = [this]() { addSParamFilter(); };
    m_graph_widget->onAddAdc = [this]() { addAdc(); };
    m_graph_widget->onAddPFB = [this]() { addPFB(); };
    m_graph_widget->onRemoveNode = [this](int id) { removeComponent(id); };
    m_graph_widget->onTopologyChange = [this]() { markDirty(); };

    addGenerator();
    m_generators[0]->addTone(100e6, -20.0);

    addAmplifier();

    m_inspector_panel = std::make_unique<InspectorPanel>(
        m_graph_engine, m_amplifiers, m_mixers, m_splitters,
        m_sparam_amps, m_sparam_filters, m_adcs, m_generators
    );
    m_inspector_panel->onRemoveNode = [this](int graph_node_id) {
        removeComponent(graph_node_id);
    };

    m_inspector_panel->setViewToggles({
        &m_show_log,
        &m_show_spectrum,
        &m_show_properties,
        nullptr, // iq_plot (per-PFB toggles used instead)
        &m_show_node_editor
    });

    m_graph_widget->onNodeHover = [this](int graph_node_id) -> std::string {
        for (auto& g : m_generators) if (g->graphNodeId() == graph_node_id) return g->hoverSummary();
        for (auto& a : m_amplifiers) if (a->graphNodeId() == graph_node_id) return a->hoverSummary();
        for (auto& s : m_splitters) if (s->graphNodeId() == graph_node_id) return s->hoverSummary();
        for (auto& m : m_mixers) if (m->graphNodeId() == graph_node_id) return m->hoverSummary();
        for (auto& s : m_sparam_amps) if (s->graphNodeId() == graph_node_id) return s->hoverSummary();
        for (auto& s : m_sparam_filters) if (s->graphNodeId() == graph_node_id) return s->hoverSummary();
        for (auto& a : m_adcs) if (a->graphNodeId() == graph_node_id) return a->hoverSummary();
        for (auto& p : m_pfbs) if (p->graphNodeId() == graph_node_id) return p->hoverSummary();
        return "";
    };

    m_spectrum_widget = std::make_unique<SpectrumAnalyzerWidget>(m_spectrum_engine, m_view_manager);
    load_window_states();

    // Auto-load last project on startup
    auto last = m_state.lastProject();
    if (!last.empty()) {
        m_state.setLastProject("");  // Clear first to prevent crash loop
        std::ifstream test(last);
        if (test.good()) {
            test.close();
            loadProject(last);  // Re-sets lastProject on success
        }
    }
}

void RfSimulatorApp::load_window_states() {
    // Window state loaded from project file; use defaults for fresh start
    m_show_log = true;
    m_show_spectrum = true;
    m_show_properties = true;
    m_show_node_editor = true;
}

void RfSimulatorApp::addGenerator() {
    int id = static_cast<int>(m_generators.size());
    auto gen = std::make_unique<SignalGeneratorEngine>(id, m_graph_engine);
    m_view_manager.registerNode(&gen->node());
    m_generator_widgets.push_back(std::make_unique<SignalGeneratorWidget>(*gen));
    m_generators.push_back(std::move(gen));
    LOG_INFO("Added generator %d", id);
    markDirty();
}

void RfSimulatorApp::addAmplifier() {
    int id = static_cast<int>(m_amplifiers.size());
    auto amp = std::make_unique<AmplifierEngine>(id, m_graph_engine);
    m_view_manager.registerNode(&amp->node());
    m_amplifiers.push_back(std::move(amp));
    LOG_INFO("Added amplifier %d", id);
    markDirty();
}

void RfSimulatorApp::addSplitter() {
    int id = static_cast<int>(m_splitters.size());
    auto split = std::make_unique<SplitterEngine>(id, m_graph_engine);
    m_view_manager.registerNode(&split->node());
    m_splitters.push_back(std::move(split));
    LOG_INFO("Added splitter %d", id);
    markDirty();
}

void RfSimulatorApp::addMixer() {
    int id = static_cast<int>(m_mixers.size());
    auto mix = std::make_unique<MixerEngine>(id, m_graph_engine);
    m_view_manager.registerNode(&mix->node());
    m_mixers.push_back(std::move(mix));
    LOG_INFO("Added mixer %d", id);
    markDirty();
}

void RfSimulatorApp::addSParamAmp() {
    int id = static_cast<int>(m_sparam_amps.size());
    std::string path = std::string(PROJECT_SOURCE_DIR) +
                       "/amplifier/data_files/adm-8344psm-s_parameters/ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    auto spamp = std::make_unique<SParameterAmplifierEngine>(id, m_graph_engine, path);
    m_view_manager.registerNode(&spamp->node());
    m_sparam_amps.push_back(std::move(spamp));
    LOG_INFO("Added S-parameter amplifier %d", id);
    markDirty();
}

void RfSimulatorApp::addSParamFilter() {
    int id = static_cast<int>(m_sparam_filters.size());
    std::string path = "";
    auto spf = std::make_unique<SParameterFilterEngine>(id, m_graph_engine, path);
    m_view_manager.registerNode(&spf->node());
    m_sparam_filters.push_back(std::move(spf));
    LOG_INFO("Added S-parameter filter %d", id);
    markDirty();
}

void RfSimulatorApp::addAdc() {
    int id = static_cast<int>(m_adcs.size());
    auto adc = std::make_unique<AdcEngine>(id, m_graph_engine);
    m_view_manager.registerNode(&adc->node());
    m_adcs.push_back(std::move(adc));
    LOG_INFO("Added ADC %d", id);
    markDirty();
}

void RfSimulatorApp::addPFB() {
    int id = static_cast<int>(m_pfbs.size());
    auto pfb = std::make_unique<PFBChannelizerEngine>(id, m_graph_engine);
    m_view_manager.registerNode(&pfb->node());
    m_pfbs.push_back(std::move(pfb));
    m_iq_widgets.push_back(std::make_unique<IQPlotWidget>(*m_pfbs.back()));
    m_show_iq_pfbs.push_back(
        m_state.loadBool("WindowState", ("IQPlot_" + std::to_string(m_pfbs.back()->id())).c_str(), true));
    LOG_INFO("Added PFB channelizer %d", id);
    markDirty();
}

void RfSimulatorApp::removeComponent(int graph_node_id) {
    // Find and remove generator
    for (size_t i = 0; i < m_generators.size(); ++i) {
        if (m_generators[i]->graphNodeId() == graph_node_id) {
            m_view_manager.unregisterNode(&m_generators[i]->node());
            m_graph_engine.removeNode(graph_node_id);
            m_generators.erase(m_generators.begin() + static_cast<std::ptrdiff_t>(i));
            m_generator_widgets.erase(m_generator_widgets.begin() + static_cast<std::ptrdiff_t>(i));
            markDirty();
            LOG_INFO("Removed generator (graph node %d)", graph_node_id);
            return;
        }
    }
    // Find and remove amplifier
    for (size_t i = 0; i < m_amplifiers.size(); ++i) {
        if (m_amplifiers[i]->graphNodeId() == graph_node_id) {
            m_view_manager.unregisterNode(&m_amplifiers[i]->node());
            m_graph_engine.removeNode(graph_node_id);
            m_amplifiers.erase(m_amplifiers.begin() + static_cast<std::ptrdiff_t>(i));
            markDirty();
            LOG_INFO("Removed amplifier (graph node %d)", graph_node_id);
            return;
        }
    }
    // Find and remove splitter
    for (size_t i = 0; i < m_splitters.size(); ++i) {
        if (m_splitters[i]->graphNodeId() == graph_node_id) {
            m_view_manager.unregisterNode(&m_splitters[i]->node());
            m_graph_engine.removeNode(graph_node_id);
            m_splitters.erase(m_splitters.begin() + static_cast<std::ptrdiff_t>(i));
            markDirty();
            LOG_INFO("Removed splitter (graph node %d)", graph_node_id);
            return;
        }
    }
    // Find and remove mixer
    for (size_t i = 0; i < m_mixers.size(); ++i) {
        if (m_mixers[i]->graphNodeId() == graph_node_id) {
            m_view_manager.unregisterNode(&m_mixers[i]->node());
            m_graph_engine.removeNode(graph_node_id);
            m_mixers.erase(m_mixers.begin() + static_cast<std::ptrdiff_t>(i));
            markDirty();
            LOG_INFO("Removed mixer (graph node %d)", graph_node_id);
            return;
        }
    }
    // Find and remove S-parameter amplifier
    for (size_t i = 0; i < m_sparam_amps.size(); ++i) {
        if (m_sparam_amps[i]->graphNodeId() == graph_node_id) {
            m_view_manager.unregisterNode(&m_sparam_amps[i]->node());
            m_graph_engine.removeNode(graph_node_id);
            m_sparam_amps.erase(m_sparam_amps.begin() + static_cast<std::ptrdiff_t>(i));
            markDirty();
            LOG_INFO("Removed S-parameter amplifier (graph node %d)", graph_node_id);
            return;
        }
    }
    // Find and remove S-parameter filter
    for (size_t i = 0; i < m_sparam_filters.size(); ++i) {
        if (m_sparam_filters[i]->graphNodeId() == graph_node_id) {
            m_view_manager.unregisterNode(&m_sparam_filters[i]->node());
            m_graph_engine.removeNode(graph_node_id);
            m_sparam_filters.erase(m_sparam_filters.begin() + static_cast<std::ptrdiff_t>(i));
            markDirty();
            LOG_INFO("Removed S-parameter filter (graph node %d)", graph_node_id);
            return;
        }
    }
    // Find and remove ADC
    for (size_t i = 0; i < m_adcs.size(); ++i) {
        if (m_adcs[i]->graphNodeId() == graph_node_id) {
            m_view_manager.unregisterNode(&m_adcs[i]->node());
            m_graph_engine.removeNode(graph_node_id);
            m_adcs.erase(m_adcs.begin() + static_cast<std::ptrdiff_t>(i));
            markDirty();
            LOG_INFO("Removed ADC (graph node %d)", graph_node_id);
            return;
        }
    }
    // Find and remove PFB channelizer
    for (size_t i = 0; i < m_pfbs.size(); ++i) {
        if (m_pfbs[i]->graphNodeId() == graph_node_id) {
            m_view_manager.unregisterNode(&m_pfbs[i]->node());
            m_graph_engine.removeNode(graph_node_id);
            m_iq_widgets.erase(m_iq_widgets.begin() + static_cast<std::ptrdiff_t>(i));
            m_show_iq_pfbs.erase(m_show_iq_pfbs.begin() + static_cast<std::ptrdiff_t>(i));
            m_pfbs.erase(m_pfbs.begin() + static_cast<std::ptrdiff_t>(i));
            markDirty();
            LOG_INFO("Removed PFB channelizer (graph node %d)", graph_node_id);
            return;
        }
    }
}

void RfSimulatorApp::update_dsp() {
    // Propagate engine-level dirty flags to app-level dirty
    for (auto& gen : m_generators) if (gen->isDirty()) { m_dirty = true; break; }
    if (!m_dirty) for (auto& amp : m_amplifiers) if (amp->isDirty()) { m_dirty = true; break; }
    if (!m_dirty) for (auto& s : m_splitters) if (s->isDirty()) { m_dirty = true; break; }
    if (!m_dirty) for (auto& m : m_mixers) if (m->isDirty()) { m_dirty = true; break; }
    if (!m_dirty) for (auto& s : m_sparam_amps) if (s->isDirty()) { m_dirty = true; break; }
    if (!m_dirty) for (auto& s : m_sparam_filters) if (s->isDirty()) { m_dirty = true; break; }
    if (!m_dirty) for (auto& a : m_adcs) if (a->isDirty()) { m_dirty = true; break; }
    if (!m_dirty) for (auto& p : m_pfbs) if (p->isDirty()) { m_dirty = true; break; }

    std::unordered_map<int, std::function<void()>> updates;

    for (auto& gen : m_generators)
        updates[gen->graphNodeId()] = [ptr = gen.get()]() { ptr->update(0.0); };

    auto addWiredUpdate = [&](auto& components) {
        for (auto& comp : components)
            updates[comp->graphNodeId()] = [this, ptr = comp.get()]() {
                auto* source = this->m_graph_engine.getSourceForInput(ptr->inputPinId());
                ptr->node().inputs[0] = source ? &source->outputs[0] : nullptr;
                ptr->update(0.0);
            };
    };

    addWiredUpdate(m_amplifiers);
    addWiredUpdate(m_splitters);
    addWiredUpdate(m_mixers);
    addWiredUpdate(m_sparam_amps);
    addWiredUpdate(m_sparam_filters);
    addWiredUpdate(m_adcs);
    addWiredUpdate(m_pfbs);

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
    std::vector<PFBChannelizerEngine*> pfb_ptrs;
    for (auto& pfb : m_pfbs)
        pfb_ptrs.push_back(pfb.get());
    m_spectrum_widget->setPFBs(pfb_ptrs);
    m_inspector_panel->setPFBs(pfb_ptrs);
}

void RfSimulatorApp::draw_ui() {
    // Menu bar
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New")) {
                if (m_dirty) promptUnsaved(PendingAction::New);
                else newProject();
            }
            if (ImGui::MenuItem("Open...", "Ctrl+O")) {
                if (m_dirty) promptUnsaved(PendingAction::Open);
                else openFileDialog();
            }
            if (ImGui::MenuItem("Save", "Ctrl+S")) {
                if (!m_current_project_path.empty())
                    saveProject(m_current_project_path);
                else
                    saveFileDialog();
            }
            if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) {
                saveFileDialog();
            }
            ImGui::Separator();
            auto recents = m_state.recentFiles();
            for (const auto& f : recents) {
                if (ImGui::MenuItem(f.c_str())) {
                    if (m_dirty) promptUnsaved(PendingAction::OpenRecent, f);
                    else loadProject(f);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) {
                if (m_dirty) promptUnsaved(PendingAction::Exit);
                else exit(0);
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
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About")) {
                ImGui::OpenPopup("About");
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // About popup
    if (ImGui::BeginPopupModal("About", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("RF Simulator %s", APP_VERSION);
        ImGui::Text("Git: %s", APP_GIT_HASH);
        if (ImGui::Button("OK", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Keyboard shortcuts
    auto& io = ImGui::GetIO();
    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S)) {
        if (!m_current_project_path.empty())
            saveProject(m_current_project_path);
        else
            saveFileDialog();
    }
    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_O)) {
        if (m_dirty) promptUnsaved(PendingAction::Open);
        else openFileDialog();
    }
    if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S)) {
        saveFileDialog();
    }

    // Unsaved changes popup
    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("You have unsaved changes. Save before continuing?");
        if (ImGui::Button("Save", ImVec2(120, 0))) {
            if (!m_current_project_path.empty())
                saveProject(m_current_project_path);
            else
                saveFileDialog();
            if (!m_dirty) {
                m_deferred_action = m_pending_action;
                m_deferred_path = m_pending_path;
                m_pending_action = PendingAction::None;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard", ImVec2(120, 0))) {
            m_deferred_action = m_pending_action;
            m_deferred_path = m_pending_path;
            m_pending_action = PendingAction::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            m_pending_action = PendingAction::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Execute deferred action (outside the popup to avoid ImGui state issues with blocking dialogs)
    if (m_deferred_action != PendingAction::None) {
        PendingAction action = m_deferred_action;
        m_deferred_action = PendingAction::None;
        switch (action) {
            case PendingAction::New: newProject(); break;
            case PendingAction::Open: openFileDialog(); break;
            case PendingAction::OpenRecent: loadProject(m_deferred_path); break;
            case PendingAction::Exit: exit(0); break;
            default: break;
        }
    }

    // Title bar showing project name
    std::string title = "RF Simulator";
    if (!m_current_project_path.empty()) {
        auto pos = m_current_project_path.find_last_of("\\/");
        title = (pos != std::string::npos) ? m_current_project_path.substr(pos + 1) : m_current_project_path;
        title = (m_dirty ? "*" : "") + title + " - RF Simulator";
    } else if (m_dirty) {
        title = "*Untitled - RF Simulator";
    }

    ImGuiIO &imgui_io = ImGui::GetIO();
    (void)imgui_io;
    ImGui::Text("%s %s (%s) | %.3f ms/frame (%.1f FPS)", title.c_str(), APP_VERSION,
                APP_GIT_HASH, 1000.0f / imgui_io.Framerate, imgui_io.Framerate);

    if (m_show_node_editor)
        m_graph_widget->draw("Node Editor", &m_show_node_editor);

    if (m_show_spectrum)
        m_spectrum_widget->draw("Spectrum Analyzer", &m_show_spectrum);

    for (size_t i = 0; i < m_pfbs.size(); ++i) {
        if (m_show_iq_pfbs[i]) {
            std::string label = "IQ Plot - PFB " + std::to_string(i);
            bool show = m_show_iq_pfbs[i];
            m_iq_widgets[i]->draw(label.c_str(), &show);
            m_show_iq_pfbs[i] = show;
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
    // Window state now persisted in project file
}

void RfSimulatorApp::newProject() {
    // Clear all component vectors
    m_generator_widgets.clear();
    m_view_manager.clearNodes();
    m_generators.clear();
    m_amplifiers.clear();
    m_splitters.clear();
    m_mixers.clear();
    m_sparam_amps.clear();
    m_sparam_filters.clear();
    m_adcs.clear();
    m_pfbs.clear();
    m_iq_widgets.clear();
    m_show_iq_pfbs.clear();

    // Clear graph engine
    while (!m_graph_engine.nodes().empty())
        m_graph_engine.removeNode(m_graph_engine.nodes().back().node_id);

    // Reset graph engine ID counters
    m_graph_engine.setNextIds(1, 100, 1000);

    // Clear spectrum probes
    m_graph_engine.clearProbes();
    m_spectrum_widget->setProbeLabels({});

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

    nlohmann::json nodes_arr = nlohmann::json::array();
    nlohmann::json links_arr = nlohmann::json::array();
    nlohmann::json probe_arr = nlohmann::json::array();

    auto serializeVec = [&](auto& vec, const std::string& type) {
        for (auto& comp : vec) {
            nlohmann::json node;
            int nid = comp->graphNodeId();
            node["node_id"] = nid;
            node["type"] = type;
            for (const auto& gn : m_graph_engine.nodes()) {
                if (gn.node_id == nid) {
                    node["label"] = gn.label;
                    break;
                }
            }
            ImNodes::EditorContextSet(m_graph_widget->context());
            ImVec2 pos = ImNodes::GetNodeEditorSpacePos(nid);
            node["x"] = pos.x;
            node["y"] = pos.y;
            comp->serialize(node["params"]);
            nodes_arr.push_back(node);
        }
    };

    serializeVec(m_generators, "SignalGenerator");
    serializeVec(m_amplifiers, "Amplifier");
    serializeVec(m_splitters, "Splitter");
    serializeVec(m_mixers, "Mixer");
    serializeVec(m_sparam_amps, "SParameterAmplifier");
    serializeVec(m_sparam_filters, "SParameterFilter");
    serializeVec(m_adcs, "ADC");
    serializeVec(m_pfbs, "PFBChannelizer");

    for (const auto& link : m_graph_engine.links()) {
        nlohmann::json l;
        l["link_id"] = link.link_id;
        l["start_pin_id"] = link.start_pin_id;
        l["end_pin_id"] = link.end_pin_id;
        links_arr.push_back(l);
    }

    for (int pin : m_graph_engine.probePins())
        probe_arr.push_back(pin);

    root["graph"]["nodes"] = nodes_arr;
    root["graph"]["links"] = links_arr;
    root["graph"]["probe_pins"] = probe_arr;

    root["window_state"]["log"] = m_show_log;
    root["window_state"]["spectrum_analyzer"] = m_show_spectrum;
    root["window_state"]["properties"] = m_show_properties;
    root["window_state"]["node_editor"] = m_show_node_editor;
    nlohmann::json iq_arr = nlohmann::json::array();
    for (size_t i = 0; i < m_show_iq_pfbs.size(); ++i)
        iq_arr.push_back(m_show_iq_pfbs[i]);
    root["window_state"]["iq_plots"] = iq_arr;

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

    auto& nodes = root["graph"]["nodes"];
    for (const auto& nj : nodes) {
        std::string type = nj.value("type", "");
        auto params = nj.value("params", nlohmann::json::object());
        int node_id = nj.value("node_id", -1);
        float x = nj.value("x", 0.0f);
        float y = nj.value("y", 0.0f);

        if (type == "SignalGenerator") {
            addGenerator();
            m_generators.back()->deserialize(params);
        } else if (type == "Amplifier") {
            addAmplifier();
            m_amplifiers.back()->deserialize(params);
        } else if (type == "Splitter") {
            addSplitter();
            m_splitters.back()->deserialize(params);
        } else if (type == "Mixer") {
            addMixer();
            m_mixers.back()->deserialize(params);
        } else if (type == "SParameterAmplifier") {
            addSParamAmp();
            m_sparam_amps.back()->deserialize(params);
        } else if (type == "SParameterFilter") {
            addSParamFilter();
            m_sparam_filters.back()->deserialize(params);
        } else if (type == "ADC") {
            addAdc();
            m_adcs.back()->deserialize(params);
        } else if (type == "PFBChannelizer") {
            addPFB();
            m_pfbs.back()->deserialize(params);
        } else {
            LOG_WARN("Unknown component type in project file: %s", type.c_str());
            continue;
        }
        ImNodes::EditorContextSet(m_graph_widget->context());
        ImNodes::SetNodeEditorSpacePos(node_id, ImVec2(x, y));
    }

    auto& links = root["graph"]["links"];
    for (const auto& lj : links) {
        int start = lj.value("start_pin_id", -1);
        int end = lj.value("end_pin_id", -1);
        if (start >= 0 && end >= 0)
            m_graph_engine.addLink(start, end);
    }

    auto& probes = root["graph"]["probe_pins"];
    for (const auto& pj : probes) {
        if (pj.is_number_integer())
            m_graph_engine.addProbePin(pj.get<int>());
    }

    auto& ws = root["window_state"];
    if (!ws.is_null()) {
        m_show_log = ws.value("log", true);
        m_show_spectrum = ws.value("spectrum_analyzer", true);
        m_show_properties = ws.value("properties", true);
        m_show_node_editor = ws.value("node_editor", true);
        auto& iq = ws["iq_plots"];
        if (iq.is_array()) {
            for (size_t i = 0; i < iq.size() && i < m_show_iq_pfbs.size(); ++i)
                m_show_iq_pfbs[i] = iq[i].get<bool>();
        }
    }

    int max_node_id = 0, max_pin_id = 0, max_link_id = 0;
    for (const auto& gn : m_graph_engine.nodes()) {
        max_node_id = (std::max)(max_node_id, gn.node_id);
        for (int p : gn.input_pin_ids) max_pin_id = (std::max)(max_pin_id, p);
        for (int p : gn.output_pin_ids) max_pin_id = (std::max)(max_pin_id, p);
    }
    for (const auto& link : m_graph_engine.links())
        max_link_id = (std::max)(max_link_id, link.link_id);
    m_graph_engine.setNextIds(max_node_id + 1, max_pin_id + 1, max_link_id + 1);

    m_current_project_path = path;
    m_dirty = false;
    m_state.addRecentFile(path);
    m_state.setLastProject(path);
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
    auto result = pfd::save_file("Save Project As", ".",
        {"RF Simulator Project (*.rfsim)", "*.rfsim"})
        .result();
    if (!result.empty())
        saveProject(result);
}

void RfSimulatorApp::promptUnsaved(PendingAction action, const std::string& path) {
    m_pending_action = action;
    m_pending_path = path;
    ImGui::OpenPopup("Unsaved Changes");
}
