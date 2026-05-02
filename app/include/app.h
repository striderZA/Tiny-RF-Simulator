#pragma once

#include "adc_engine.h"
#include "amplifier_engine.h"
#include "inspector_panel.h"
#include "logging_widget.h"
#include "mixer_engine.h"
#include "node_graph_engine.h"
#include "node_graph_widget.h"
#include "s_parameter_amplifier_engine.h"
#include "s_parameter_filter_engine.h"
#include "signal_generator_engine.h"
#include "signal_generator_widget.h"
#include "spectrum_analyzer_engine.h"
#include "spectrum_analyzer_widget.h"
#include "splitter_engine.h"
#include "view_manager.h"
#include <memory>
#include <vector>

class RfSimulatorApp {
  public:
    RfSimulatorApp();
    void draw_ui();
    void update_dsp();

    LoggingWidget m_log_widget;
    bool m_show_log = true;

  private:
    NodeGraphEngine m_graph_engine;
    ViewManager m_view_manager;
    SpectrumAnalyzerEngine m_spectrum_engine;
    std::unique_ptr<SpectrumAnalyzerWidget> m_spectrum_widget;
    std::unique_ptr<NodeGraphWidget> m_graph_widget;

    std::vector<std::unique_ptr<SignalGeneratorEngine>> m_generators;
    std::vector<std::unique_ptr<SignalGeneratorWidget>> m_generator_widgets;
    std::vector<std::unique_ptr<AmplifierEngine>> m_amplifiers;
    std::vector<std::unique_ptr<SplitterEngine>> m_splitters;
    std::vector<std::unique_ptr<MixerEngine>> m_mixers;
    std::vector<std::unique_ptr<SParameterAmplifierEngine>> m_sparam_amps;
    std::vector<std::unique_ptr<SParameterFilterEngine>> m_sparam_filters;
    std::vector<std::unique_ptr<AdcEngine>> m_adcs;
    std::unique_ptr<InspectorPanel> m_inspector_panel;

    void addGenerator();
    void addAmplifier();
    void addSplitter();
    void addMixer();
    void addSParamAmp();
    void addSParamFilter();
    void addAdc();
    void removeComponent(int graph_node_id);
};
