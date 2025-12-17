#pragma once

#include "amplifier_engine.h"
#include "amplifier_widget.h"
#include "logging_widget.h"
#include "signal_generator_engine.h"
#include "signal_generator_widget.h"
#include "spectrum_analyzer_engine.h"
#include "spectrum_analyzer_widget.h"
#include "view_manager.h"
#include <memory>
#include <vector>

enum class InputSignals : int { G0 = 0, G1, G2, G3, COUNT };

class RfSimulatorApp {
  public:
    RfSimulatorApp();
    void draw_ui();
    void update_dsp();

    LoggingWidget m_log_widget;
    bool m_show_log = true;

  private:
    ViewManager m_view_manager;
    SpectrumAnalyzerEngine m_spectrum_engine;
    std::unique_ptr<SpectrumAnalyzerWidget> m_spectrum_widget;
    std::vector<std::unique_ptr<SignalGeneratorEngine>> m_generators;
    std::vector<std::unique_ptr<SignalGeneratorWidget>> m_generator_widgets;
    std::vector<std::unique_ptr<AmplifierEngine>> m_amplifiers;
    std::vector<std::unique_ptr<AmplifierWidget>> m_amplifier_widgets;
};
