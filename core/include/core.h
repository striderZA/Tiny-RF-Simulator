#pragma once

#include <functional>
#include <memory>

class RfSimulatorCore {
  public:
    RfSimulatorCore();
    ~RfSimulatorCore();
    // onGui draws each frame. beforeFrame runs between frames, before ImGui::NewFrame, so a
    // caller can rebuild the font atlas there. Pass {} for none.
    void Run(const std::function<void()> &onGui, const std::function<void()> &beforeFrame = {});

  private:
    bool Initialize();
    void Shutdown();
    void MainLoop(const std::function<void()> &onGui, const std::function<void()> &beforeFrame);

    struct Impl;
    std::unique_ptr<Impl> p_impl;
};
