#include "app.h"
#include "imgui.h"
#include "logging_core.h"
#include "logging_widget.h"
#include "pfb_channelizer_engine.h"
#include "coax_cable_engine.h"
#include "attenuator_engine.h"
#include <algorithm>
#include <functional>
#include <unordered_map>

RfSimulatorApp::RfSimulatorApp()
    : m_components(m_graph_engine, m_view_manager) {
    m_graph_widget = std::make_unique<NodeGraphWidget>(m_graph_engine);
    m_graph_widget->onAddGenerator = [this]() {
        m_components.add<SignalGeneratorEngine>(m_next_component_id++, m_graph_engine);
    };
    m_graph_widget->onAddAmplifier = [this]() {
        m_components.add<AmplifierEngine>(m_next_component_id++, m_graph_engine);
    };
    m_graph_widget->onAddSplitter = [this]() {
        m_components.add<SplitterEngine>(m_next_component_id++, m_graph_engine);
    };
    m_graph_widget->onAddMixer = [this]() {
        m_components.add<MixerEngine>(m_next_component_id++, m_graph_engine);
    };
    m_graph_widget->onAddAdc = [this]() {
        m_components.add<AdcEngine>(m_next_component_id++, m_graph_engine);
    };
    m_graph_widget->onAddPFB = [this]() {
        auto& pfb = m_components.add<PFBChannelizerEngine>(m_next_component_id++, m_graph_engine);
        m_iq_widgets.push_back(std::make_unique<IQPlotWidget>(pfb));
        m_show_iq_pfbs.push_back(
            m_state.loadBool("WindowState", ("IQPlot_" + std::to_string(pfb.id())).c_str(), true));
        m_pfb_grid_widgets.push_back(std::make_unique<PFBChannelizerWidget>(pfb));
        m_show_pfb_grids.push_back(
            m_state.loadBool("WindowState", ("PFBGrid_" + std::to_string(pfb.id())).c_str(), true));
    };
    m_graph_widget->onAddCoaxCable = [this]() {
        m_components.add<CoaxCableEngine>(m_next_component_id++, m_graph_engine);
    };
    m_graph_widget->onAddEqualizer = [this]() {
        m_components.add<EqualizerEngine>(m_next_component_id++, m_graph_engine);
    };
    m_graph_widget->onAddIdealFilter = [this]() {
        m_components.add<IdealFilterEngine>(m_next_component_id++, m_graph_engine);
    };
    m_graph_widget->onAddAttenuator = [this]() {
        m_components.add<AttenuatorEngine>(m_next_component_id++, m_graph_engine);
    };
    m_graph_widget->onRemoveNode = [this](int id) {
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

    m_spectrum_widget = std::make_unique<SpectrumAnalyzerWidget>(m_spectrum_engine, m_view_manager);
    load_window_states();
}

void RfSimulatorApp::load_window_states() {
    m_show_log = m_state.loadBool("WindowState", "Log", true);
    m_show_spectrum = m_state.loadBool("WindowState", "SpectrumAnalyzer", true);
    m_show_properties = m_state.loadBool("WindowState", "Properties", true);
    m_show_node_editor = m_state.loadBool("WindowState", "NodeEditor", true);
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
    ImGui::Text("RF Simulator %s (%s) | %.3f ms/frame (%.1f FPS)", APP_VERSION,
                APP_GIT_HASH, 1000.0f / io.Framerate, io.Framerate);

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
