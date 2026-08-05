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

    enum class CaptureTarget {
        None,
        GainX1,
        GainX2,
        GainY1,
        GainY2,
        GainPoint,
        NfX1,
        NfX2,
        NfY1,
        NfY2,
        NfPoint,
    };

    struct CurveUiState {
        double x_value_1 = 100e6;
        double x_value_2 = 200e6;
        double y_value_1 = 10.0;
        double y_value_2 = 20.0;
        double x_pixel_1 = 0.0;
        double x_pixel_2 = 100.0;
        double y_pixel_1 = 100.0;
        double y_pixel_2 = 0.0;
        bool x_log = false;
    };

  private:
    bool canExport() const;

    AmplifierDigitizerModel m_model;
    ImageTexture m_image;
    bool m_export_requested = false;
    int m_step = 0;
    CaptureTarget m_capture_target = CaptureTarget::None;
    CurveUiState m_gain_ui;
    CurveUiState m_nf_ui{100e6, 200e6, 1.0, 3.0, 0.0, 100.0, 100.0, 0.0, false};
    char m_part_number[128] = {};
    char m_manufacturer[128] = {};
    std::string m_status_message;
};
