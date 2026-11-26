#include "spectrum_analyzer_im3.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <utility>
#include <vector>

// IM3 (third-order intermodulation) measurement rules. The model is UI-free: it reads the
// same jitter-free readout trace/axis as Peak, the markers, and the harmonic measurement,
// auto-detects the two fundamental tones (or takes the two marker frequencies the widget
// supplies), and searches for the 2*f1-f2 and 2*f2-f1 products. test_ui drives the panel.

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

TEST_CASE("IM3 measurement starts hidden and on auto-detected tones") {
    Im3Model model;
    REQUIRE_FALSE(model.visible());
    REQUIRE_FALSE(model.useMarkers());
    model.setVisible(true);
    model.setUseMarkers(true);
    REQUIRE(model.visible());
    REQUIRE(model.useMarkers());
}

TEST_CASE("two clear tones report both IM3 products in dBc and a TOI estimate") {
    const auto axis = uniformAxis(0.0, 100.0 * kMHz, 1.0 * kMHz);
    auto trace = floorTrace(axis);
    addTone(trace, axis, 10.0 * kMHz, -10.0); // f1
    addTone(trace, axis, 15.0 * kMHz, -12.0); // f2
    addTone(trace, axis, 5.0 * kMHz, -60.0);  // 2*f1 - f2
    addTone(trace, axis, 20.0 * kMHz, -62.0); // 2*f2 - f1

    Im3Model model;
    model.measure(axis, trace, 0.0, 100.0 * kMHz, 1.0 * kMHz);
    const auto &result = model.result();

    REQUIRE(result.tone_status == Im3Model::Status::Measured);
    REQUIRE(result.tones[0].frequency_Hz == Catch::Approx(10.0 * kMHz));
    REQUIRE(result.tones[0].amplitude_dBm == Catch::Approx(-10.0));
    REQUIRE(result.tones[1].frequency_Hz == Catch::Approx(15.0 * kMHz));
    REQUIRE(result.tones[1].amplitude_dBm == Catch::Approx(-12.0));

    const auto &low = result.products[0];
    REQUIRE_FALSE(low.high_side);
    REQUIRE(low.status == Im3Model::Status::Measured);
    REQUIRE(low.expected_Hz == Catch::Approx(5.0 * kMHz));
    REQUIRE(low.frequency_Hz == Catch::Approx(5.0 * kMHz));
    REQUIRE(low.amplitude_dBm == Catch::Approx(-60.0));
    REQUIRE(low.relative_dBc == Catch::Approx(-50.0));

    const auto &high = result.products[1];
    REQUIRE(high.high_side);
    REQUIRE(high.status == Im3Model::Status::Measured);
    REQUIRE(high.expected_Hz == Catch::Approx(20.0 * kMHz));
    REQUIRE(high.frequency_Hz == Catch::Approx(20.0 * kMHz));
    REQUIRE(high.amplitude_dBm == Catch::Approx(-62.0));
    REQUIRE(high.relative_dBc == Catch::Approx(-50.0));

    // TOI = (P1 + P2 - dBc) / 2 for each product, averaged when both are measured.
    REQUIRE(result.toi_valid);
    REQUIRE(result.toi_dBm == Catch::Approx(14.0));
}

TEST_CASE("the TOI estimate averages the two measured products") {
    const auto axis = uniformAxis(0.0, 100.0 * kMHz, 1.0 * kMHz);
    auto trace = floorTrace(axis);
    addTone(trace, axis, 20.0 * kMHz, -10.0);
    addTone(trace, axis, 30.0 * kMHz, -20.0);
    addTone(trace, axis, 10.0 * kMHz, -55.0); // -45 dBc vs f1 -> intercept 7.5 dBm
    addTone(trace, axis, 40.0 * kMHz, -75.0); // -55 dBc vs f2 -> intercept 12.5 dBm

    Im3Model model;
    model.measure(axis, trace, 0.0, 100.0 * kMHz, 1.0 * kMHz);
    const auto &result = model.result();

    REQUIRE(result.products[0].status == Im3Model::Status::Measured);
    REQUIRE(result.products[0].relative_dBc == Catch::Approx(-45.0));
    REQUIRE(result.products[0].intercept_dBm == Catch::Approx(7.5));
    REQUIRE(result.products[1].status == Im3Model::Status::Measured);
    REQUIRE(result.products[1].relative_dBc == Catch::Approx(-55.0));
    REQUIRE(result.products[1].intercept_dBm == Catch::Approx(12.5));
    REQUIRE(result.toi_valid);
    REQUIRE(result.toi_dBm == Catch::Approx(10.0));
}

