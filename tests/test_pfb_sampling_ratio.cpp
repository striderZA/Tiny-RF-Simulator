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

TEST_CASE("PFB oversampling scales usable bandwidth but not the channel grid",
          "[pfb][sampling_ratio]") {
    NodeGraphEngine graph;
    PFBChannelizerEngine pfb(0, graph);
    auto input = makeInput(400e6);
    pfb.node().inputs[0] = &input;
    pfb.update(0.0);

    std::vector<double> critical_centers;
    std::vector<double> critical_bandwidths;
    critical_centers.reserve(pfb.channels().size());
    critical_bandwidths.reserve(pfb.channels().size());
    for (const auto &ch : pfb.channels()) {
        critical_centers.push_back(ch.center_freq_Hz);
        critical_bandwidths.push_back(ch.bandwidth_Hz);
    }

    pfb.setSamplingRatio(2);
    pfb.update(0.0);
    REQUIRE(pfb.samplingRatio() == 2);
    REQUIRE(pfb.channels().size() == critical_centers.size());

    for (size_t i = 0; i < critical_centers.size(); ++i) {
        REQUIRE(pfb.channels()[i].center_freq_Hz == Approx(critical_centers[i]));
        REQUIRE(pfb.channels()[i].bandwidth_Hz == Approx(2.0 * critical_bandwidths[i]));
    }
}

TEST_CASE("PFB oversampling preserves full-band flat-noise density", "[pfb][sampling_ratio]") {
    NodeGraphEngine graph;
    PFBChannelizerEngine pfb(0, graph);
    auto input = makeInput(400e6);
    pfb.node().inputs[0] = &input;

    pfb.update(0.0);
    const double critical_psd = pfb.node().outputs[1].noise_total_W.at(200);
    REQUIRE(critical_psd > 0.0);

    pfb.setSamplingRatio(2);
    pfb.update(0.0);
    const double oversampled_psd = pfb.node().outputs[1].noise_total_W.at(200);

    REQUIRE(oversampled_psd / critical_psd == Approx(1.0).epsilon(0.05));
}

TEST_CASE("PFB oversampling widens the usable channel response", "[pfb][sampling_ratio]") {
    NodeGraphEngine graph;
    PFBChannelizerEngine pfb(0, graph);
    auto input = makeInput(400e6);
    input.tones.push_back({18.25e6, -30.0, 0.0});
    pfb.node().inputs[0] = &input;

    pfb.update(0.0);
    const double critical_tone = pfb.channels().at(16).tones.front().power_dBm;

    pfb.setSamplingRatio(2);
    pfb.update(0.0);
    const double oversampled_tone = pfb.channels().at(16).tones.front().power_dBm;

    REQUIRE(oversampled_tone > critical_tone + 20.0);
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
