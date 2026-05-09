#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "common.h"
#include "node_graph_engine.h"
#include "pfb_channelizer_engine.h"

using Catch::Approx;

TEST_CASE("PFB channelizer tone routing", "[pfb]") {
    NodeGraphEngine graph;
    PFBChannelizerEngine pfb(0, graph);

    Spectrum in;
    in.frequencies.resize(401);
    for (int i = 0; i < 401; ++i)
        in.frequencies[i] = -200e6 + i * 1e6;
    in.tones.push_back({6.25e6, -30.0, 0.0});
    in.noise_total_W.assign(401, 1e-20);

    pfb.setFs_Hz(400e6);
    pfb.node().inputs[0] = &in;
    pfb.update(0.0);

    const auto &chs = pfb.channels();
    REQUIRE(chs.size() == 32);

    // M=32, Fs=400 MHz, channel_bw=12.5 MHz.
    // Channel 16 centre = -200 + 6.25 + 16*12.5 = 6.25 MHz
    REQUIRE(chs[16].center_freq_Hz == Approx(6.25e6).margin(1.0));
    REQUIRE(chs[16].tones.size() == 1);
    REQUIRE(chs[16].tones[0].power_dBm == Approx(-30.0).margin(1.0));

    // Tone at ch 16 centre should NOT appear in distant channels
    REQUIRE(chs[0].tones.size() == 0);

    // Switching active channel forwards the right tone
    pfb.setActiveChannel(16);
    pfb.update(0.0);
    const auto &out = pfb.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
}

TEST_CASE("PFB channelizer noise distribution", "[pfb]") {
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

    for (const auto &ch : pfb.channels())
        REQUIRE(ch.noise_W > 0.0);
}

TEST_CASE("PFB channelizer no input produces empty output", "[pfb]") {
    NodeGraphEngine graph;
    PFBChannelizerEngine pfb(0, graph);

    Spectrum in;
    in.frequencies = {};
    pfb.node().inputs[0] = &in;
    pfb.update(0.0);

    REQUIRE(pfb.node().outputs[0].frequencies.empty());
    REQUIRE(pfb.node().outputs[0].tones.empty());
}

TEST_CASE("PFB channelizer oversampling: tone at bin boundary", "[pfb]") {
    NodeGraphEngine graph;
    PFBChannelizerEngine pfb(0, graph);
    pfb.setChannelCount(16);
    pfb.setFs_Hz(200e6);

    Spectrum in;
    in.frequencies.resize(201);
    for (int i = 0; i < 201; ++i)
        in.frequencies[i] = -100e6 + i * 1e6;
    // Tone at 0 Hz: between ch 7 centre (-6.25 MHz) and ch 8 centre (6.25 MHz)
    in.tones.push_back({0.0, -30.0, 0.0});
    in.noise_total_W.assign(201, 1e-20);

    pfb.node().inputs[0] = &in;
    pfb.update(0.0);

    const auto &ch7 = pfb.channels()[7];
    const auto &ch8 = pfb.channels()[8];

    // Both adjacent channels should see the tone (oversampling within ±1 channel_bw)
    REQUIRE(ch7.tones.size() == 1);
    REQUIRE(ch8.tones.size() == 1);

    // Both should be attenuated relative to centre frequency
    REQUIRE(ch7.tones[0].power_dBm < -30.0);
    REQUIRE(ch8.tones[0].power_dBm < -30.0);
}

TEST_CASE("PFB has two outputs with correct sizes", "[pfb]") {
    NodeGraphEngine graph;
    PFBChannelizerEngine pfb(0, graph);

    REQUIRE(pfb.node().outputs.size() == 2);

    Spectrum in;
    in.frequencies.resize(401);
    for (int i = 0; i < 401; ++i)
        in.frequencies[i] = -200e6 + i * 1e6;
    in.tones.push_back({6.25e6, -30.0, 0.0});
    in.noise_total_W.assign(401, 1e-20);

    pfb.setFs_Hz(400e6);
    pfb.setActiveChannel(16);
    pfb.node().inputs[0] = &in;
    pfb.update(0.0);

    const auto &out_active = pfb.node().outputs[0];
    REQUIRE(!out_active.frequencies.empty());
    REQUIRE(out_active.frequencies.size() < in.frequencies.size());
    REQUIRE(out_active.tones.size() == 1);

    const auto &out_full = pfb.node().outputs[1];
    REQUIRE(out_full.frequencies.size() == in.frequencies.size());
    REQUIRE(out_full.tones.size() >= 1);
}