TEST_CASE("fewer than two clear tones reports that two are needed") {
    const auto axis = uniformAxis(0.0, 100.0 * kMHz, 1.0 * kMHz);
    auto trace = floorTrace(axis);
    addTone(trace, axis, 10.0 * kMHz, -10.0);

    Im3Model model;
    model.measure(axis, trace, 0.0, 100.0 * kMHz, 1.0 * kMHz);
    const auto &result = model.result();

    REQUIRE(result.tone_status == Im3Model::Status::NeedTwoTones);
    REQUIRE_FALSE(result.toi_valid);
    for (const auto &product : result.products)
        REQUIRE(product.status == Im3Model::Status::NeedTwoTones);
}

TEST_CASE("a noise-only trace reports that two tones are needed") {
    const auto axis = uniformAxis(0.0, 100.0 * kMHz, 1.0 * kMHz);
    const auto trace = floorTrace(axis);

    Im3Model model;
    model.measure(axis, trace, 0.0, 100.0 * kMHz, 1.0 * kMHz);
    REQUIRE(model.result().tone_status == Im3Model::Status::NeedTwoTones);
    REQUIRE_FALSE(model.result().toi_valid);
}

TEST_CASE("equally strong tones are ordered by frequency") {
    const auto axis = uniformAxis(0.0, 100.0 * kMHz, 1.0 * kMHz);
    auto trace = floorTrace(axis);
    addTone(trace, axis, 30.0 * kMHz, -10.0);
    addTone(trace, axis, 20.0 * kMHz, -10.0);
    addTone(trace, axis, 10.0 * kMHz, -60.0); // 2*20 - 30
    addTone(trace, axis, 40.0 * kMHz, -60.0); // 2*30 - 20

    Im3Model model;
    model.measure(axis, trace, 0.0, 100.0 * kMHz, 1.0 * kMHz);
    const auto &result = model.result();

    REQUIRE(result.tone_status == Im3Model::Status::Measured);
    REQUIRE(result.tones[0].frequency_Hz == Catch::Approx(20.0 * kMHz));
    REQUIRE(result.tones[1].frequency_Hz == Catch::Approx(30.0 * kMHz));
    REQUIRE(result.products[0].expected_Hz == Catch::Approx(10.0 * kMHz));
    REQUIRE(result.products[1].expected_Hz == Catch::Approx(40.0 * kMHz));
    REQUIRE(result.products[0].status == Im3Model::Status::Measured);
    REQUIRE(result.products[1].status == Im3Model::Status::Measured);
}

TEST_CASE("a product outside the swept span is reported out of span, not guessed") {
    const auto axis = uniformAxis(0.0, 100.0 * kMHz, 1.0 * kMHz);
    auto trace = floorTrace(axis);
    addTone(trace, axis, 12.0 * kMHz, -10.0);
    addTone(trace, axis, 20.0 * kMHz, -10.0);
    addTone(trace, axis, 4.0 * kMHz, -60.0);  // 2*12 - 20, outside the span below
    addTone(trace, axis, 28.0 * kMHz, -60.0); // 2*20 - 12

    Im3Model model;
    model.measure(axis, trace, 5.0 * kMHz, 30.0 * kMHz, 1.0 * kMHz);
    const auto &result = model.result();

    REQUIRE(result.tone_status == Im3Model::Status::Measured);
    REQUIRE(result.products[0].expected_Hz == Catch::Approx(4.0 * kMHz));
    REQUIRE(result.products[0].status == Im3Model::Status::OutOfSpan);
    REQUIRE(result.products[1].expected_Hz == Catch::Approx(28.0 * kMHz));
    REQUIRE(result.products[1].status == Im3Model::Status::Measured);
    REQUIRE(result.products[1].amplitude_dBm == Catch::Approx(-60.0));
    // TOI is still valid from the one measured product.
    REQUIRE(result.toi_valid);
    REQUIRE(result.toi_dBm == Catch::Approx(15.0));
}

TEST_CASE("a product below the noise floor is reported not found") {
    const auto axis = uniformAxis(0.0, 100.0 * kMHz, 1.0 * kMHz);
    auto trace = floorTrace(axis);
    addTone(trace, axis, 10.0 * kMHz, -10.0);
    addTone(trace, axis, 15.0 * kMHz, -12.0);
    // No products on the trace: both expected bins hold only the noise floor.

    Im3Model model;
    model.measure(axis, trace, 0.0, 100.0 * kMHz, 1.0 * kMHz);
    const auto &result = model.result();

    REQUIRE(result.tone_status == Im3Model::Status::Measured);
    for (const auto &product : result.products) {
        REQUIRE(product.status == Im3Model::Status::NotFound);
        REQUIRE(product.relative_dBc == Catch::Approx(0.0));
    }
    REQUIRE_FALSE(result.toi_valid);
}

