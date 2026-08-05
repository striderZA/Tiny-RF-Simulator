#pragma once

#include "amplifier_digitizer_model.h"
#include "image_texture.h"

class AmplifierDigitizerWidget {
  public:
    bool draw(bool *open);
    AmplifierDigitizerModel &model() { return m_model; }
    const AmplifierDigitizerModel &model() const { return m_model; }
    bool exportRequested() const { return m_export_requested; }
    void clearExportRequest() { m_export_requested = false; }
    void requestExportForTest() { m_export_requested = true; }

  private:
    AmplifierDigitizerModel m_model;
    ImageTexture m_image;
    bool m_export_requested = false;
    int m_step = 0;
};