TEST_CASE("PFB active channel query methods", "[pfb]") {
    NodeGraphEngine graph;
    PFBChannelizerEngine pfb(0, graph);
    pfb.setFs_Hz(400e6);

    Spectrum in;
    in.frequencies.resize(401);
    for (int i = 0; i < 401; ++i)
        in.frequencies[i] = -200e6 + i * 1e6;
    in.noise_total_W.assign(401, 1e-20);
    pfb.node().inputs[0] = &in;
    pfb.update(0.0);

    double bw = pfb.activeChannelBandwidth_Hz();
    REQUIRE(bw == Approx(12.5e6).margin(0.001));

    double center = pfb.activeChannelCenter_Hz();
    REQUIRE(center == Approx(-193.75e6).margin(1.0));

    pfb.setActiveChannel(16);
    pfb.update(0.0);
    double center16 = pfb.activeChannelCenter_Hz();
    REQUIRE(center16 == Approx(6.25e6).margin(1.0));
}

TEST_CASE("PFB outputs[1] noise floor flatness at overlap boundaries", "[pfb]") {
    NodeGraphEngine graph;
    PFBChannelizerEngine pfb(0, graph);
    pfb.setChannelCount(8);
    pfb.setFs_Hz(200e6);

    Spectrum in;
    const size_t n_bins = 401;
    in.frequencies.resize(n_bins);
    for (size_t i = 0; i < n_bins; ++i)
        in.frequencies[i] = -100e6 + static_cast<double>(i) * 0.5e6;
    in.noise_total_W.assign(n_bins, 1e-20);
    in.fs_Hz = 200e6;

    pfb.node().inputs[0] = &in;
    pfb.update(0.0);

    const auto &out = pfb.node().outputs[1];
    REQUIRE(out.noise_W.size() == n_bins);

    double sum = 0.0;
    double min_val = out.noise_W[0];
    double max_val = out.noise_W[0];
    for (size_t i = 0; i < n_bins; ++i) {
        double v = out.noise_W[i];
        REQUIRE(v > 0.0);
        sum += v;
        if (v < min_val)
            min_val = v;
        if (v > max_val)
            max_val = v;
    }
    double mean = sum / static_cast<double>(n_bins);
    double ripple_pct = 100.0 * (max_val - min_val) / mean;

    REQUIRE(ripple_pct < 1.0);
}

TEST_CASE("PFB channelizer recomputes channels when M increases", "[pfb]") {
    NodeGraphEngine graph;
    PFBChannelizerEngine pfb(0, graph);

    Spectrum in;
    in.frequencies.resize(401);
    for (int i = 0; i < 401; ++i)
        in.frequencies[i] = -200e6 + i * 1e6;
    in.noise_total_W.assign(401, 1e-20);

    // Start with M=32
    pfb.setFs_Hz(400e6);
    pfb.node().inputs[0] = &in;
    pfb.update(0.0);
    REQUIRE(pfb.channels().size() == 32);

    // Increase to M=128
    pfb.setChannelCount(128);
    pfb.setActiveChannel(100);
    pfb.update(0.0);

    // After fix: channels should be resized to 128
    REQUIRE(pfb.channels().size() == 128);
    // Channel 100 should have valid geometry
    REQUIRE(pfb.channels()[100].center_freq_Hz != 0.0);
    REQUIRE(pfb.channels()[100].bandwidth_Hz > 0.0);
    // Output should also be valid
    const auto& out = pfb.node().outputs[0];
    REQUIRE(out.tones.size() <= 1);
    REQUIRE(out.frequencies.size() > 0);
}
