#include "spectrum_analyzer_harmonics.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

// Harmonic measurement rules. The model is UI-free: it reads the same jitter-free
// readout trace/axis as Peak and the markers, names the fundamental (strongest tone,
// or the marker frequency the widget supplies), and searches for the 2nd and 3rd
// harmonics within one resolution bandwidth. test_ui drives the panel.

namespace {

constexpr double kMHz = 1.0e6;

std::vector<double> uniformAxis(double start_Hz, double stop_Hz, double step_Hz) {
    std::vector<double> axis;
    for (double f = start_Hz; f <= stop_Hz + 0.5 * step_Hz; f += step_Hz)
        axis.push_back(f);
    return axis;
}

// A noise floor with a deterministic +-0.3 dB ripple so a peak search sees a floor,
// never a false clearance of the 6 dB peak threshold.
std::vector<double> floorTrace(const std::vector<double> &axis, double floor_dBm = -120.0) {
    std::vector<double> trace(axis.size(), floor_dBm);
    for (size_t i = 0; i < trace.size(); ++i)
        trace[i] = floor_dBm + 0.3 * static_cast<double>(static_cast<int>(i % 3) - 1);
    return trace;
}

// Puts a tone on the bin nearest `freq_Hz`; the uniform test grids put it on an exact bin.
void addTone(std::vector<double> &trace, const std::vector<double> &axis, double freq_Hz,
             double level_dBm) {
    size_t best = 0;
    for (size_t i = 0; i < axis.size(); ++i) {
        if (std::abs(axis[i] - freq_Hz) < std::abs(axis[best] - freq_Hz))
            best = i;
    }
    trace[best] = level_dBm;
}

} // namespace

TEST_CASE("harmonic measurement starts hidden and on the strongest tone") {
    HarmonicModel model;
    REQUIRE_FALSE(model.visible());
    REQUIRE_FALSE(model.useMarker());
    model.setVisible(true);
    model.setUseMarker(true);
    REQUIRE(model.visible());
    REQUIRE(model.useMarker());
}

TEST_CASE("a clear fundamental reports the 2nd and 3rd harmonics in dBc") {
    const auto axis = uniformAxis(0.0, 60.0 * kMHz, 1.0 * kMHz);
    auto trace = floorTrace(axis);
    addTone(trace, axis, 10.0 * kMHz, -10.0);
    addTone(trace, axis, 20.0 * kMHz, -40.0);
    addTone(trace, axis, 30.0 * kMHz, -50.0);

    HarmonicModel model;
    model.measure(axis, trace, 0.0, 60.0 * kMHz, 1.0 * kMHz);
    const auto &result = model.result();

    REQUIRE(result.fundamental_status == HarmonicModel::Status::Measured);
    REQUIRE(result.fundamental_Hz == Catch::Approx(10.0 * kMHz));
    REQUIRE(result.fundamental_dBm == Catch::Approx(-10.0));

    const auto &second = result.harmonics[0];
    REQUIRE(second.order == 2);
    REQUIRE(second.status == HarmonicModel::Status::Measured);
    REQUIRE(second.expected_Hz == Catch::Approx(20.0 * kMHz));
    REQUIRE(second.frequency_Hz == Catch::Approx(20.0 * kMHz));
    REQUIRE(second.amplitude_dBm == Catch::Approx(-40.0));
    REQUIRE(second.relative_dBc == Catch::Approx(-30.0));

    const auto &third = result.harmonics[1];
    REQUIRE(third.order == 3);
    REQUIRE(third.status == HarmonicModel::Status::Measured);
    REQUIRE(third.frequency_Hz == Catch::Approx(30.0 * kMHz));
    REQUIRE(third.amplitude_dBm == Catch::Approx(-50.0));
    REQUIRE(third.relative_dBc == Catch::Approx(-40.0));
}

TEST_CASE("a harmonic with no peak in its window is reported missing") {
    const auto axis = uniformAxis(0.0, 60.0 * kMHz, 1.0 * kMHz);
    auto trace = floorTrace(axis);
    addTone(trace, axis, 10.0 * kMHz, -10.0);

    HarmonicModel model;
    model.measure(axis, trace, 0.0, 60.0 * kMHz, 1.0 * kMHz);
    const auto &result = model.result();

    REQUIRE(result.fundamental_status == HarmonicModel::Status::Measured);
    for (const auto &entry : result.harmonics) {
        REQUIRE(entry.status == HarmonicModel::Status::NotFound);
        REQUIRE(entry.expected_Hz == Catch::Approx(static_cast<double>(entry.order) * 10.0 * kMHz));
        REQUIRE(entry.relative_dBc == Catch::Approx(0.0));
    }
}

TEST_CASE("a harmonic outside the swept span is reported out of span, not guessed") {
    const auto axis = uniformAxis(0.0, 60.0 * kMHz, 1.0 * kMHz);
    auto trace = floorTrace(axis);
    addTone(trace, axis, 10.0 * kMHz, -10.0);
    addTone(trace, axis, 20.0 * kMHz, -40.0); // present on the grid, but outside the span

    HarmonicModel model;
    model.measure(axis, trace, 5.0 * kMHz, 15.0 * kMHz, 1.0 * kMHz);
    const auto &result = model.result();

    // The fundamental search also stays inside the shown span.
    REQUIRE(result.fundamental_status == HarmonicModel::Status::Measured);
    REQUIRE(result.fundamental_Hz == Catch::Approx(10.0 * kMHz));
    for (const auto &entry : result.harmonics) {
        REQUIRE(entry.status == HarmonicModel::Status::OutOfSpan);
        REQUIRE(entry.expected_Hz == Catch::Approx(static_cast<double>(entry.order) * 10.0 * kMHz));
        REQUIRE(entry.relative_dBc == Catch::Approx(0.0));
    }
}

TEST_CASE("a noise-only trace has no clear fundamental") {
    const auto axis = uniformAxis(0.0, 60.0 * kMHz, 1.0 * kMHz);
    const auto trace = floorTrace(axis);

    HarmonicModel model;
    model.measure(axis, trace, 0.0, 60.0 * kMHz, 1.0 * kMHz);
    const auto &result = model.result();

    REQUIRE(result.fundamental_status == HarmonicModel::Status::NoFundamental);
    for (const auto &entry : result.harmonics)
        REQUIRE(entry.status == HarmonicModel::Status::NoFundamental);
}