TEST_CASE("an RBW too wide to separate a product from its tone is reported unresolved") {
    const auto axis = uniformAxis(0.0, 100.0 * kMHz, 1.0 * kMHz);
    auto trace = floorTrace(axis);
    addTone(trace, axis, 10.0 * kMHz, -10.0);
    addTone(trace, axis, 20.0 * kMHz, -10.0);
    addTone(trace, axis, 0.0 * kMHz, -60.0);

    Im3Model model;
    // A 20 MHz RBW is wider than the 10 MHz tone spacing.
    model.measure(axis, trace, 0.0, 100.0 * kMHz, 20.0 * kMHz);
    const auto &result = model.result();

    REQUIRE(result.tone_status == Im3Model::Status::Measured);
    for (const auto &product : result.products) {
        REQUIRE(product.status == Im3Model::Status::Unresolved);
        REQUIRE(product.rbw_limited);
    }
    REQUIRE_FALSE(result.toi_valid);
}

TEST_CASE("a coarse grid unresolved case records that the bin spacing is the limit") {
    const auto axis = uniformAxis(0.0, 100.0 * kMHz, 10.0 * kMHz);
    auto trace = floorTrace(axis);
    addTone(trace, axis, 30.0 * kMHz, -10.0);
    addTone(trace, axis, 50.0 * kMHz, -10.0);

    Im3Model model;
    // A narrow RBW cannot help a 10 MHz bin spacing against a 20 MHz tone spacing.
    model.measure(axis, trace, 0.0, 100.0 * kMHz, 1.0e3);
    REQUIRE(model.result().tone_status == Im3Model::Status::Measured);
    for (const auto &product : model.result().products) {
        REQUIRE(product.status == Im3Model::Status::Unresolved);
        REQUIRE_FALSE(product.rbw_limited);
    }
}

TEST_CASE("a supplied marker pair replaces auto-detection and is ordered by frequency") {
    const auto axis = uniformAxis(0.0, 100.0 * kMHz, 1.0 * kMHz);
    auto trace = floorTrace(axis);
    addTone(trace, axis, 10.0 * kMHz, -10.0);
    addTone(trace, axis, 15.0 * kMHz, -12.0);

    Im3Model model;
    // The markers sit a bin away from the auto-detected peaks and are supplied high first.
    model.measure(axis, trace, 0.0, 100.0 * kMHz, 1.0 * kMHz,
                  std::pair<double, double>{16.0 * kMHz, 11.0 * kMHz});
    const auto &result = model.result();

    REQUIRE(result.tone_status == Im3Model::Status::Measured);
    REQUIRE(result.tones[0].frequency_Hz == Catch::Approx(11.0 * kMHz));
    REQUIRE(result.tones[0].amplitude_dBm == Catch::Approx(trace[11]));
    REQUIRE(result.tones[1].frequency_Hz == Catch::Approx(16.0 * kMHz));
    REQUIRE(result.tones[1].amplitude_dBm == Catch::Approx(trace[16]));
    REQUIRE(result.products[0].expected_Hz == Catch::Approx(6.0 * kMHz));
    REQUIRE(result.products[1].expected_Hz == Catch::Approx(21.0 * kMHz));
}

TEST_CASE("a supplied marker tone outside the span is reported as such") {
    const auto axis = uniformAxis(0.0, 100.0 * kMHz, 1.0 * kMHz);
    auto trace = floorTrace(axis);
    addTone(trace, axis, 10.0 * kMHz, -10.0);
    addTone(trace, axis, 15.0 * kMHz, -12.0);

    Im3Model model;
    model.measure(axis, trace, 0.0, 100.0 * kMHz, 1.0 * kMHz,
                  std::pair<double, double>{10.0 * kMHz, 150.0 * kMHz});
    const auto &result = model.result();

    REQUIRE(result.tone_status == Im3Model::Status::TonesOutOfSpan);
    for (const auto &product : result.products)
        REQUIRE(product.status == Im3Model::Status::TonesOutOfSpan);
    REQUIRE_FALSE(result.toi_valid);
}

TEST_CASE("a supplied pair that cannot form products is rejected") {
    const auto axis = uniformAxis(0.0, 100.0 * kMHz, 1.0 * kMHz);
    auto trace = floorTrace(axis);
    addTone(trace, axis, 10.0 * kMHz, -10.0);
    addTone(trace, axis, 15.0 * kMHz, -12.0);

    Im3Model model;
    model.measure(axis, trace, 0.0, 100.0 * kMHz, 1.0 * kMHz,
                  std::pair<double, double>{10.0 * kMHz, 10.0 * kMHz});
    REQUIRE(model.result().tone_status == Im3Model::Status::NeedTwoTones);

    model.measure(axis, trace, 0.0, 100.0 * kMHz, 1.0 * kMHz,
                  std::pair<double, double>{0.0, 15.0 * kMHz});
    REQUIRE(model.result().tone_status == Im3Model::Status::NeedTwoTones);
}

