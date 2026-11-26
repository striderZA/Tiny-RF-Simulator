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

TEST_CASE("a marker frequency becomes the fundamental when supplied") {
    const auto axis = uniformAxis(0.0, 60.0 * kMHz, 1.0 * kMHz);
    auto trace = floorTrace(axis);
    addTone(trace, axis, 10.0 * kMHz, -10.0);
    addTone(trace, axis, 11.0 * kMHz, -30.0);

    HarmonicModel model;

    // Auto detection picks the stronger tone.
    model.measure(axis, trace, 0.0, 60.0 * kMHz, 1.0 * kMHz);
    REQUIRE(model.result().fundamental_Hz == Catch::Approx(10.0 * kMHz));

    // The user-placed marker is taken as the fundamental, level and all.
    model.measure(axis, trace, 0.0, 60.0 * kMHz, 1.0 * kMHz, 11.0 * kMHz);
    const auto &result = model.result();
    REQUIRE(result.fundamental_status == HarmonicModel::Status::Measured);
    REQUIRE(result.fundamental_Hz == Catch::Approx(11.0 * kMHz));
    REQUIRE(result.fundamental_dBm == Catch::Approx(-30.0));
    REQUIRE(result.harmonics[0].expected_Hz == Catch::Approx(22.0 * kMHz));
    REQUIRE(result.harmonics[0].status == HarmonicModel::Status::NotFound);
}

TEST_CASE("a fundamental marker outside the span is reported as such") {
    const auto axis = uniformAxis(0.0, 60.0 * kMHz, 1.0 * kMHz);
    auto trace = floorTrace(axis);
    addTone(trace, axis, 10.0 * kMHz, -10.0);

    HarmonicModel model;
    model.measure(axis, trace, 5.0 * kMHz, 15.0 * kMHz, 1.0 * kMHz, 100.0 * kMHz);
    const auto &result = model.result();

    REQUIRE(result.fundamental_status == HarmonicModel::Status::FundamentalOutOfSpan);
    for (const auto &entry : result.harmonics) {
        REQUIRE(entry.status == HarmonicModel::Status::NoFundamental);
        REQUIRE(entry.relative_dBc == Catch::Approx(0.0));
    }
}

TEST_CASE("an RBW too wide to separate harmonic orders is reported unresolved") {
    const auto axis = uniformAxis(0.0, 60.0 * kMHz, 1.0 * kMHz);
    auto trace = floorTrace(axis);
    addTone(trace, axis, 10.0 * kMHz, -10.0);
    addTone(trace, axis, 20.0 * kMHz, -40.0);
    addTone(trace, axis, 30.0 * kMHz, -50.0);

    HarmonicModel model;
    // A 20 MHz RBW is wider than the 10 MHz fundamental spacing.
    model.measure(axis, trace, 0.0, 60.0 * kMHz, 20.0 * kMHz);
    const auto &result = model.result();

    REQUIRE(result.fundamental_status == HarmonicModel::Status::Measured);
    for (const auto &entry : result.harmonics)
        REQUIRE(entry.status == HarmonicModel::Status::Unresolved);
}

TEST_CASE("the fundamental is the strongest positive-frequency tone") {
    // A real-domain trace mirrors its tones at +-f; the negative image must not win a tie.
    const auto axis = uniformAxis(-30.0 * kMHz, 30.0 * kMHz, 1.0 * kMHz);
    auto trace = floorTrace(axis);
    addTone(trace, axis, -10.0 * kMHz, -10.0);
    addTone(trace, axis, 10.0 * kMHz, -10.0);

    HarmonicModel model;
    model.measure(axis, trace, -30.0 * kMHz, 30.0 * kMHz, 1.0 * kMHz);
    REQUIRE(model.result().fundamental_status == HarmonicModel::Status::Measured);
    REQUIRE(model.result().fundamental_Hz == Catch::Approx(10.0 * kMHz));
}

TEST_CASE("a zero-frequency marker is not a usable fundamental") {
    const auto axis = uniformAxis(0.0, 60.0 * kMHz, 1.0 * kMHz);
    auto trace = floorTrace(axis);
    addTone(trace, axis, 10.0 * kMHz, -10.0);

    HarmonicModel model;
    model.measure(axis, trace, 0.0, 60.0 * kMHz, 1.0 * kMHz, 0.0);
    const auto &result = model.result();

    REQUIRE(result.fundamental_status == HarmonicModel::Status::NoFundamental);
    for (const auto &entry : result.harmonics)
        REQUIRE(entry.status == HarmonicModel::Status::NoFundamental);
}

TEST_CASE("a harmonic peak inside the search window sets the reported frequency") {
    const auto axis = uniformAxis(0.0, 60.0 * kMHz, 1.0 * kMHz);
    auto trace = floorTrace(axis);
    addTone(trace, axis, 10.0 * kMHz, -10.0);
    addTone(trace, axis, 21.0 * kMHz, -40.0); // just inside the 20 MHz +- 1 MHz window

    HarmonicModel model;
    model.measure(axis, trace, 0.0, 60.0 * kMHz, 1.0 * kMHz);
    const auto &second = model.result().harmonics[0];

    REQUIRE(second.status == HarmonicModel::Status::Measured);
    REQUIRE(second.expected_Hz == Catch::Approx(20.0 * kMHz));
    REQUIRE(second.frequency_Hz == Catch::Approx(21.0 * kMHz));
    REQUIRE(second.relative_dBc == Catch::Approx(-30.0));
}

