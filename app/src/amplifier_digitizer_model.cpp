#include "amplifier_digitizer_model.h"
#include <algorithm>
#include <cmath>

namespace {

double lerp(double a, double b, double t) { return a + (b - a) * t; }

double inverseLerp(double a, double b, double value) {
    if (std::abs(b - a) < 1e-12)
        return 0.0;
    return (value - a) / (b - a);
}

} // namespace

bool AmplifierDigitizerModel::setImagePath(const std::filesystem::path &path) {
    m_image_path = path;
    return true;
}

void AmplifierDigitizerModel::setAxisMode(DigitizerCurveKind kind, bool x_log) {
    auto &curve = state(kind);
    if (!curve.calibration)
        curve.calibration = AxisCalibration{};
    curve.calibration->x_log = x_log;
}

bool AmplifierDigitizerModel::setCalibration(
    DigitizerCurveKind kind, std::pair<AxisReferencePoint, AxisReferencePoint> x_refs,
    std::pair<AxisReferencePoint, AxisReferencePoint> y_refs) {
    if (std::abs(x_refs.second.pixel - x_refs.first.pixel) < 1e-12 ||
        std::abs(y_refs.second.pixel - y_refs.first.pixel) < 1e-12) {
        return false;
    }

    auto &curve = state(kind);
    const bool x_log = curve.calibration ? curve.calibration->x_log : false;
    curve.calibration = AxisCalibration{x_refs, y_refs, x_log};
    return true;
}

bool AmplifierDigitizerModel::addPoint(DigitizerCurveKind kind, double pixel_x, double pixel_y) {
    auto &curve = state(kind);
    if (!curve.calibration)
        return false;

    curve.points.emplace_back(mapX(*curve.calibration, pixel_x), mapY(*curve.calibration, pixel_y));
    std::sort(curve.points.begin(), curve.points.end(),
              [](const auto &lhs, const auto &rhs) { return lhs.first < rhs.first; });
    return true;
}

std::vector<std::pair<double, double>>
AmplifierDigitizerModel::curve(DigitizerCurveKind kind) const {
    return state(kind).points;
}

AmplifierDigitizerModel::CurveState &AmplifierDigitizerModel::state(DigitizerCurveKind kind) {
    return kind == DigitizerCurveKind::Gain ? m_gain : m_nf;
}

const AmplifierDigitizerModel::CurveState &
AmplifierDigitizerModel::state(DigitizerCurveKind kind) const {
    return kind == DigitizerCurveKind::Gain ? m_gain : m_nf;
}

double AmplifierDigitizerModel::mapX(const AxisCalibration &calibration, double pixel_x) {
    const double t =
        inverseLerp(calibration.x_refs.first.pixel, calibration.x_refs.second.pixel, pixel_x);
    if (!calibration.x_log)
        return lerp(calibration.x_refs.first.value, calibration.x_refs.second.value, t);

    const double log_a = std::log10(calibration.x_refs.first.value);
    const double log_b = std::log10(calibration.x_refs.second.value);
    return std::pow(10.0, lerp(log_a, log_b, t));
}

double AmplifierDigitizerModel::mapY(const AxisCalibration &calibration, double pixel_y) {
    const double t =
        inverseLerp(calibration.y_refs.first.pixel, calibration.y_refs.second.pixel, pixel_y);
    return lerp(calibration.y_refs.first.value, calibration.y_refs.second.value, t);
}
