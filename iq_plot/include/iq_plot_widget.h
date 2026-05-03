#pragma once

#include "pfb_channelizer_engine.h"
#include <vector>
#include <complex>

class IQPlotWidget {
  public:
    explicit IQPlotWidget(PFBChannelizerEngine& engine) : m_pfb(engine) {}

    void draw(const char* title, bool* p_open = nullptr);

  private:
    PFBChannelizerEngine& m_pfb;
    std::vector<double> m_time_us;
    std::vector<double> m_i_samples;
    std::vector<double> m_q_samples;
};
