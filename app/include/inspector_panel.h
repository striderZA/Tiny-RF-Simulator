#pragma once

#include "node_graph_engine.h"
#include "signal_node.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

class AdcEngine;
class AmplifierEngine;
class MixerEngine;
class SignalGeneratorEngine;
class SplitterEngine;
class SParameterAmplifierEngine;

class InspectorPanel {
public:
    InspectorPanel(
        NodeGraphEngine& graph,
        std::vector<std::unique_ptr<AmplifierEngine>>& amps,
        std::vector<std::unique_ptr<MixerEngine>>& mixers,
        std::vector<std::unique_ptr<SplitterEngine>>& splitters,
        std::vector<std::unique_ptr<SParameterAmplifierEngine>>& sparam_amps,
        std::vector<std::unique_ptr<AdcEngine>>& adcs,
        std::vector<std::unique_ptr<SignalGeneratorEngine>>& generators
    );

    void draw(const char* title, bool* p_open = nullptr);

    std::function<void(int graph_node_id)> onRemoveNode;

private:
    NodeGraphEngine& m_graph;
    std::vector<std::unique_ptr<AmplifierEngine>>& m_amplifiers;
    std::vector<std::unique_ptr<MixerEngine>>& m_mixers;
    std::vector<std::unique_ptr<SplitterEngine>>& m_splitters;
    std::vector<std::unique_ptr<SParameterAmplifierEngine>>& m_sparam_amps;
    std::vector<std::unique_ptr<AdcEngine>>& m_adcs;
    std::vector<std::unique_ptr<SignalGeneratorEngine>>& m_generators;

    enum class ComponentType { None, Generator, Amplifier, Splitter, Mixer, SParamAmp, Adc };
    struct Hit { ComponentType type; int index; };
    Hit findSelected() const;

    void drawAmplifierProperties(AmplifierEngine& engine, int index);
    void drawMixerProperties(MixerEngine& engine, int index);
    void drawSplitterProperties(SplitterEngine& engine, int index);
    void drawSParamAmpProperties(SParameterAmplifierEngine& engine, int index);
    void drawAdcProperties(AdcEngine& engine, int index);
    void drawGeneratorProperties(SignalGeneratorEngine& engine, int index);
};