TEST_CASE("a real-domain trace does not pair a tone with its own mirror image") {
    // A real-domain trace mirrors every tone at +-f. The two fundamentals must be +10 and
    // +15 MHz, not +10 and its -10 MHz image.
    const auto axis = uniformAxis(-30.0 * kMHz, 30.0 * kMHz, 1.0 * kMHz);
    auto trace = floorTrace(axis);
    for (double f : {10.0, 15.0}) {
        addTone(trace, axis, f * kMHz, -10.0);
        addTone(trace, axis, -f * kMHz, -10.0);
    }
    addTone(trace, axis, 5.0 * kMHz, -60.0);   // 2*10 - 15
    addTone(trace, axis, 20.0 * kMHz, -60.0);  // 2*15 - 10
    addTone(trace, axis, -5.0 * kMHz, -60.0);  // mirrored products
    addTone(trace, axis, -20.0 * kMHz, -60.0);

    Im3Model model;
    model.measure(axis, trace, -30.0 * kMHz, 30.0 * kMHz, 1.0 * kMHz);
    const auto &result = model.result();

    REQUIRE(result.tone_status == Im3Model::Status::Measured);
    REQUIRE(result.tones[0].frequency_Hz == Catch::Approx(10.0 * kMHz));
    REQUIRE(result.tones[1].frequency_Hz == Catch::Approx(15.0 * kMHz));
    REQUIRE(result.products[0].expected_Hz == Catch::Approx(5.0 * kMHz));
    REQUIRE(result.products[1].expected_Hz == Catch::Approx(20.0 * kMHz));
}

TEST_CASE("a negative-frequency tone pair on a one-sided trace is measured") {
    // Complex-baseband ADC/PFB traces are one-sided: both tones can be negative. Their
    // products are 2*f1-f2 and 2*f2-f1 at their own (negative) frequencies.
    const auto axis = uniformAxis(-250.0 * kMHz, 250.0 * kMHz, 1.0 * kMHz);
    auto trace = floorTrace(axis);
    addTone(trace, axis, -200.0 * kMHz, -10.0);
    addTone(trace, axis, -150.0 * kMHz, -12.0);
    addTone(trace, axis, -250.0 * kMHz, -60.0); // 2*(-200) - (-150)
    addTone(trace, axis, -100.0 * kMHz, -62.0); // 2*(-150) - (-200)

    Im3Model model;
    model.measure(axis, trace, -250.0 * kMHz, 250.0 * kMHz, 1.0 * kMHz);
    const auto &result = model.result();

    REQUIRE(result.tone_status == Im3Model::Status::Measured);
    REQUIRE(result.tones[0].frequency_Hz == Catch::Approx(-200.0 * kMHz));
    REQUIRE(result.tones[1].frequency_Hz == Catch::Approx(-150.0 * kMHz));
    REQUIRE(result.products[0].status == Im3Model::Status::Measured);
    REQUIRE(result.products[0].frequency_Hz == Catch::Approx(-250.0 * kMHz));
    REQUIRE(result.products[1].status == Im3Model::Status::Measured);
    REQUIRE(result.products[1].frequency_Hz == Catch::Approx(-100.0 * kMHz));
}

TEST_CASE("a raised noise band does not supply a fundamental") {
    const auto axis = uniformAxis(0.0, 300.0 * kMHz, 1.0 * kMHz);
    auto trace = floorTrace(axis);
    // A 100-200 MHz noise pedestal 40 dB above the floor, with a shallow ripple, plus one
    // real tone. The pedestal must not be named as the second fundamental.
    for (size_t i = 0; i < axis.size(); ++i) {
        if (axis[i] >= 100.0 * kMHz && axis[i] <= 200.0 * kMHz)
            trace[i] = -80.0 + 0.2 * static_cast<double>(static_cast<int>(i % 3) - 1);
    }
    addTone(trace, axis, 20.0 * kMHz, -10.0);

    Im3Model model;
    model.measure(axis, trace, 0.0, 300.0 * kMHz, 1.0 * kMHz);
    REQUIRE(model.result().tone_status == Im3Model::Status::NeedTwoTones);
}

TEST_CASE("mismatched or empty trace input measures nothing") {
    const auto axis = uniformAxis(0.0, 100.0 * kMHz, 1.0 * kMHz);

    Im3Model model;
    model.measure(axis, std::vector<double>(axis.size() - 1, -120.0), 0.0, 100.0 * kMHz,
                  1.0 * kMHz);
    REQUIRE(model.result().tone_status == Im3Model::Status::NeedTwoTones);

    model.measure({}, {}, 0.0, 100.0 * kMHz, 1.0 * kMHz);
    REQUIRE(model.result().tone_status == Im3Model::Status::NeedTwoTones);
}
