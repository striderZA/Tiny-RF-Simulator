#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "node_graph_engine.h"
#include "pfb_channelizer_engine.h"
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
    REQUIRE(hint.find("beta") != std::string::npos);

    // A target 6 dB above achieved is Within10Db.
    REQUIRE(compareRejection(m.adjacent_rejection_db, achieved + 6.0) ==
            RejectionStatus::Within10Db);

    // Exact lower boundary: achieved == target_db - 10 is still Within10Db.
    REQUIRE(compareRejection(m.adjacent_rejection_db, achieved + 10.0) ==
            RejectionStatus::Within10Db);
}

TEST_CASE("PFB engine tiles flat noise (regression vs narrow model)", "[pfb_filter_design]") {
    NodeGraphEngine graph;
    PFBChannelizerEngine pfb(0, graph);

    Spectrum in;
    in.frequencies.resize(401);
    for (int i = 0; i < 401; ++i)
        in.frequencies[i] = -200e6 + i * 1e6;
    in.noise_total_W.assign(401, 1e-20);

    pfb.setFs_Hz(400e6);
    pfb.node().inputs[0] = &in;
    pfb.update(0.0);

    const auto &out = pfb.node().outputs[1];
    REQUIRE(out.noise_W.size() == in.frequencies.size());
    double sum = 0.0;
    for (double v : out.noise_W)
        sum += v;
    const double mean = sum / out.noise_W.size();
    // Corrected prototype tiles to within ~1 dB of the input PSD.
    // The old narrow model produced ~0.12 * 1e-20 and fails this bound.
    REQUIRE(mean > 0.5e-20);
    REQUIRE(mean < 1.5e-20);
}

TEST_CASE("PFB engine tone weights match the shared prototype", "[pfb_filter_design]") {
    NodeGraphEngine graph;
    PFBChannelizerEngine pfb(0, graph); // defaults M=32, K=8, beta=8

    Spectrum in;
    in.frequencies.resize(401);
    for (int i = 0; i < 401; ++i)
        in.frequencies[i] = -200e6 + i * 1e6;
    in.noise_total_W.assign(401, 1e-20);

    pfb.setFs_Hz(400e6); // channel_bw = 12.5 MHz; ch16 center = 6.25 MHz
    in.tones.push_back({6.25e6, -30.0, 0.0});
    pfb.node().inputs[0] = &in;
    pfb.update(0.0);

    const auto &chs = pfb.channels();
    // Center tone passes at unity gain...
    REQUIRE(chs[16].tones.size() == 1);
    REQUIRE(chs[16].tones[0].power_dBm == Approx(-30.0).margin(0.5));
    // ...and leaks into the adjacent channels at the prototype's |H(1.0)|
    // (~ -83 dB relative to the tone, i.e. ~ -113 dBm here), not at the old
    // model's exact -300 dB null.
    REQUIRE(chs[15].tones.size() == 1);
    REQUIRE(chs[15].tones[0].power_dBm > -118.0);
    REQUIRE(chs[15].tones[0].power_dBm < -108.0);
}

