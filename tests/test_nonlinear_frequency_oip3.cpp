#include "nonlinear_model.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <optional>
#include <vector>

using Catch::Approx;

namespace {

// Unit amplitude gain, so product powers depend only on the intercepts.
const NonlinearModel::GainFn kUnitGain = [](double) { return 1.0; };

NonlinearModel scalarModel(double oip3_dBm) {
    NonlinearModel model;
    model.setOIP3_dBm(oip3_dBm);
    model.setEnabled(true);
    return model;
}

// Power of the product tone at freq_Hz, or nullopt when the model made none there.
std::optional<double> productPower(const NonlinearModel::Result &result, double freq_Hz) {
    for (const auto &tone : result.extra_tones) {
        if (std::abs(tone.freq_Hz - freq_Hz) < 1.0)
            return tone.power_dBm;
    }
    return std::nullopt;
}

// Power of the product at freq_Hz. Fails the test when the model made no product there.
double requireProductPower(const NonlinearModel::Result &result, double freq_Hz) {
    const auto power = productPower(result, freq_Hz);
    REQUIRE(power.has_value());
    return *power;
}

} // namespace

TEST_CASE("Per-frequency OIP3 sets the third harmonic of each tone", "[nonlinear][oip3]") {
    const std::vector<Spectrum::Tone> tones = {{1.0e9, -20.0, 0.0}};
    // 10 dBm at 1 GHz. The 3 GHz harmonic takes that fundamental intercept, not the 25 dBm
    // at 3 GHz. The scalar model with 10 dBm is the reference.
    const NonlinearModel::Oip3Fn oip3 = [](double f) { return f < 1.5e9 ? 10.0 : 25.0; };

    // The scalar intercept (-50 dBm) must not be used while a function is supplied.
    const auto result = scalarModel(-50.0).process(tones, kUnitGain, oip3);
    const auto reference = scalarModel(10.0).process(tones, kUnitGain);

    CHECK(requireProductPower(result, 3.0e9) == Approx(requireProductPower(reference, 3.0e9)));
}

TEST_CASE("Two-tone products take the intercept at the fundamental they square",
          "[nonlinear][oip3]") {
    const double f1 = 1.0e9;
    const double f2 = 1.1e9;
    const std::vector<Spectrum::Tone> tones = {{f1, -20.0, 0.0}, {f2, -20.0, 0.0}};
    const NonlinearModel::Oip3Fn oip3 = [f1](double f) {
        return std::abs(f - f1) < 1.0 ? 10.0 : 20.0;
    };

    const auto result = scalarModel(-50.0).process(tones, kUnitGain, oip3);
    // Products that square f1 (2f1 - f2, 2f1 + f2) and the third harmonic of f1 use 10 dBm.
    const auto at_f1 = scalarModel(10.0).process(tones, kUnitGain);
    // Products that square f2 (2f2 + f1, |f1 - 2f2|) and the third harmonic of f2 use 20 dBm.
    const auto at_f2 = scalarModel(20.0).process(tones, kUnitGain);

    CHECK(requireProductPower(result, 0.9e9) == Approx(requireProductPower(at_f1, 0.9e9)));
    CHECK(requireProductPower(result, 3.1e9) == Approx(requireProductPower(at_f1, 3.1e9)));
    CHECK(requireProductPower(result, 3.0e9) == Approx(requireProductPower(at_f1, 3.0e9)));
    CHECK(requireProductPower(result, 3.2e9) == Approx(requireProductPower(at_f2, 3.2e9)));
    CHECK(requireProductPower(result, 1.2e9) == Approx(requireProductPower(at_f2, 1.2e9)));
    CHECK(requireProductPower(result, 3.3e9) == Approx(requireProductPower(at_f2, 3.3e9)));

    // The two references differ at the same products, so the checks above can tell the
    // intercepts apart: 2f1 - f2 at 0.9 GHz and 2f2 + f1 at 3.2 GHz.
    CHECK(requireProductPower(at_f1, 0.9e9) != Approx(requireProductPower(at_f2, 0.9e9)));
    CHECK(requireProductPower(at_f1, 3.2e9) != Approx(requireProductPower(at_f2, 3.2e9)));
}

TEST_CASE("A constant per-frequency OIP3 matches the scalar model", "[nonlinear][oip3]") {
    const std::vector<Spectrum::Tone> tones = {{1.0e9, -20.0, 0.0}, {1.1e9, -20.0, 0.0}};
    const NonlinearModel::Oip3Fn constant = [](double) { return 12.0; };

    const auto with_function = scalarModel(-50.0).process(tones, kUnitGain, constant);
    const auto scalar = scalarModel(12.0).process(tones, kUnitGain);

    for (double freq : {0.9e9, 1.2e9, 3.0e9, 3.1e9, 3.2e9, 3.3e9}) {
        CAPTURE(freq);
        CHECK(requireProductPower(with_function, freq) ==
              Approx(requireProductPower(scalar, freq)));
    }
}
