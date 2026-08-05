#include "amplifier_digitizer_widget.h"
#include "imgui.h"
#include <portable-file-dialogs.h>

namespace {

constexpr ImVec2 kImageSize{512.0f, 512.0f};

bool isCaptureForGain(AmplifierDigitizerWidget::CaptureTarget target) {
    return target == AmplifierDigitizerWidget::CaptureTarget::GainX1 ||
           target == AmplifierDigitizerWidget::CaptureTarget::GainX2 ||
           target == AmplifierDigitizerWidget::CaptureTarget::GainY1 ||
           target == AmplifierDigitizerWidget::CaptureTarget::GainY2 ||
           target == AmplifierDigitizerWidget::CaptureTarget::GainPoint;
}

bool isCaptureForNf(AmplifierDigitizerWidget::CaptureTarget target) {
    return target == AmplifierDigitizerWidget::CaptureTarget::NfX1 ||
           target == AmplifierDigitizerWidget::CaptureTarget::NfX2 ||
           target == AmplifierDigitizerWidget::CaptureTarget::NfY1 ||
           target == AmplifierDigitizerWidget::CaptureTarget::NfY2 ||
           target == AmplifierDigitizerWidget::CaptureTarget::NfPoint;
}

void applyCapture(AmplifierDigitizerWidget::CurveUiState &ui,
                  AmplifierDigitizerWidget::CaptureTarget target, double x, double y) {
    switch (target) {
    case AmplifierDigitizerWidget::CaptureTarget::GainX1:
    case AmplifierDigitizerWidget::CaptureTarget::NfX1:
        ui.x_pixel_1 = x;
        break;
    case AmplifierDigitizerWidget::CaptureTarget::GainX2:
    case AmplifierDigitizerWidget::CaptureTarget::NfX2:
        ui.x_pixel_2 = x;
        break;
    case AmplifierDigitizerWidget::CaptureTarget::GainY1:
    case AmplifierDigitizerWidget::CaptureTarget::NfY1:
        ui.y_pixel_1 = y;
        break;
    case AmplifierDigitizerWidget::CaptureTarget::GainY2:
    case AmplifierDigitizerWidget::CaptureTarget::NfY2:
        ui.y_pixel_2 = y;
        break;
    default:
        break;
    }
}

} // namespace

bool AmplifierDigitizerWidget::canExport() const {
    return m_image.loaded() && !m_model.partNumber().empty() && !m_model.manufacturer().empty() &&
           m_model.hasCalibration(DigitizerCurveKind::Gain) &&
           m_model.hasCalibration(DigitizerCurveKind::NoiseFigure) &&
           m_model.curve(DigitizerCurveKind::Gain).size() >= 2 &&
           m_model.curve(DigitizerCurveKind::NoiseFigure).size() >= 2;
}