TEST_CASE("PFB deserialize clamps filter parameters", "[pfb_filter_design]") {
    NodeGraphEngine graph;
    PFBChannelizerEngine pfb(0, graph);

    pfb.deserialize({{"taps_per_branch", 0}, {"kaiser_beta", 99.0}});

    REQUIRE(pfb.tapsPerBranch() == 1);
    REQUIRE(pfb.kaiserBeta() == Approx(20.0));
}
TEST_CASE("PFB full-band output deduplicates tones across overlapping channels",
          "[pfb_filter_design]") {
    NodeGraphEngine graph;
    PFBChannelizerEngine pfb(0, graph); // defaults K=8, beta=8
    pfb.setChannelCount(16);
    pfb.setFs_Hz(200e6); // channel_bw = 12.5 MHz; ch8 center = 6.25 MHz

    Spectrum in;
    in.frequencies.resize(201);
    for (int i = 0; i < 201; ++i)
        in.frequencies[i] = -100e6 + i * 1e6;
    in.noise_total_W.assign(201, 1e-20);

    // T1 sits between ch7 (-6.25 MHz) and ch8 (6.25 MHz); ch8's filter weight
    // (offset 0.25 ch) is stronger than ch7's (0.75 ch).
    in.tones.push_back({3.125e6, -30.0, 0.0});
    // T2 sits at ch8's centre (offset 0 ch) and leaks into ch7/ch9 at 1.0 ch.
    in.tones.push_back({6.25e6, -30.0, 0.0});
    // T3a/T3b share one frequency (ch8/ch9 boundary at offset 0.5 ch) but
    // differ in power; the duplicate-frequency input tones must collapse into
    // a single output tone carrying the stronger one's filtered power.
    in.tones.push_back({12.5e6, -30.0, 0.0});
    in.tones.push_back({12.5e6, -20.0, 0.0});

    pfb.node().inputs[0] = &in;
    pfb.update(0.0);

    PfbFilterDesign proto(16, 8, 8.0);
    auto filtered = [&proto](double power_dBm, double norm_offset) {
        double w = proto.responseAt(norm_offset);
        return 10.0 * std::log10(std::pow(10.0, power_dBm / 10.0) * w * w + 1e-300);
    };

    const auto &out = pfb.node().outputs[1];
    // Every frequency appears exactly once, ordered by first-appearance
    // channel (ch7's tones precede ch8's), with the strongest filtered
    // representation kept at that first position.
    REQUIRE(out.tones.size() == 3);
    REQUIRE(out.tones[0].freq_Hz == Approx(3.125e6).margin(1e-6));
    REQUIRE(out.tones[1].freq_Hz == Approx(6.25e6).margin(1e-6));
    REQUIRE(out.tones[2].freq_Hz == Approx(12.5e6).margin(1e-6));

    // T1 keeps ch8's stronger representation (found two channels later and
    // replaced in place), not ch7's first, weaker one.
    REQUIRE(out.tones[0].power_dBm == Approx(filtered(-30.0, 3.125e6 / 12.5e6)).margin(1e-9));
    REQUIRE(out.tones[0].power_dBm > filtered(-30.0, 9.375e6 / 12.5e6));
    // T2's centre-channel pass (w = 1) dominates its 1.0-ch edge leaks.
    REQUIRE(out.tones[1].power_dBm == Approx(-30.0).margin(1e-9));
    // T3a/T3b collapse to T3b's stronger filtered power.
    REQUIRE(out.tones[2].power_dBm == Approx(filtered(-20.0, 6.25e6 / 12.5e6)).margin(1e-9));
}

TEST_CASE("PFB full-band output deduplicates late in the channel sweep", "[pfb_filter_design]") {
    // Regression: dedupe must find an earlier occurrence of a frequency even
    // when many unrelated tones were appended in between (the duplicate's
    // first appearance is several channel runs back, not adjacent).
    NodeGraphEngine graph;
    PFBChannelizerEngine pfb(0, graph); // defaults K=8, beta=8
    pfb.setChannelCount(8);
    pfb.setFs_Hz(200e6); // channel_bw = 25 MHz; ch4 center = 6.25 MHz

    Spectrum in;
    in.frequencies.resize(201);
    for (int i = 0; i < 201; ++i)
        in.frequencies[i] = -100e6 + i * 1e6;
    in.noise_total_W.assign(201, 1e-20);
    in.tones.push_back({0.0, -30.0, 0.0}); // ch3/ch4 boundary (offset 0.5 ch)
    for (int i = 1; i <= 30; ++i)
        in.tones.push_back({static_cast<double>(i) * 1e6, -50.0, 0.0}); // spread tones

    pfb.node().inputs[0] = &in;
    pfb.update(0.0);

    const auto &out = pfb.node().outputs[1];
    REQUIRE(out.tones.size() == 31); // no duplicate frequencies survive
    std::vector<double> freqs;
    freqs.reserve(out.tones.size());
    for (const auto &t : out.tones)
        freqs.push_back(t.freq_Hz);
    std::sort(freqs.begin(), freqs.end());
    for (size_t i = 1; i < freqs.size(); ++i)
        REQUIRE(freqs[i] > freqs[i - 1]); // strictly increasing => globally unique
}
