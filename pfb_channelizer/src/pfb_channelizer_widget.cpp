#include "pfb_channelizer_widget.h"

PFBChannelizerWidget::PFBChannelizerWidget(PFBChannelizerEngine& engine)
    : m_engine(engine) {}

void PFBChannelizerWidget::draw(const char* title, bool* p_open) {
    (void)title;
    (void)p_open;
}

void PFBChannelizerWidget::rebuildCache() {}

void PFBChannelizerWidget::drawCell(ImDrawList* dl, const ImRect& rect, int ch_idx) {
    (void)dl;
    (void)rect;
    (void)ch_idx;
}
