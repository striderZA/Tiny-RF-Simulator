#pragma once
#include "pfb_channelizer_engine.h"
#include "spectrum.h"
#include <vector>

class ImDrawList;
struct ImRect;

class PFBChannelizerWidget {
  public:
    explicit PFBChannelizerWidget(PFBChannelizerEngine &engine);

    void draw(const char *title, bool *p_open = nullptr);

  private:
    PFBChannelizerEngine &m_engine;
    int m_grid_offset = 0;
    uint64_t m_cached_gen = 0;
    const Spectrum *m_cached_input = nullptr;

    struct CellCache {
        std::vector<double> freqs;
        std::vector<double> power_dBm;
        std::vector<Spectrum::Tone> tones;
    };
    std::vector<CellCache> m_cells;
    double m_y_min = -120.0;
    double m_y_max = 20.0;

    void rebuildCache();
    void drawCell(ImDrawList *dl, const ImRect &rect, int ch_idx);
};