bool AmplifierDigitizerWidget::draw(bool *open) {
    if (open && !*open)
        return false;

    bool keep_open = true;
    bool *window_open = open ? open : &keep_open;
    if (!ImGui::Begin("Amplifier Datasheet Import", window_open)) {
        ImGui::End();
        return false;
    }

    ImGui::TextUnformatted("Import amplifier data from a datasheet image");
    const char *step_label = m_step == 0   ? "Source"
                             : m_step == 1 ? "Gain"
                             : m_step == 2 ? "Noise Figure"
                                           : "Review";
    ImGui::Text("Step %d/4: %s", m_step + 1, step_label);
    ImGui::Separator();

    if (m_step == 0) {
        const std::string current_path =
            m_model.imagePath().empty() ? std::string("(none)") : m_model.imagePath().string();
        ImGui::TextWrapped("Image: %s", current_path.c_str());
        if (ImGui::Button("Choose Image...")) {
            auto picked =
                pfd::open_file("Choose Datasheet Image", ".", {"Image Files", "*.png *.jpg *.jpeg"})
                    .result();
            if (!picked.empty()) {
                std::string error;
                if (m_image.load(picked.front(), error)) {
                    m_model.setImagePath(picked.front());
                    m_status_message = "Loaded image: " + picked.front();
                } else {
                    m_status_message = "Image load failed: " + error;
                }
            }
        }
        if (m_image.loaded())
            ImGui::Image(m_image.id(), kImageSize);
    } else {
        const bool editing_gain = (m_step == 1);
        const DigitizerCurveKind kind =
            editing_gain ? DigitizerCurveKind::Gain : DigitizerCurveKind::NoiseFigure;
        CurveUiState &ui = editing_gain ? m_gain_ui : m_nf_ui;

        ImGui::Checkbox("Log frequency X axis", &ui.x_log);
        m_model.setAxisMode(kind, ui.x_log);
        ImGui::InputDouble("X1 value", &ui.x_value_1);
        ImGui::SameLine();
        if (ImGui::Button("Capture X1"))
            m_capture_target = editing_gain ? CaptureTarget::GainX1 : CaptureTarget::NfX1;
        ImGui::InputDouble("X2 value", &ui.x_value_2);
        ImGui::SameLine();
        if (ImGui::Button("Capture X2"))
            m_capture_target = editing_gain ? CaptureTarget::GainX2 : CaptureTarget::NfX2;
        ImGui::InputDouble("Y1 value", &ui.y_value_1);
        ImGui::SameLine();
        if (ImGui::Button("Capture Y1"))
            m_capture_target = editing_gain ? CaptureTarget::GainY1 : CaptureTarget::NfY1;
        ImGui::InputDouble("Y2 value", &ui.y_value_2);
        ImGui::SameLine();
        if (ImGui::Button("Capture Y2"))
            m_capture_target = editing_gain ? CaptureTarget::GainY2 : CaptureTarget::NfY2;

        ImGui::Text("Captured pixels: X1=%.1f X2=%.1f Y1=%.1f Y2=%.1f", ui.x_pixel_1, ui.x_pixel_2,
                    ui.y_pixel_1, ui.y_pixel_2);
        if (ImGui::Button("Apply Calibration")) {
            if (m_model.setCalibration(
                    kind, {{ui.x_pixel_1, ui.x_value_1}, {ui.x_pixel_2, ui.x_value_2}},
                    {{ui.y_pixel_1, ui.y_value_1}, {ui.y_pixel_2, ui.y_value_2}})) {
                m_status_message =
                    std::string(editing_gain ? "Gain" : "NF") + " calibration applied";
            } else {
                m_status_message =
                    "Calibration failed: capture distinct X and Y pixel references first";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Capture Point"))
            m_capture_target = editing_gain ? CaptureTarget::GainPoint : CaptureTarget::NfPoint;
        ImGui::SameLine();
        if (ImGui::Button("Clear Points"))
            m_model.clearCurve(kind);

        if (m_image.loaded()) {
            ImGui::Image(m_image.id(), kImageSize);
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                const ImVec2 min = ImGui::GetItemRectMin();
                const ImVec2 mouse = ImGui::GetMousePos();
                const double pixel_x = mouse.x - min.x;
                const double pixel_y = mouse.y - min.y;

                if ((editing_gain && isCaptureForGain(m_capture_target)) ||
                    (!editing_gain && isCaptureForNf(m_capture_target))) {
                    if (m_capture_target == CaptureTarget::GainPoint ||
                        m_capture_target == CaptureTarget::NfPoint) {
                        if (m_model.addPoint(kind, pixel_x, pixel_y)) {
                            m_status_message =
                                std::string(editing_gain ? "Gain" : "NF") + " point captured";
                        } else {
                            m_status_message = "Point capture failed: apply calibration first";
                        }
                    } else {
                        applyCapture(ui, m_capture_target, pixel_x, pixel_y);
                        m_status_message = "Reference captured from image";
                    }
                    m_capture_target = CaptureTarget::None;
                }
            }
        } else {
            ImGui::TextDisabled("Load an image first to capture calibration and curve points.");
        }

        const auto points = m_model.curve(kind);
        ImGui::Text("Captured points: %d", static_cast<int>(points.size()));
        for (const auto &[freq_Hz, value] : points)
            ImGui::BulletText("%.6g Hz -> %.3f dB", freq_Hz, value);
    }

    if (m_step == 3) {
        ImGui::InputText("Part Number", m_part_number, sizeof(m_part_number));
        ImGui::InputText("Manufacturer", m_manufacturer, sizeof(m_manufacturer));
        m_model.setPartNumber(m_part_number);
        m_model.setManufacturer(m_manufacturer);

        ImGui::Separator();
        ImGui::Text("Gain points: %d",
                    static_cast<int>(m_model.curve(DigitizerCurveKind::Gain).size()));
        ImGui::Text("NF points: %d",
                    static_cast<int>(m_model.curve(DigitizerCurveKind::NoiseFigure).size()));

        if (ImGui::Button("Export")) {
            if (canExport()) {
                m_export_requested = true;
                m_status_message = "Export requested";
            } else {
                m_status_message = "Export blocked: load an image, calibrate both curves, capture "
                                   ">=2 points per curve, and fill in part number/manufacturer";
            }
        }
    }

    ImGui::Separator();
    if (ImGui::Button("Previous") && m_step > 0)
        --m_step;
    ImGui::SameLine();
    if (ImGui::Button("Next") && m_step < 3)
        ++m_step;

    if (!m_status_message.empty())
        ImGui::TextWrapped("%s", m_status_message.c_str());

    ImGui::End();
    return true;
}
