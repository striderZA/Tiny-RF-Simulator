#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

enum class DigitizerCurveKind { Gain = 0, NoiseFigure = 1 };

struct AxisReferencePoint {
    double pixel = 0.0;
    double value = 0.0;
};

struct AxisCalibration {
    std::pair<AxisReferencePoint, AxisReferencePoint> x_refs;
    std::pair<AxisReferencePoint, AxisReferencePoint> y_refs;
    bool x_log = false;
};

class AmplifierDigitizerModel {
  public:
    bool setImagePath(const std::filesystem::path &path);
    void setAxisMode(DigitizerCurveKind kind, bool x_log);
    bool setCalibration(DigitizerCurveKind kind,
                        std::pair<AxisReferencePoint, AxisReferencePoint> x_refs,
                        std::pair<AxisReferencePoint, AxisReferencePoint> y_refs);
    bool addPoint(DigitizerCurveKind kind, double pixel_x, double pixel_y);
    std::vector<std::pair<double, double>> curve(DigitizerCurveKind kind) const;

    void setPartNumber(const std::string &part_number) { m_part_number = part_number; }
    void setManufacturer(const std::string &manufacturer) { m_manufacturer = manufacturer; }
    const std::string &partNumber() const { return m_part_number; }
    const std::string &manufacturer() const { return m_manufacturer; }

  private:
    struct CurveState {
        std::optional<AxisCalibration> calibration;
        std::vector<std::pair<double, double>> points;
    };

    CurveState &state(DigitizerCurveKind kind);
    const CurveState &state(DigitizerCurveKind kind) const;
    static double mapX(const AxisCalibration &calibration, double pixel_x);
    static double mapY(const AxisCalibration &calibration, double pixel_y);

    std::filesystem::path m_image_path;
    CurveState m_gain;
    CurveState m_nf;
    std::string m_part_number;
    std::string m_manufacturer;
};
