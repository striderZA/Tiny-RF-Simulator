#pragma once

#include "component_interface.h"
#include "node_graph_engine.h"
#include "signal_node.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

class AdcEngine;
class AmplifierEngine;
class CoaxCableEngine;
class MixerEngine;
class SignalGeneratorEngine;
class SplitterEngine;
class IdealFilterEngine;
class SParamEngine;
class PFBChannelizerEngine;
class ComponentRegistry;

struct ViewToggles {
    bool* log = nullptr;
    bool* spectrum = nullptr;
    bool* properties = nullptr;
    bool* iq_plot = nullptr;
    bool* node_editor = nullptr;
};

class InspectorPanel {
public:
    explicit InspectorPanel(NodeGraphEngine& graph, ComponentRegistry& components);

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
    ComponentRegistry* m_components = nullptr;
    std::vector<PFBChannelizerEngine*> m_pfb_ptrs;
    int m_selected_pfb_index = 0;
    ViewToggles m_viewToggles;

    enum class ComponentType { None, Generator, Amplifier, Splitter, Mixer, SParam, Adc, PFB, IdealFilter, CoaxCable };
    struct Hit { ComponentType type; IComponentEngine* engine = nullptr; };
    Hit findSelected() const;
    std::string labelForHit(const Hit& hit) const;

    void drawAmplifierProperties(AmplifierEngine& engine, int index);
    void drawCoaxCableProperties(CoaxCableEngine& engine, int index);
    void drawMixerProperties(MixerEngine& engine, int index);
    void drawSplitterProperties(SplitterEngine& engine, int index);
    void drawSParamProperties(SParamEngine& engine, int index);
    void drawAdcProperties(AdcEngine& engine, int index);
    void drawGeneratorProperties(SignalGeneratorEngine& engine, int index);
    void drawPFBProperties(PFBChannelizerEngine& engine);
    void drawIdealFilterProperties(IdealFilterEngine& engine, int index);
};
