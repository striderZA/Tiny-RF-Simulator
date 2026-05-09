#include "app.h"
#include "imgui.h"
#include "logging_core.h"
#include "logging_widget.h"
#include "pfb_channelizer_engine.h"
#include <algorithm>
#include <functional>
#include <unordered_map>

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
}

void RfSimulatorApp::load_window_states() {
    m_show_log = m_state.loadBool("WindowState", "Log", true);
    m_show_spectrum = m_state.loadBool("WindowState", "SpectrumAnalyzer", true);
    m_show_properties = m_state.loadBool("WindowState", "Properties", true);
    m_show_node_editor = m_state.loadBool("WindowState", "NodeEditor", true);
}

void RfSimulatorApp::addGenerator() {
    int id = static_cast<int>(m_generators.size());
    auto gen = std::make_unique<SignalGeneratorEngine>(id, m_graph_engine);
    m_view_manager.registerNode(&gen->node());
    m_generator_widgets.push_back(std::make_unique<SignalGeneratorWidget>(*gen));
    m_generators.push_back(std::move(gen));
    LOG_INFO("Added generator %d", id);
}

void RfSimulatorApp::addAmplifier() {
    int id = static_cast<int>(m_amplifiers.size());
    auto amp = std::make_unique<AmplifierEngine>(id, m_graph_engine);
    m_view_manager.registerNode(&amp->node());
    m_amplifiers.push_back(std::move(amp));
    LOG_INFO("Added amplifier %d", id);
}

void RfSimulatorApp::addSplitter() {
    int id = static_cast<int>(m_splitters.size());
    auto split = std::make_unique<SplitterEngine>(id, m_graph_engine);
    m_view_manager.registerNode(&split->node());
    m_splitters.push_back(std::move(split));
    LOG_INFO("Added splitter %d", id);
}

void RfSimulatorApp::addMixer() {
    int id = static_cast<int>(m_mixers.size());
    auto mix = std::make_unique<MixerEngine>(id, m_graph_engine);
    m_view_manager.registerNode(&mix->node());
    m_mixers.push_back(std::move(mix));
    LOG_INFO("Added mixer %d", id);
}

void RfSimulatorApp::addSParamAmp() {
    int id = static_cast<int>(m_sparam_amps.size());
    std::string path = std::string(PROJECT_SOURCE_DIR) +
                       "/amplifier/data_files/adm-8344psm-s_parameters/ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    auto spamp = std::make_unique<SParameterAmplifierEngine>(id, m_graph_engine, path);
    m_view_manager.registerNode(&spamp->node());
    m_sparam_amps.push_back(std::move(spamp));
    LOG_INFO("Added S-parameter amplifier %d", id);
}

void RfSimulatorApp::addSParamFilter() {
    int id = static_cast<int>(m_sparam_filters.size());
    std::string path = std::string(PROJECT_SOURCE_DIR) +
                       "/amplifier/data_files/adm-8344psm-s_parameters/ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    auto spf = std::make_unique<SParameterFilterEngine>(id, m_graph_engine, path);
    m_view_manager.registerNode(&spf->node());
    m_sparam_filters.push_back(std::move(spf));
    LOG_INFO("Added S-parameter filter %d", id);
}

void RfSimulatorApp::addAdc() {
    int id = static_cast<int>(m_adcs.size());
    auto adc = std::make_unique<AdcEngine>(id, m_graph_engine);
    m_view_manager.registerNode(&adc->node());
    m_adcs.push_back(std::move(adc));
    LOG_INFO("Added ADC %d", id);
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
}

void RfSimulatorApp::removeComponent(int graph_node_id) {
    // Find and remove generator
    for (size_t i = 0; i < m_generators.size(); ++i) {
        if (m_generators[i]->graphNodeId() == graph_node_id) {
            m_view_manager.unregisterNode(&m_generators[i]->node());
            m_graph_engine.removeNode(graph_node_id);
            m_generators.erase(m_generators.begin() + static_cast<std::ptrdiff_t>(i));
            m_generator_widgets.erase(m_generator_widgets.begin() + static_cast<std::ptrdiff_t>(i));
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
            LOG_INFO("Removed PFB channelizer (graph node %d)", graph_node_id);
            return;
        }
    }
}

void RfSimulatorApp::update_dsp() {
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
    ImGuiIO &io = ImGui::GetIO();
    (void)io;
    ImGui::Text("RF Simulator %s (%s) | %.3f ms/frame (%.1f FPS)", APP_VERSION,
                APP_GIT_HASH, 1000.0f / io.Framerate, io.Framerate);

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
    m_state.saveBool("WindowState", "Log", m_show_log);
    m_state.saveBool("WindowState", "SpectrumAnalyzer", m_show_spectrum);
    m_state.saveBool("WindowState", "Properties", m_show_properties);
    for (size_t i = 0; i < m_show_iq_pfbs.size(); ++i) {
        std::string key = "IQPlot_" + std::to_string(m_pfbs[i]->id());
        m_state.saveBool("WindowState", key.c_str(), m_show_iq_pfbs[i]);
    }
    m_state.saveBool("WindowState", "NodeEditor", m_show_node_editor);
}
