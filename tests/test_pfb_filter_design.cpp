#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include "node_graph_engine.h"
#include "pfb_channelizer_engine.h"
#include "pfb_filter_design.h"
#include "power_meter_engine.h"

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
    REQUIRE(pfbGuidanceText(d, m, achieved, PfbRecommendation{}).empty());

    // A much harder target must produce a non-empty hint.
    REQUIRE(compareRejection(m.adjacent_rejection_db, 140.0) == RejectionStatus::Misses);
    const PfbRecommendation rec = pfbRecommend(d, m);
    REQUIRE(!pfbGuidanceText(d, m, 140.0, rec).empty());

    // Issue #289: at K=8, beta=8 the hint must not send the user to a beta that
    // lowers adjacent rejection (beta=20 gives H(1) = -55.9 dB against -83.2 dB).
    // The single suggested change must lower |H(1)|, and its beta must be the best
    // of the checked values 0, 4, ..., 20 at K=8.
    auto h1_db = [](const PfbFilterDesign &x) { return 20.0 * std::log10(x.responseAt(1.0)); };
    REQUIRE(rec.searched);
    REQUIRE(rec.beta.has_value());
    REQUIRE(!rec.taps_per_branch.has_value());
    const double suggested_db = h1_db(PfbFilterDesign(32, 8, *rec.beta));
    REQUIRE(suggested_db < m.adjacent_rejection_db);
    for (int b = 0; b <= 20; b += 4)
        REQUIRE(h1_db(PfbFilterDesign(32, 8, b)) >= suggested_db - 1e-9);
    REQUIRE(pfbGuidanceText(d, m, 140.0, rec).find("beta (8.0 -> 12.0)") != std::string::npos);

    // A target 6 dB above achieved is Within10Db.
    REQUIRE(compareRejection(m.adjacent_rejection_db, achieved + 6.0) ==
            RejectionStatus::Within10Db);

    // Exact lower boundary: achieved == target_db - 10 is still Within10Db.
    REQUIRE(compareRejection(m.adjacent_rejection_db, achieved + 10.0) ==
            RejectionStatus::Within10Db);
}
TEST_CASE("PFB guidance never suggests a change that worsens adjacent rejection",
          "[pfb_filter_design]") {
    // Sweep: any single change shown must lower |H(1)| when applied alone, and the text
    // must say "None of the checked" exactly when nothing is shown. The text never claims
    // that larger K values fail, since only the checked candidates were evaluated.
    auto h1_db = [](const PfbFilterDesign &x) { return 20.0 * std::log10(x.responseAt(1.0)); };
    for (int M : {4, 8, 32})
        for (int K = 1; K <= 16; ++K)
            for (double beta : {0.0, 8.0, 9.0, 20.0}) {
                const PfbFilterDesign d(M, K, beta);
                const auto m = computePfbMetrics(d);
                const auto rec = pfbRecommend(d, m);
                const std::string text = pfbGuidanceText(d, m, 140.0, rec);
                if (compareRejection(m.adjacent_rejection_db, 140.0) == RejectionStatus::Meets) {
                    REQUIRE(text.empty()); // target met: no hint at all
                    continue;
                }
                REQUIRE(rec.searched);
                REQUIRE(!(rec.taps_per_branch.has_value() && rec.beta.has_value()));
                if (rec.taps_per_branch.has_value()) {
                    REQUIRE(h1_db(PfbFilterDesign(M, *rec.taps_per_branch, beta)) <
                            m.adjacent_rejection_db);
                } else if (rec.beta.has_value()) {
                    REQUIRE(h1_db(PfbFilterDesign(M, K, *rec.beta)) < m.adjacent_rejection_db);
                } else {
                    REQUIRE(text.find("None of the checked") != std::string::npos);
                }
                REQUIRE(text.find("No larger") == std::string::npos);
            }
}

