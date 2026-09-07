#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "node_graph_engine.h"
#include "pfb_channelizer_engine.h"

using Catch::Approx;

namespace {
Spectrum makeInput(double fs_Hz) {
    Spectrum input;
    input.frequencies.resize(401);
    for (int i = 0; i < 401; ++i)
        input.frequencies[i] = -fs_Hz / 2.0 + static_cast<double>(i) * fs_Hz / 400.0;
    input.noise_total_W.assign(input.frequencies.size(), 1e-20);
    input.fs_Hz = fs_Hz;
    return input;
}
} // namespace

TEST_CASE("PFB defaults to critically sampled output", "[pfb][sampling_ratio]") {
    NodeGraphEngine graph;
    PFBChannelizerEngine pfb(0, graph);
    auto input = makeInput(400e6);
    pfb.node().inputs[0] = &input;
    pfb.update(0.0);

    REQUIRE(pfb.samplingRatio() == 1);
    REQUIRE(pfb.outputFs_Hz() == Approx(12.5e6));
    REQUIRE(pfb.activeChannelBandwidth_Hz() == Approx(12.5e6));
    REQUIRE(pfb.node().outputs[0].fs_Hz == Approx(12.5e6));
    REQUIRE(pfb.node().outputs[1].fs_Hz == Approx(400e6));
}

TEST_CASE("PFB ratio two doubles channel output rate and bandwidth", "[pfb][sampling_ratio]") {
    NodeGraphEngine graph;
    PFBChannelizerEngine pfb(0, graph);
    auto input = makeInput(400e6);
    pfb.node().inputs[0] = &input;
    pfb.update(0.0);

    pfb.setSamplingRatio(2);
    pfb.update(0.0);

    REQUIRE(pfb.samplingRatio() == 2);
    REQUIRE(pfb.outputFs_Hz() == Approx(25e6));
    REQUIRE(pfb.activeChannelBandwidth_Hz() == Approx(25e6));
    REQUIRE(pfb.channels()[16].center_freq_Hz == Approx(6.25e6).margin(1.0));
    REQUIRE(pfb.channels()[17].center_freq_Hz - pfb.channels()[16].center_freq_Hz ==
            Approx(12.5e6).margin(1.0));
    REQUIRE(pfb.node().outputs[0].fs_Hz == Approx(25e6));
}

TEST_CASE("PFB sampling ratio accepts only one or two", "[pfb][sampling_ratio]") {
    NodeGraphEngine graph;
    PFBChannelizerEngine pfb(0, graph);

    pfb.setSamplingRatio(0);
    REQUIRE(pfb.samplingRatio() == 1);
    pfb.setSamplingRatio(7);
    REQUIRE(pfb.samplingRatio() == 2);
}

TEST_CASE("PFB sampling ratio is persisted with legacy default", "[pfb][sampling_ratio]") {
    NodeGraphEngine graph;
    PFBChannelizerEngine pfb(0, graph);
    pfb.setSamplingRatio(2);
    const auto saved = pfb.serialize();
    REQUIRE(saved.at("sampling_ratio") == 2);

    PFBChannelizerEngine restored(1, graph);
    restored.deserialize(saved);
    REQUIRE(restored.samplingRatio() == 2);

    PFBChannelizerEngine legacy(2, graph);
    legacy.deserialize(nlohmann::json::object());
    REQUIRE(legacy.samplingRatio() == 1);

    PFBChannelizerEngine invalid(3, graph);
    invalid.deserialize({{"sampling_ratio", 99}});
    REQUIRE(invalid.samplingRatio() == 2);
}
