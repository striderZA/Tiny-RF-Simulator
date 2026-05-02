#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "pfb_channelizer_engine.h"
#include "node_graph_engine.h"
#include "common.h"

using Catch::Approx;

TEST_CASE("PFB channelizer tone routing", "[pfb]") {
    NodeGraphEngine graph;
    PFBChannelizerEngine pfb(0, graph);

    Spectrum in;
    in.frequencies.resize(401);
    for (int i = 0; i < 401; ++i)
        in.frequencies[i] = -200e6 + i * 1e6;
    in.tones.push_back({0.0, -30.0, 0.0});
    in.noise_total_W.assign(401, 1e-20);

    pfb.node().inputs[0] = in;
    pfb.update(0.0);

    const auto& chs = pfb.channels();
    REQUIRE(chs.size() == 32);

    // M=32, Fs=400 MHz, channel_bw=12.5 MHz.
    // Ch 0 centre = -200 MHz. Ch 16 centre = 0 MHz.
    REQUIRE(chs[16].center_freq_Hz == Approx(0.0).margin(1.0));
    REQUIRE(chs[16].tones.size() == 1);
    REQUIRE(chs[16].tones[0].freq_Hz == Approx(0.0));

    REQUIRE(chs[0].tones.size() == 0);

    pfb.setActiveChannel(16);
    pfb.update(0.0);
    const auto& out = pfb.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    REQUIRE(out.tones[0].freq_Hz == Approx(0.0));
}

TEST_CASE("PFB channelizer noise distribution", "[pfb]") {
    NodeGraphEngine graph;
    PFBChannelizerEngine pfb(0, graph);

    Spectrum in;
    in.frequencies.resize(401);
    for (int i = 0; i < 401; ++i)
        in.frequencies[i] = -200e6 + i * 1e6;
    in.noise_total_W.assign(401, 1e-20);

    pfb.node().inputs[0] = in;
    pfb.update(0.0);

    for (const auto& ch : pfb.channels())
        REQUIRE(ch.noise_W > 0.0);
}

TEST_CASE("PFB channelizer no input produces empty output", "[pfb]") {
    NodeGraphEngine graph;
    PFBChannelizerEngine pfb(0, graph);

    Spectrum in;
    in.frequencies = {};
    pfb.node().inputs[0] = in;
    pfb.update(0.0);

    REQUIRE(pfb.node().outputs[0].frequencies.empty());
    REQUIRE(pfb.node().outputs[0].tones.empty());
}

TEST_CASE("PFB channelizer oversampling: tone at bin boundary", "[pfb]") {
    NodeGraphEngine graph;
    PFBChannelizerEngine pfb(0, graph);
    pfb.setChannelCount(16);

    Spectrum in;
    in.frequencies.resize(201);
    for (int i = 0; i < 201; ++i)
        in.frequencies[i] = -100e6 + i * 1e6;
    // Place tone at 0 Hz, which is ch 8 centre (= -100 + 8 * 200/16 = 0 MHz)
    in.tones.push_back({0.0, -30.0, 0.0});
    in.noise_total_W.assign(201, 1e-20);

    pfb.node().inputs[0] = in;
    pfb.update(0.0);

    const auto& ch7 = pfb.channels()[7];
    const auto& ch8 = pfb.channels()[8];

    REQUIRE(ch8.tones.size() == 1);
    REQUIRE(ch8.tones[0].power_dBm == Approx(-30.0).margin(1.0));

    // ch7 should also contain the tone (within ±1 channel_bw), but attenuated
    REQUIRE(ch7.tones.size() == 1);
    REQUIRE(ch7.tones[0].power_dBm < -30.0);
}