TEST_CASE("mismatched or empty trace input measures nothing") {
    const auto axis = uniformAxis(0.0, 10.0 * kMHz, 1.0 * kMHz);

    HarmonicModel model;
    model.measure(axis, std::vector<double>(axis.size() - 1, -120.0), 0.0, 10.0 * kMHz, 1.0 * kMHz);
    REQUIRE(model.result().fundamental_status == HarmonicModel::Status::NoFundamental);

    model.measure({}, {}, 0.0, 10.0 * kMHz, 1.0 * kMHz);
    REQUIRE(model.result().fundamental_status == HarmonicModel::Status::NoFundamental);
}

TEST_CASE("a negative-frequency tone on a one-sided trace is a fundamental") {
    // Complex-baseband ADC/PFB traces are one-sided: a clear tone can sit at a negative
    // frequency with no positive image. The auto search must measure it, as the marker and
    // Peak readouts do.
    const auto axis = uniformAxis(-250.0 * kMHz, 250.0 * kMHz, 1.0 * kMHz);
    auto trace = floorTrace(axis);
    addTone(trace, axis, -100.0 * kMHz, -10.0);
    addTone(trace, axis, -200.0 * kMHz, -40.0);

    HarmonicModel model;
    model.measure(axis, trace, -250.0 * kMHz, 250.0 * kMHz, 1.0 * kMHz);
    const auto &result = model.result();

    REQUIRE(result.fundamental_status == HarmonicModel::Status::Measured);
    REQUIRE(result.fundamental_Hz == Catch::Approx(-100.0 * kMHz));
    REQUIRE(result.fundamental_dBm == Catch::Approx(-10.0));
    REQUIRE(result.harmonics[0].status == HarmonicModel::Status::Measured);
    REQUIRE(result.harmonics[0].frequency_Hz == Catch::Approx(-200.0 * kMHz));
    REQUIRE(result.harmonics[0].relative_dBc == Catch::Approx(-30.0));
}

TEST_CASE("a negative tone is measured when the span hides its positive image") {
    const auto axis = uniformAxis(-300.0 * kMHz, -100.0 * kMHz, 1.0 * kMHz);
    auto trace = floorTrace(axis);
    addTone(trace, axis, -200.0 * kMHz, -10.0);

    HarmonicModel model;
    model.measure(axis, trace, -300.0 * kMHz, -100.0 * kMHz, 1.0 * kMHz);
    REQUIRE(model.result().fundamental_status == HarmonicModel::Status::Measured);
    REQUIRE(model.result().fundamental_Hz == Catch::Approx(-200.0 * kMHz));
}

TEST_CASE("a raised noise band is not a clear fundamental") {
    const auto axis = uniformAxis(0.0, 300.0 * kMHz, 1.0 * kMHz);
    auto trace = floorTrace(axis);
    // A 100-200 MHz noise pedestal 40 dB above the floor, with a shallow ripple.
    for (size_t i = 0; i < axis.size(); ++i) {
        if (axis[i] >= 100.0 * kMHz && axis[i] <= 200.0 * kMHz)
            trace[i] = -80.0 + 0.2 * static_cast<double>(static_cast<int>(i % 3) - 1);
    }

    HarmonicModel model;
    model.measure(axis, trace, 0.0, 300.0 * kMHz, 1.0 * kMHz);
    const auto &result = model.result();

    REQUIRE(result.fundamental_status == HarmonicModel::Status::NoFundamental);
    for (const auto &entry : result.harmonics)
        REQUIRE(entry.status == HarmonicModel::Status::NoFundamental);
}

TEST_CASE("a flat raised noise band is not a clear fundamental") {
    const auto axis = uniformAxis(0.0, 300.0 * kMHz, 1.0 * kMHz);
    auto trace = floorTrace(axis);
    for (size_t i = 0; i < axis.size(); ++i) {
        if (axis[i] >= 100.0 * kMHz && axis[i] <= 200.0 * kMHz)
            trace[i] = -80.0;
    }

    HarmonicModel model;
    model.measure(axis, trace, 0.0, 300.0 * kMHz, 1.0 * kMHz);
    REQUIRE(model.result().fundamental_status == HarmonicModel::Status::NoFundamental);
}

TEST_CASE("an unresolved harmonic records whether the RBW or the grid is too coarse") {
    const auto coarse_axis = uniformAxis(0.0, 60.0 * kMHz, 10.0 * kMHz);
    auto coarse_trace = floorTrace(coarse_axis);
    addTone(coarse_trace, coarse_axis, 10.0 * kMHz, -10.0);

    HarmonicModel model;
    // A narrow RBW cannot help a 10 MHz bin spacing against a 10 MHz fundamental.
    model.measure(coarse_axis, coarse_trace, 0.0, 60.0 * kMHz, 1.0e3);
    REQUIRE(model.result().harmonics[0].status == HarmonicModel::Status::Unresolved);
    REQUIRE_FALSE(model.result().harmonics[0].rbw_limited);

    const auto fine_axis = uniformAxis(0.0, 60.0 * kMHz, 1.0 * kMHz);
    auto fine_trace = floorTrace(fine_axis);
    addTone(fine_trace, fine_axis, 10.0 * kMHz, -10.0);
    model.measure(fine_axis, fine_trace, 0.0, 60.0 * kMHz, 20.0 * kMHz);
    REQUIRE(model.result().harmonics[0].status == HarmonicModel::Status::Unresolved);
    REQUIRE(model.result().harmonics[0].rbw_limited);
}