TEST_CASE("PFB guidance clamps K suggestion to the slider maximum", "[pfb_filter_design]") {
    // K=64 is the slider maximum: no "more K" suggestion, and the text never says "more".
    PfbFilterDesign top(32, 64, 0.0);
    auto m = computePfbMetrics(top);
    REQUIRE(compareRejection(m.adjacent_rejection_db, 140.0) == RejectionStatus::Misses);
    const PfbRecommendation rec = pfbRecommend(top, m);
    REQUIRE(!rec.taps_per_branch.has_value());
    const std::string hint = pfbGuidanceText(top, m, 140.0, rec);
    REQUIRE(!hint.empty());
    REQUIRE(hint.find("more") == std::string::npos);
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
namespace {
constexpr double kBoltzmannT_W_per_Hz = 1.380649e-23 * 290.0;

// Continuous ENBW in Hz: D * int_{-1}^{1} |H(x)|^2 dx, with D the channel width.
double continuousEnbwHz(const PfbFilterDesign &design, double channel_bw_Hz) {
    constexpr int steps = 8000;
    double acc = 0.0;
    for (int i = 0; i < steps; ++i) {
        const double h = design.responseAt(-1.0 + (i + 0.5) * 2.0 / steps);
        acc += h * h;
    }
    return channel_bw_Hz * acc * 2.0 / steps;
}

// 10 MHz input grid spanning -500..+500 MHz (101 bins), flat kT noise.
void loadTenMhzKtGrid(Spectrum &in) {
    for (int i = 0; i <= 100; ++i)
        in.frequencies.push_back(-500e6 + i * 10e6);
    in.noise_total_W.assign(in.frequencies.size(), kBoltzmannT_W_per_Hz);
}
} // namespace

TEST_CASE("PFB channel ENBW matches continuous noise integration on a fine grid",
          "[pfb_filter_design][enbw]") {
    NodeGraphEngine graph;
    PFBChannelizerEngine pfb(0, graph);

    Spectrum in;
    in.frequencies.resize(401);
    for (int i = 0; i < 401; ++i)
        in.frequencies[i] = -200e6 + i * 1e6;
    constexpr double input_psd_W_per_Hz = 1e-20;
    in.noise_total_W.assign(in.frequencies.size(), input_psd_W_per_Hz);

    pfb.setFs_Hz(400e6);
    pfb.node().inputs[0] = &in;
    pfb.update(0.0);

    const auto &critical = pfb.channels().at(16);
    const double expected_enbw_Hz = continuousEnbwHz(PfbFilterDesign(32, 8, 8.0), 400e6 / 32.0);
    REQUIRE(critical.enbw_Hz == Approx(expected_enbw_Hz).epsilon(1e-3));
    REQUIRE(critical.noise_W == Approx(input_psd_W_per_Hz * critical.enbw_Hz).epsilon(1e-12));
    const double critical_enbw_Hz = critical.enbw_Hz;

    pfb.setSamplingRatio(2);
    pfb.update(0.0);
    const auto &oversampled = pfb.channels().at(16);
    REQUIRE(oversampled.enbw_Hz > 1.8 * critical_enbw_Hz);
    REQUIRE(oversampled.noise_W == Approx(input_psd_W_per_Hz * oversampled.enbw_Hz).epsilon(1e-12));
}

TEST_CASE("PFB channel ENBW is grid-independent at M=64 on a 10 MHz grid",
          "[pfb_filter_design][enbw][grid]") {
    NodeGraphEngine graph;
    PFBChannelizerEngine pfb(0, graph);

    Spectrum in;
    loadTenMhzKtGrid(in);
    pfb.setFs_Hz(1e9);
    pfb.setChannelCount(64);
    pfb.node().inputs[0] = &in;
    pfb.update(0.0);

    const double expected_enbw_Hz = continuousEnbwHz(PfbFilterDesign(64, 8, 8.0), 1e9 / 64.0);
    REQUIRE(pfb.channels().size() == 64);
    for (const auto &ch : pfb.channels())
        REQUIRE(ch.enbw_Hz == Approx(expected_enbw_Hz).epsilon(0.01));
}

TEST_CASE("PFB centre-channel noise at M=128 on a 10 MHz grid tracks kT*Fs/M",
          "[pfb_filter_design][enbw][grid]") {
    NodeGraphEngine graph;
    PFBChannelizerEngine pfb(0, graph);

    Spectrum in;
    loadTenMhzKtGrid(in);
    pfb.setFs_Hz(1e9);
    pfb.setChannelCount(128);
    pfb.setActiveChannel(64);
    pfb.node().inputs[0] = &in;
    pfb.update(0.0);

    const auto &active = pfb.channels().at(64);
    const double ideal_dBm = 10.0 * std::log10(kBoltzmannT_W_per_Hz * 1e9 / 128.0) + 30.0;
    REQUIRE(active.noise_W > 0.0);
    REQUIRE(10.0 * std::log10(active.noise_W) + 30.0 == Approx(ideal_dBm).margin(1.0));
}

TEST_CASE("PFB M=256 channel readout is finite only when PowerMeter accepts it",
          "[pfb_filter_design][enbw][grid]") {
    NodeGraphEngine graph;
    PFBChannelizerEngine pfb(0, graph);

    Spectrum in;
    loadTenMhzKtGrid(in);
    pfb.setFs_Hz(1e9);
    pfb.setChannelCount(256);
    pfb.setActiveChannel(128);
    pfb.node().inputs[0] = &in;
    pfb.update(0.0);

    const double noise_W = pfb.channels().at(128).noise_W;
    const bool readout_finite = std::isfinite(noise_W) && noise_W > 0.0;
    const PowerMeasurement pm = PowerMeterEngine{}.measure(&pfb.node().outputs[0]);
    REQUIRE(readout_finite == pm.valid);

    // Full-band output must not silently read zero: where no channel has a usable
    // estimate, every bin is unavailable (NaN) and PowerMeter rejects the spectrum.
    const auto &full_band = pfb.node().outputs[1];
    REQUIRE(full_band.noise_total_W.size() == in.frequencies.size());
    for (double density : full_band.noise_total_W)
        REQUIRE(std::isnan(density));
    REQUIRE_FALSE(PowerMeterEngine{}.measure(&full_band).valid);
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

TEST_CASE("PFB tone index handles disjoint generations without saturation",
          "[pfb_filter_design][performance]") {
    NodeGraphEngine graph;
    PFBChannelizerEngine pfb(0, graph);
    pfb.setChannelCount(8);
    pfb.setFs_Hz(200e6);

    Spectrum in;
    in.frequencies.resize(401);
    for (int i = 0; i < 401; ++i)
        in.frequencies[i] = -100e6 + i * 0.5e6;
    in.noise_total_W.assign(in.frequencies.size(), 1e-20);
    pfb.node().inputs[0] = &in;

    for (int generation = 0; generation < 3; ++generation) {
        in.tones.clear();
        for (int i = 0; i < 8; ++i)
            in.tones.push_back({-87e6 + i * 25e6 + generation * 0.25e6, -30.0, 0.0});
        in.bumpGeneration();
        pfb.update(0.0);

        REQUIRE(pfb.node().outputs[1].tones.size() == in.tones.size());
    }
}

TEST_CASE("PfbFilterDesign responseAt matches the DTFT definition", "[pfb_filter_design]") {
    constexpr double kPi = 3.14159265358979323846;
    // Issue #130: responseAt uses a phasor recurrence instead of a cos() per
    // tap. Compare against the full complex DTFT sum over every tap, which is
    // the definition, for several (M, K, beta) and offsets, including the long
    // filter where the recurrence re-anchors many times.
    struct Case {
        int M;
        int K;
        double beta;
    };
    const Case cases[] = {
        {32, 8, 8.0}, {32, 16, 12.0}, {256, 8, 8.0}, {2048, 8, 8.0}, {2048, 64, 8.0}};
    const double xs[] = {0.0, 0.25, 0.5, 1.0, 1.37, 2.0, -0.8, -1.5};
    for (const auto &c : cases) {
        PfbFilterDesign d(c.M, c.K, c.beta);
        const auto &taps = d.taps();
        const int N = static_cast<int>(taps.size());
        for (double x : xs) {
            const double w = 2.0 * kPi * x / c.M;
            double re = 0.0;
            double im = 0.0;
            for (int n = 0; n < N; ++n) {
                re += taps[n] * std::cos(w * n);
                im -= taps[n] * std::sin(w * n);
            }
            const double reference = std::sqrt(re * re + im * im);
            INFO("M=" << c.M << " K=" << c.K << " x=" << x);
            REQUIRE(d.responseAt(x) == Approx(reference).margin(1e-10));
        }
    }
}
