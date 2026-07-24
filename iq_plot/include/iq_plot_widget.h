#pragma once

#include "pfb_channelizer_engine.h"
#include <deque>

struct ZoomState {
    bool active = false;
    double x_min = 0.0;
    double x_max = 0.0;
};

struct kiss_fft_state;
typedef struct kiss_fft_state *kiss_fft_cfg;

class IQPlotWidget {
  public:
    explicit IQPlotWidget(PFBChannelizerEngine &engine) : m_pfb(engine) {}
    ~IQPlotWidget();

    void draw(const char *title, bool *p_open = nullptr);

  private:
    PFBChannelizerEngine &m_pfb;
    std::deque<double> m_stream_i;
    std::deque<double> m_stream_q;
    double m_time_step_s = 0.0;
    bool m_time_inited = false;

    ZoomState m_zoom;
    bool m_zoom_locked = false;
    double m_zoom_locked_xmin = 0.0;
    double m_zoom_locked_xmax = 0.0;

    // Task 8: Cached kissFFT config
    kiss_fft_cfg m_ifft = nullptr;
    size_t m_ifft_N = 0;

    // Task 10: EMA Y-axis smoothing
    double m_smooth_y_min = 0.0;
    double m_smooth_y_max = 0.0;
    bool m_y_inited = false;
    static constexpr double kYAlpha = 0.15;

    static constexpr size_t kMaxSamples = 4096;

    void runIDFT();
};
