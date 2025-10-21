#include "app.h"
#include "imgui.h"
#include <string>
#include <signal_generator.h>
#include <memory>

RfSimulatorApp::RfSimulatorApp()
    : m_spectrum_analyzer(), m_signal_generators() {
  m_signal_generators.push_back(
      std::make_unique<SignalGenerator>(static_cast<int>(InputSignals::G0)));
}

void RfSimulatorApp::onGui() {
  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
              1000.0f / io.Framerate, io.Framerate);
  m_spectrum_analyzer.update("Spectrum", nullptr);

  for (auto &gen : m_signal_generators) {
    std::string title = "Generator " + std::to_string(gen->id());
    gen->setup(title.c_str(), nullptr);
  }
}
