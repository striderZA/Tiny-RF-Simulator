#include "app.h"
#include "imgui.h"
#include "logging_core.h"
#include "logging_widget.h"

RfSimulatorApp::RfSimulatorApp() {
    m_graph_widget = std::make_unique<NodeGraphWidget>(m_graph_engine);
    m_graph_widget->onAddGenerator = [this]() { addGenerator(); };
    m_graph_widget->onAddAmplifier = [this]() { addAmplifier(); };
    m_graph_widget->onAddSplitter = [this]() { addSplitter(); };
    m_graph_widget->onAddMixer = [this]() { addMixer(); };
    m_graph_widget->onAddSParamAmp = [this]() { addSParamAmp(); };
    m_graph_widget->onAddAdc = [this]() { addAdc(); };
    m_graph_widget->onRemoveNode = [this](int id) { removeComponent(id); };

    addGenerator();
    m_generators[0]->addTone(100e6, -20.0);

    addAmplifier();

    m_inspector_panel = std::make_unique<InspectorPanel>(
        m_graph_engine, m_amplifiers, m_mixers, m_splitters,
        m_sparam_amps, m_adcs, m_generators
    );
    m_inspector_panel->onRemoveNode = [this](int graph_node_id) {
        removeComponent(graph_node_id);
    };

    m_graph_widget->onNodeHover = [this](int graph_node_id) -> std::string {
        for (auto& g : m_generators) if (g->graphNodeId() == graph_node_id) return g->hoverSummary();
        for (auto& a : m_amplifiers) if (a->graphNodeId() == graph_node_id) return a->hoverSummary();
        for (auto& s : m_splitters) if (s->graphNodeId() == graph_node_id) return s->hoverSummary();
        for (auto& m : m_mixers) if (m->graphNodeId() == graph_node_id) return m->hoverSummary();
        for (auto& s : m_sparam_amps) if (s->graphNodeId() == graph_node_id) return s->hoverSummary();
        for (auto& a : m_adcs) if (a->graphNodeId() == graph_node_id) return a->hoverSummary();
        return "";
    };

    m_spectrum_widget = std::make_unique<SpectrumAnalyzerWidget>(m_spectrum_engine, m_view_manager);
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

void RfSimulatorApp::addAdc() {
    int id = static_cast<int>(m_adcs.size());
    auto adc = std::make_unique<AdcEngine>(id, m_graph_engine);
    m_view_manager.registerNode(&adc->node());
    m_adcs.push_back(std::move(adc));
    LOG_INFO("Added ADC %d", id);
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
}

void RfSimulatorApp::update_dsp() {
    auto wireAndUpdate = [&](auto& components) {
        for (auto& comp : components) {
            auto* source = m_graph_engine.getSourceForInput(comp->inputPinId());
            if (source)
                comp->node().inputs[0] = source->outputs[0];
            else
                comp->node().inputs[0] = Spectrum();
            comp->update(0.0);
        }
    };

    for (auto& gen : m_generators)
        gen->update(0.0);

    wireAndUpdate(m_amplifiers);
    wireAndUpdate(m_splitters);
    wireAndUpdate(m_mixers);
    wireAndUpdate(m_sparam_amps);
    wireAndUpdate(m_adcs);

    // Update spectrum view based on first active probe
    auto probed_nodes = m_graph_engine.probedSignalNodes();
    SignalNode* primary_probe = probed_nodes.empty() ? nullptr : probed_nodes[0];
    if (primary_probe) {
        std::string label;
        for (const auto& node : m_graph_engine.nodes()) {
            if (node.signal_node == primary_probe) {
                label = node.label + " OUT";
                break;
            }
        }
        m_spectrum_widget->setProbeLabel(label);
    } else {
        m_spectrum_widget->setProbeLabel("");
    }

    for (auto* node : m_view_manager.nodes()) {
        if (node) {
            node->view_enabled = (node == primary_probe);
        }
    }
}

void RfSimulatorApp::draw_ui() {
    ImGuiIO &io = ImGui::GetIO();
    (void)io;
    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate,
                io.Framerate);

    m_graph_widget->draw("Node Editor");
    m_spectrum_widget->draw("Spectrum Analyzer");

    for (size_t i = 0; i < m_generator_widgets.size(); ++i) {
        m_generator_widgets[i]->draw("Generators");
    }

    if (m_inspector_panel) m_inspector_panel->draw("Properties");

    if (m_show_log)
        m_log_widget.draw("Log", &m_show_log);
}
