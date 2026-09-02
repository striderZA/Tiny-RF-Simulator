#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "pfb_filter_design.h"

using Catch::Approx;

TEST_CASE("PfbFilterDesign taps length and symmetry", "[pfb_filter_design]") {
    PfbFilterDesign d(32, 8, 8.0);
    REQUIRE(d.tapCount() == 256);
    REQUIRE(d.channelCount() == 32);
    REQUIRE(d.tapsPerBranch() == 8);

    const auto &taps = d.taps();
    // Kaiser windowed-sinc is symmetric about (N-1)/2 -> linear phase.
    for (int i = 0; i < 256; ++i)
        REQUIRE(taps[i] == Approx(taps[255 - i]).margin(1e-12));

    // DC normalization: h[n] sums to exactly 1 (H(0) = 1).
    double sum = 0.0;
    for (double v : taps) {
        sum += v;
        REQUIRE(std::isfinite(v));
    }
    REQUIRE(sum == Approx(1.0).margin(1e-9));
}

TEST_CASE("PfbFilterDesign constructor clamps parameters", "[pfb_filter_design]") {
    PfbFilterDesign d(1, 0, -5.0);   // below engine minima
    REQUIRE(d.channelCount() == 2);  // M clamp floor
    REQUIRE(d.tapsPerBranch() == 1); // K clamp floor
    REQUIRE(d.beta() == 0.0);
    PfbFilterDesign e(9999, 99, 99.0);
    REQUIRE(e.channelCount() == 2048);
    REQUIRE(e.tapsPerBranch() == 64);
    REQUIRE(e.beta() == 20.0);
}

TEST_CASE("PfbFilterDesign response: normalization and symmetry", "[pfb_filter_design]") {
    PfbFilterDesign d(32, 8, 8.0);
    REQUIRE(d.responseAt(0.0) == Approx(1.0).margin(1e-9));
    REQUIRE(d.responseAt(0.3) == Approx(d.responseAt(-0.3)).margin(1e-9));
    REQUIRE(d.responseAt(1.2) == Approx(d.responseAt(-1.2)).margin(1e-9));
}

TEST_CASE("PfbFilterDesign response: band edge and adjacent center", "[pfb_filter_design]") {
    auto db = [](double v) { return 20.0 * std::log10(v); };

    PfbFilterDesign a(32, 8, 8.0);
    REQUIRE(db(a.responseAt(0.5)) == Approx(-6.02).margin(0.2)); // band edge
    REQUIRE(db(a.responseAt(1.0)) < -40.0);                      // adjacent center

    PfbFilterDesign b(32, 16, 12.0);
    REQUIRE(db(b.responseAt(1.0)) < -100.0); // deeper stopband
}
