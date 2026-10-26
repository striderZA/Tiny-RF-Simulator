#include "nonlinear_model.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

TEST_CASE("NonlinearModel P1dB defaults to 100 dBm", "[nonlinear][p1db]") {
    NonlinearModel model;
    REQUIRE(model.p1db_dBm() == Approx(100.0));
}

TEST_CASE("NonlinearModel P1dB setter updates value", "[nonlinear][p1db]") {
    NonlinearModel model;
    model.setP1dB_dBm(20.0);
    REQUIRE(model.p1db_dBm() == Approx(20.0));
}

TEST_CASE("NonlinearModel derives OIP3 from P1dB when OIP3 is default", "[nonlinear][p1db]") {
    NonlinearModel model;
    REQUIRE(model.oip3_dBm() == Approx(100.0)); // default

    model.setP1dB_dBm(20.0);
    REQUIRE(model.oip3_dBm() == Approx(29.6).margin(0.01)); // 20 + 9.6
}

TEST_CASE("NonlinearModel does not override explicit OIP3 when setting P1dB", "[nonlinear][p1db]") {
    NonlinearModel model;
    model.setOIP3_dBm(35.0);
    model.setP1dB_dBm(20.0);
    REQUIRE(model.oip3_dBm() == Approx(35.0)); // unchanged
    REQUIRE(model.p1db_dBm() == Approx(20.0));
}

namespace {

// Compression (dB) of one 1 GHz tone through a unity-gain model.
double compressionAt(const NonlinearModel &model, double in_dBm) {
    const auto r = model.process({{1.0e9, in_dBm, 0.0}}, [](double) { return 1.0; });
    return r.tone_compression_dB[0];
}

// Input power (dBm) at which compression reaches target_dB. Compression only falls as the input
// rises, so bisection finds the single crossing.
double inputAtCompression(const NonlinearModel &model, double target_dB) {
    double lo = -40.0;
    double hi = 60.0;
    for (int i = 0; i < 200; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (compressionAt(model, mid) > target_dB)
            lo = mid;
        else
            hi = mid;
    }
    return 0.5 * (lo + hi);
}

} // namespace

TEST_CASE("NonlinearModel 1 dB point is the closed-form input for scalar OIP3",
          "[nonlinear][p1db][issue273]") {
    NonlinearModel model;
    model.setOIP3_dBm(20.0);
    model.setEnabled(true);
    // Closed form: 1 dB compression at Pout = OIP3 - 9.64 dB, so 10.36 dBm here.
    CHECK(inputAtCompression(model, -1.0) == Approx(10.36).margin(0.2));
}

TEST_CASE("NonlinearModel setP1dB puts the 1 dB input at P1dB", "[nonlinear][p1db][issue273]") {
    for (double p1db : {0.0, 10.0, 15.0}) {
        CAPTURE(p1db);
        NonlinearModel model;
        model.setP1dB_dBm(p1db);
        model.setEnabled(true);
        CHECK(inputAtCompression(model, -1.0) == Approx(p1db).margin(0.2));
    }
}

TEST_CASE("NonlinearModel compression floors at -3.01 dB deep in saturation",
          "[nonlinear][p1db][issue273]") {
    NonlinearModel model;
    model.setOIP3_dBm(0.0);
    model.setEnabled(true);
    // Distortion is capped at half the fundamental, so compression stops at -3.01 dB.
    CHECK(compressionAt(model, 30.0) == Approx(10.0 * std::log10(0.5)).margin(1e-9));
    CHECK(compressionAt(model, 60.0) == Approx(10.0 * std::log10(0.5)).margin(1e-9));
}

namespace {

// OIP3 is 20 dBm below 1.5 GHz and 30 dBm above it.
double oip3Split(double freq_Hz) { return freq_Hz < 1.5e9 ? 20.0 : 30.0; }

} // namespace

TEST_CASE("NonlinearModel compression is per tone: equal tones, each with its own OIP3",
          "[nonlinear][p1db][issue273]") {
    NonlinearModel model;
    model.setOIP3_dBm(20.0);
    model.setEnabled(true);
    const NonlinearModel::GainFn unity = [](double) { return 1.0; };
    // Equal 15 dBm tones. At 1 GHz (OIP3 20 dBm) the tone is 5 dB below its OIP3, so its
    // closed form is past the floor and reads -3.01 dB. At 2 GHz (OIP3 30 dBm) it is 15 dB below,
    // so it reads 20*log10(1 - 10^-1.5) = -0.28 dB.
    const auto r = model.process({{1.0e9, 15.0, 0.0}, {2.0e9, 15.0, 0.0}}, unity, oip3Split);
    REQUIRE(r.tone_compression_dB.size() == 2);
    CHECK(r.tone_compression_dB[0] == Approx(10.0 * std::log10(0.5)).margin(1e-9));
    CHECK(r.tone_compression_dB[1] ==
          Approx(20.0 * std::log10(1.0 - std::pow(10.0, -1.5))).margin(1e-9));
}

TEST_CASE("NonlinearModel compression per tone follows each tone's own output power",
          "[nonlinear][p1db][issue273]") {
    NonlinearModel model;
    model.setOIP3_dBm(20.0);
    model.setEnabled(true);
    const NonlinearModel::GainFn unity = [](double) { return 1.0; };
    // 10 dBm at 1 GHz (OIP3 20 dBm): ratio 0.1, so -0.92 dB. 25 dBm at 2 GHz (OIP3 30 dBm): ratio
    // 0.316, past the floor, so -3.01 dB. A single worst-tone value would apply -3.01 dB to both.
    const auto r = model.process({{1.0e9, 10.0, 0.0}, {2.0e9, 25.0, 0.0}}, unity, oip3Split);
    REQUIRE(r.tone_compression_dB.size() == 2);
    CHECK(r.tone_compression_dB[0] == Approx(20.0 * std::log10(1.0 - 0.1)).margin(1e-9));
    CHECK(r.tone_compression_dB[1] == Approx(10.0 * std::log10(0.5)).margin(1e-9));
}
