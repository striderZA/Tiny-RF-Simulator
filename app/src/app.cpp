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

    m_splitter_widget = std::make_unique<SplitterWidget>(m_splitters);
    m_splitter_widget->onAddSplitter = [this]() { addSplitter(); };
    m_splitter_widget->onRemoveSplitter = [this](size_t index) {
        if (index >= m_splitters.size()) return;
        m_view_manager.unregisterNode(&m_splitters[index]->node());
        m_graph_engine.removeNode(m_splitters[index]->graphNodeId());
        m_splitters.erase(m_splitters.begin() + static_cast<std::ptrdiff_t>(index));
        LOG_INFO("Removed splitter at index %zu", index);
    };

    m_mixer_widget = std::make_unique<MixerWidget>(m_mixers);
    m_mixer_widget->onAddMixer = [this]() { addMixer(); };
    m_mixer_widget->onRemoveMixer = [this](size_t index) {
        if (index >= m_mixers.size()) return;
        m_view_manager.unregisterNode(&m_mixers[index]->node());
        m_graph_engine.removeNode(m_mixers[index]->graphNodeId());
        m_mixers.erase(m_mixers.begin() + static_cast<std::ptrdiff_t>(index));
        LOG_INFO("Removed mixer at index %zu", index);
    };

    m_sparam_amp_widget = std::make_unique<SParameterAmplifierWidget>(m_sparam_amps);
    m_sparam_amp_widget->onAddSParamAmp = [this]() { addSParamAmp(); };
    m_sparam_amp_widget->onRemoveSParamAmp = [this](size_t index) {
        if (index >= m_sparam_amps.size()) return;
        m_view_manager.unregisterNode(&m_sparam_amps[index]->node());
        m_graph_engine.removeNode(m_sparam_amps[index]->graphNodeId());
        m_sparam_amps.erase(m_sparam_amps.begin() + static_cast<std::ptrdiff_t>(index));
        LOG_INFO("Removed S-parameter amplifier at index %zu", index);
    };

    m_adc_widget = std::make_unique<AdcWidget>(m_adcs);
    m_adc_widget->onAddAdc = [this]() { addAdc(); };
    m_adc_widget->onRemoveAdc = [this](size_t index) {
        if (index >= m_adcs.size()) return;
        m_view_manager.unregisterNode(&m_adcs[index]->node());
        m_graph_engine.removeNode(m_adcs[index]->graphNodeId());
        m_adcs.erase(m_adcs.begin() + static_cast<std::ptrdiff_t>(index));
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
}

void RfSimulatorApp::update_dsp() {
    for (auto& gen : m_generators) {
        gen->update(0.0);
    }

    for (auto& amp : m_amplifiers) {
        auto* source = m_graph_engine.getSourceForInput(amp->inputPinId());
        if (source) {
            amp->node().inputs[0] = source->outputs[0];
        } else {
            amp->node().inputs[0] = Spectrum();
        }
        amp->update(0.0);
    }

    for (auto& split : m_splitters) {
        auto* source = m_graph_engine.getSourceForInput(split->inputPinId());
        if (source) {
            split->node().inputs[0] = source->outputs[0];
        } else {
            split->node().inputs[0] = Spectrum();
        }
        split->update(0.0);
    }

    for (auto& mix : m_mixers) {
        auto* source = m_graph_engine.getSourceForInput(mix->inputPinId());
        if (source) {
            mix->node().inputs[0] = source->outputs[0];
        } else {
            mix->node().inputs[0] = Spectrum();
        }
        mix->update(0.0);
    }

    for (auto& spamp : m_sparam_amps) {
        auto* source = m_graph_engine.getSourceForInput(spamp->inputPinId());
        if (source) {
            spamp->node().inputs[0] = source->outputs[0];
        } else {
            spamp->node().inputs[0] = Spectrum();
        }
        spamp->update(0.0);
    }

    for (auto& adc : m_adcs) {
        auto* source = m_graph_engine.getSourceForInput(adc->inputPinId());
        if (source)
            adc->node().inputs[0] = source->outputs[0];
        else
            adc->node().inputs[0] = Spectrum();
        adc->update(0.0);
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

    if (m_splitter_widget) {
        m_splitter_widget->draw("Splitters");
    }

    if (m_mixer_widget) {
        m_mixer_widget->draw("Mixers");
    }

    if (m_sparam_amp_widget) {
        m_sparam_amp_widget->draw("S-Param Amplifiers");
    }

    if (m_adc_widget)
        m_adc_widget->draw("RF ADC");

    if (m_show_log)
        m_log_widget.draw("Log", &m_show_log);
}
