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

// Scalar model with the given OIP2 and OIP3 in dBm. Both intercepts are fixed, and the model
// is enabled.
NonlinearModel scalarModel(double oip2_dBm, double oip3_dBm = 100.0) {
    NonlinearModel model;
    model.setOIP2_dBm(oip2_dBm);
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

TEST_CASE("Per-frequency OIP2 sets the second harmonic of each tone", "[nonlinear][oip2]") {
    const std::vector<Spectrum::Tone> tones = {{1.0e9, -20.0, 0.0}};
    // 10 dBm at 1 GHz. The 2 GHz harmonic takes that fundamental intercept, not the 25 dBm
    // at 3 GHz. The scalar model with 10 dBm is the reference.
    const NonlinearModel::Oip2Fn oip2 = [](double f) { return f < 1.5e9 ? 10.0 : 25.0; };

    // The scalar intercept (-50 dBm) must not be used while a function is supplied.
    const auto result = scalarModel(-50.0).process(tones, kUnitGain, {}, oip2);
    const auto reference = scalarModel(10.0).process(tones, kUnitGain);

    CHECK(requireProductPower(result, 2.0e9) == Approx(requireProductPower(reference, 2.0e9)));
}

TEST_CASE("IM2 products take the lower of the two tones' OIP2", "[nonlinear][oip2]") {
    const double f1 = 1.0e9;
    const double f2 = 1.1e9;
    const std::vector<Spectrum::Tone> tones = {{f1, -20.0, 0.0}, {f2, -20.0, 0.0}};
    const NonlinearModel::Oip2Fn oip2 = [f1](double f) {
        return std::abs(f - f1) < 1.0 ? 10.0 : 20.0;
    };

    const auto result = scalarModel(-50.0).process(tones, kUnitGain, {}, oip2);
    // Every tone at 10 dBm, or every tone at 20 dBm. The first is the reference for f1's
    // second harmonic and for both IM2 products. The second is the reference for f2's.
    const auto at_10 = scalarModel(10.0).process(tones, kUnitGain);
    const auto at_20 = scalarModel(20.0).process(tones, kUnitGain);

    CHECK(requireProductPower(result, 2.0e9) == Approx(requireProductPower(at_10, 2.0e9)));
    CHECK(requireProductPower(result, 2.2e9) == Approx(requireProductPower(at_20, 2.2e9)));
    CHECK(requireProductPower(result, 0.1e9) == Approx(requireProductPower(at_10, 0.1e9)));
    CHECK(requireProductPower(result, 2.1e9) == Approx(requireProductPower(at_10, 2.1e9)));

    // The two references differ at the IM2 product, so the checks above tell the intercepts apart.
    CHECK(requireProductPower(at_10, 2.1e9) != Approx(requireProductPower(at_20, 2.1e9)));
}

TEST_CASE("A constant per-frequency OIP2 matches the scalar model", "[nonlinear][oip2]") {
    const std::vector<Spectrum::Tone> tones = {{1.0e9, -20.0, 0.0}, {1.1e9, -20.0, 0.0}};
    const NonlinearModel::Oip2Fn constant = [](double) { return 12.0; };

    const auto with_function = scalarModel(-50.0).process(tones, kUnitGain, {}, constant);
    const auto scalar = scalarModel(12.0).process(tones, kUnitGain);

    for (double freq : {0.1e9, 2.0e9, 2.1e9, 2.2e9}) {
        CAPTURE(freq);
        CHECK(requireProductPower(with_function, freq) ==
              Approx(requireProductPower(scalar, freq)));
    }
}

TEST_CASE("A NaN per-frequency intercept falls back to the scalar and leaves no NaN output",
          "[nonlinear][oip2][oip3]") {
    const std::vector<Spectrum::Tone> tones = {{1.0e9, -20.0, 0.0}, {1.1e9, -20.0, 0.0}};
    const NonlinearModel::Oip2Fn nan_oip2 = [](double) { return std::nan(""); };
    const NonlinearModel::Oip3Fn nan_oip3 = [](double) { return std::nan(""); };

    const auto with_nan = scalarModel(15.0, 12.0).process(tones, kUnitGain, nan_oip3, nan_oip2);
    const auto scalar = scalarModel(15.0, 12.0).process(tones, kUnitGain);

    for (const auto &tone : with_nan.extra_tones) {
        CHECK(std::isfinite(tone.freq_Hz));
        CHECK(std::isfinite(tone.power_dBm));
    }
    // Every product matches the scalar model, because the NaN intercepts are replaced by the
    // scalars.
    for (double freq : {0.9e9, 1.2e9, 2.0e9, 2.1e9, 2.2e9, 3.0e9, 3.1e9, 3.2e9, 3.3e9}) {
        CAPTURE(freq);
        CHECK(requireProductPower(with_nan, freq) == Approx(requireProductPower(scalar, freq)));
    }
}
