#pragma once

#include "amplifier_engine.h"
#include "amplifier_widget.h"
#include "logging_widget.h"
#include "signal_generator_engine.h"
#include "signal_generator_widget.h"
#include "spectrum_analyzer_engine.h"
#include "spectrum_analyzer_widget.h"
#include "view_manager.h"
#include "node_graph_engine.h"
#include <memory>
#include <vector>

class RfSimulatorApp {
  public:
    RfSimulatorApp();
    void draw_ui();
    void update_dsp();
    void addAmplifier();
    void removeAmplifier(size_t index);

    LoggingWidget m_log_widget;
    bool m_show_log = true;

  private:
    ViewManager m_view_manager;
    NodeGraphEngine m_node_graph;
    SpectrumAnalyzerEngine m_spectrum_engine;
    std::unique_ptr<SpectrumAnalyzerWidget> m_spectrum_widget;
    std::unique_ptr<SignalGeneratorEngine> m_generator;
    std::unique_ptr<SignalGeneratorWidget> m_generator_widget;
    std::vector<std::unique_ptr<AmplifierEngine>> m_amplifiers;
    std::vector<std::unique_ptr<AmplifierWidget>> m_amplifier_widgets;
    void draw_signal_chain(const char *title);
};
