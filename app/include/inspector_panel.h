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
class IdealFilterEngine;
class SParameterAmplifierEngine;
class PFBChannelizerEngine;
class SParameterFilterEngine;

struct ViewToggles {
    bool* log = nullptr;
    bool* spectrum = nullptr;
    bool* properties = nullptr;
    bool* iq_plot = nullptr;
    bool* node_editor = nullptr;
};

class InspectorPanel {
public:
    InspectorPanel(
        NodeGraphEngine& graph,
        std::vector<std::unique_ptr<AmplifierEngine>>& amps,
        std::vector<std::unique_ptr<MixerEngine>>& mixers,
        std::vector<std::unique_ptr<SplitterEngine>>& splitters,
        std::vector<std::unique_ptr<SParameterAmplifierEngine>>& sparam_amps,
        std::vector<std::unique_ptr<SParameterFilterEngine>>& sparam_filters,
        std::vector<std::unique_ptr<AdcEngine>>& adcs,
        std::vector<std::unique_ptr<SignalGeneratorEngine>>& generators,
        std::vector<std::unique_ptr<IdealFilterEngine>>& ideal_filters
    );

    void draw(const char* title, bool* p_open = nullptr);
    void setPFBs(const std::vector<PFBChannelizerEngine*>& pfbs) {
        m_pfb_ptrs = pfbs;
        if (m_selected_pfb_index >= static_cast<int>(m_pfb_ptrs.size()))
            m_selected_pfb_index = std::max(0, static_cast<int>(m_pfb_ptrs.size()) - 1);
    }

    std::function<void(int graph_node_id)> onRemoveNode;
    void setViewToggles(const ViewToggles& t) { m_viewToggles = t; }

private:
    NodeGraphEngine& m_graph;
    std::vector<std::unique_ptr<AmplifierEngine>>& m_amplifiers;
    std::vector<std::unique_ptr<MixerEngine>>& m_mixers;
    std::vector<std::unique_ptr<SplitterEngine>>& m_splitters;
    std::vector<std::unique_ptr<SParameterAmplifierEngine>>& m_sparam_amps;
    std::vector<std::unique_ptr<SParameterFilterEngine>>& m_sparam_filters;
    std::vector<std::unique_ptr<AdcEngine>>& m_adcs;
    std::vector<std::unique_ptr<SignalGeneratorEngine>>& m_generators;
    std::vector<std::unique_ptr<IdealFilterEngine>>& m_ideal_filters;
    std::vector<PFBChannelizerEngine*> m_pfb_ptrs;
    int m_selected_pfb_index = 0;
    ViewToggles m_viewToggles;

    enum class ComponentType { None, Generator, Amplifier, Splitter, Mixer, SParamAmp, SParamFilter, Adc, PFB, IdealFilter };
    struct Hit { ComponentType type; int index; };
    Hit findSelected() const;

    void drawAmplifierProperties(AmplifierEngine& engine, int index);
    void drawMixerProperties(MixerEngine& engine, int index);
    void drawSplitterProperties(SplitterEngine& engine, int index);
    void drawSParamAmpProperties(SParameterAmplifierEngine& engine, int index);
    void drawSParamFilterProperties(SParameterFilterEngine& engine, int index);
    void drawAdcProperties(AdcEngine& engine, int index);
    void drawGeneratorProperties(SignalGeneratorEngine& engine, int index);
    void drawPFBProperties(PFBChannelizerEngine& engine);
    void drawIdealFilterProperties(IdealFilterEngine& engine, int index);
};
