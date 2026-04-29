#include "app.h"
#include "imgui.h"
#include "logging_core.h"
#include "logging_widget.h"

RfSimulatorApp::RfSimulatorApp() {
    m_graph_widget = std::make_unique<NodeGraphWidget>(m_graph_engine);
    m_graph_widget->onAddGenerator = [this]() { addGenerator(); };
    m_graph_widget->onAddAmplifier = [this]() { addAmplifier(); };
    m_graph_widget->onRemoveNode = [this](int id) { removeComponent(id); };

    addGenerator();
    m_generators[0]->addTone(100e6, -20.0);

    addAmplifier();

    m_amplifier_widget = std::make_unique<AmplifierWidget>(m_amplifiers);
    m_amplifier_widget->onAddAmplifier = [this]() { addAmplifier(); };
    m_amplifier_widget->onRemoveAmplifier = [this](size_t index) {
        if (index >= m_amplifiers.size()) return;
        m_view_manager.unregisterNode(&m_amplifiers[index]->node());
        m_graph_engine.removeNode(m_amplifiers[index]->graphNodeId());
        m_amplifiers.erase(m_amplifiers.begin() + static_cast<std::ptrdiff_t>(index));
        LOG_INFO("Removed amplifier at index %zu", index);
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
}

void RfSimulatorApp::update_dsp() {
    for (auto& gen : m_generators) {
        gen->update(0.0);
    }

    for (auto& amp : m_amplifiers) {
        auto* source = m_graph_engine.getSourceForInput(amp->inputPinId());
        if (source) {
            amp->node().input = source->output;
        } else {
            amp->node().input = Spectrum();
        }
        amp->update(0.0);
    }

    // Update spectrum view based on active probe
    SignalNode* probed = m_graph_engine.probedSignalNode();
    if (probed) {
        std::string label;
        for (const auto& node : m_graph_engine.nodes()) {
            if (node.signal_node == probed) {
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
            node->view_enabled = (node == probed);
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

    if (m_amplifier_widget) {
        m_amplifier_widget->draw("Amplifiers");
    }

    if (m_show_log)
        m_log_widget.draw("Log", &m_show_log);
}
