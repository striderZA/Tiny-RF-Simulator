#pragma once

#include "pfb_channelizer_engine.h"
#include <deque>
#include <vector>
#include <complex>

struct ZoomState {
    bool active = false;
    double x_min = 0.0;
    double x_max = 0.0;
};

class IQPlotWidget {
  public:
    explicit IQPlotWidget(PFBChannelizerEngine& engine) : m_pfb(engine) {}

    void draw(const char* title, bool* p_open = nullptr);

  private:
    PFBChannelizerEngine& m_pfb;
    std::deque<double> m_stream_i;
    std::deque<double> m_stream_q;
    double m_time_step_s = 0.0;
    bool m_time_inited = false;

    ZoomState m_zoom;
    bool m_zoom_locked = false;
    double m_zoom_locked_xmin = 0.0;
    double m_zoom_locked_xmax = 0.0;

    static constexpr size_t kMaxSamples = 4096;

    void runIDFT();
};
