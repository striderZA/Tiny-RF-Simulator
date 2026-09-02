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

TEST_CASE("PfbFilterMetrics reference configs", "[pfb_filter_design]") {
    auto m = computePfbMetrics(PfbFilterDesign(32, 8, 8.0));
    REQUIRE(m.passband_halfwidth_ch > 0.30);
    REQUIRE(m.passband_halfwidth_ch < 0.49);
    REQUIRE(m.edge_loss_db == Approx(-6.0).margin(1.0));
    REQUIRE(m.adjacent_rejection_db < -40.0);
    REQUIRE(m.far_floor_db < -40.0);
    REQUIRE(m.total_taps == 256);
    REQUIRE(m.flat_noise_tilt_db > -1.5);
    REQUIRE(m.flat_noise_tilt_db < -0.1);

    // Deeper stopband with more taps/branch and a stronger window.
    auto m2 = computePfbMetrics(PfbFilterDesign(32, 16, 12.0));
    REQUIRE(m2.adjacent_rejection_db < -100.0);
}

TEST_CASE("PfbFilterMetrics rejection comparison and guidance", "[pfb_filter_design]") {
    PfbFilterDesign d(32, 8, 8.0);
    auto m = computePfbMetrics(d);
    const double achieved = -m.adjacent_rejection_db; // positive magnitude

    REQUIRE(compareRejection(m.adjacent_rejection_db, achieved) == RejectionStatus::Meets);
    REQUIRE(pfbGuidanceText(d, m, achieved).empty());

    // A much harder target must produce a non-empty, K/beta-referencing hint.
    REQUIRE(compareRejection(m.adjacent_rejection_db, 140.0) == RejectionStatus::Misses);
    std::string hint = pfbGuidanceText(d, m, 140.0);
    REQUIRE(!hint.empty());
    REQUIRE(hint.find("K") != std::string::npos);

    // A target 6 dB above achieved is Within10Db.
    REQUIRE(compareRejection(m.adjacent_rejection_db, achieved + 6.0) ==
            RejectionStatus::Within10Db);
}
