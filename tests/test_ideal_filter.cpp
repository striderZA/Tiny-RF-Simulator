#include "common.h"
#include "ideal_filter_engine.h"
#include "node_graph_engine.h"
#include "signal_generator_engine.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

TEST_CASE("IdealFilter LPF passes tones below cutoff, blocks above", "[filter]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(50e6, -20.0);
    gen.addTone(150e6, -30.0);
    gen.update(0.0);

    IdealFilterEngine filt(0, graph);
    filt.setFilterType(FilterType::LPF);
    filt.setCutoff_Hz(100e6);
    filt.node().inputs[0] = &gen.node().outputs[0];
    filt.update(0.0);

    const auto &out = filt.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    REQUIRE(out.tones[0].freq_Hz == 50e6);
    REQUIRE(out.tones[0].power_dBm == Approx(-20.0));
}

TEST_CASE("IdealFilter HPF blocks tones below cutoff, passes above", "[filter]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(50e6, -20.0);
    gen.addTone(150e6, -30.0);
    gen.update(0.0);

    IdealFilterEngine filt(0, graph);
    filt.setFilterType(FilterType::HPF);
    filt.setCutoff_Hz(100e6);
    filt.node().inputs[0] = &gen.node().outputs[0];
    filt.update(0.0);

    const auto &out = filt.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    REQUIRE(out.tones[0].freq_Hz == 150e6);
    REQUIRE(out.tones[0].power_dBm == Approx(-30.0));
}

TEST_CASE("IdealFilter BPF passes tones in band, blocks outside", "[filter]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(30e6, -10.0);
    gen.addTone(75e6, -20.0);
    gen.addTone(150e6, -30.0);
    gen.update(0.0);

    IdealFilterEngine filt(0, graph);
    filt.setFilterType(FilterType::BPF);
    filt.setCutoffs_Hz(50e6, 100e6);
    filt.node().inputs[0] = &gen.node().outputs[0];
    filt.update(0.0);

    const auto &out = filt.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    REQUIRE(out.tones[0].freq_Hz == 75e6);
    REQUIRE(out.tones[0].power_dBm == Approx(-20.0));
}

TEST_CASE("IdealFilter BSF blocks tones in band, passes outside", "[filter]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(30e6, -10.0);
    gen.addTone(75e6, -20.0);
    gen.addTone(150e6, -30.0);
    gen.update(0.0);

    IdealFilterEngine filt(0, graph);
    filt.setFilterType(FilterType::BSF);
    filt.setCutoffs_Hz(50e6, 100e6);
    filt.node().inputs[0] = &gen.node().outputs[0];
    filt.update(0.0);

    const auto &out = filt.node().outputs[0];
    REQUIRE(out.tones.size() == 2);
    REQUIRE(out.tones[0].freq_Hz == 30e6);
    REQUIRE(out.tones[0].power_dBm == Approx(-10.0));
    REQUIRE(out.tones[1].freq_Hz == 150e6);
    REQUIRE(out.tones[1].power_dBm == Approx(-30.0));
}

TEST_CASE("IdealFilter LPF passes tone at exact cutoff", "[filter]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, -20.0);
    gen.update(0.0);

    IdealFilterEngine filt(0, graph);
    filt.setFilterType(FilterType::LPF);
    filt.setCutoff_Hz(100e6);
    filt.node().inputs[0] = &gen.node().outputs[0];
    filt.update(0.0);

    REQUIRE(filt.node().outputs[0].tones.size() == 1);
}

TEST_CASE("IdealFilter HPF blocks tone at exact cutoff", "[filter]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, -20.0);
    gen.update(0.0);

    IdealFilterEngine filt(0, graph);
    filt.setFilterType(FilterType::HPF);
    filt.setCutoff_Hz(100e6);
    filt.node().inputs[0] = &gen.node().outputs[0];
    filt.update(0.0);

    REQUIRE(filt.node().outputs[0].tones.empty());
}

TEST_CASE("IdealFilter passes noise density unchanged in passband", "[filter]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.update(0.0);

    IdealFilterEngine filt(0, graph);
    filt.setFilterType(FilterType::LPF);
    filt.setCutoff_Hz(300e6);
    filt.node().inputs[0] = &gen.node().outputs[0];
    filt.update(0.0);

    const auto &out = filt.node().outputs[0];
    const auto &in_ref = gen.node().outputs[0];
    REQUIRE(out.noise_total_W.size() == in_ref.noise_total_W.size());
    for (size_t i = 0; i < out.noise_total_W.size(); ++i) {
        if (out.frequencies[i] <= 300e6)
            REQUIRE(out.noise_total_W[i] == Approx(in_ref.noise_total_W[i]).epsilon(1e-30));
        else
            REQUIRE(out.noise_total_W[i] == Approx(0.0).epsilon(1e-30));
    }
}

TEST_CASE("IdealFilter preserves fs_Hz from input", "[filter]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, -20.0);
    gen.update(0.0);

    IdealFilterEngine filt(0, graph);
    filt.setFilterType(FilterType::LPF);
    filt.setCutoff_Hz(200e6);
    filt.node().inputs[0] = &gen.node().outputs[0];
    filt.update(0.0);

    REQUIRE(filt.node().outputs[0].fs_Hz == gen.node().outputs[0].fs_Hz);
}

TEST_CASE("IdealFilter dirty flag skips when input unchanged", "[filter]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, -20.0);
    gen.update(0.0);

    IdealFilterEngine filt(0, graph);
    filt.setFilterType(FilterType::LPF);
    filt.setCutoff_Hz(200e6);
    filt.node().inputs[0] = &gen.node().outputs[0];
    filt.update(0.0);

    uint64_t gen_after_first = filt.node().outputs[0].generation;
    auto &out = filt.node().outputs[0];
    size_t tone_count = out.tones.size();
    filt.update(0.0);
    REQUIRE(out.generation == gen_after_first);
    REQUIRE(out.tones.size() == tone_count);
}

TEST_CASE("IdealFilter with no input produces empty tones", "[filter][edge]") {
    NodeGraphEngine graph;
    IdealFilterEngine filt(0, graph);
    filt.update(0.0);
    REQUIRE(filt.node().outputs[0].tones.empty());
}

TEST_CASE("IdealFilter BSF passes noise outside stopband, blocks inside", "[filter]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.update(0.0);

    IdealFilterEngine filt(0, graph);
    filt.setFilterType(FilterType::BSF);
    filt.setCutoffs_Hz(100e6, 200e6);
    filt.node().inputs[0] = &gen.node().outputs[0];
    filt.update(0.0);

    const auto &out = filt.node().outputs[0];
    const auto &in_ref = gen.node().outputs[0];
    REQUIRE(out.noise_total_W.size() == in_ref.noise_total_W.size());
    for (size_t i = 0; i < out.noise_total_W.size(); ++i) {
        double f = out.frequencies[i];
        if (f < 100e6 || f > 200e6)
            REQUIRE(out.noise_total_W[i] == Approx(in_ref.noise_total_W[i]).epsilon(1e-30));
        else
            REQUIRE(out.noise_total_W[i] == Approx(0.0).epsilon(1e-30));
    }
}

TEST_CASE("IdealFilter preserves upstream added noise in passband", "[filter]") {
    NodeGraphEngine graph;
    IdealFilterEngine filt(0, graph);
    filt.setFilterType(FilterType::LPF);
    filt.setCutoff_Hz(300e6);

    // Drive from a synthetic spectrum whose noise_added_W is nonzero (e.g. an
    // amplifier upstream). The generator used by the older noise tests has
    // noise_added_W == 0, which is why dropping the added component was
    // invisible. For a 0 dB ideal filter the in-band noise_total must equal the
    // input's noise_total (input noise + upstream added noise).
    Spectrum in;
    in.frequencies.resize(401);
    for (int i = 0; i < 401; ++i)
        in.frequencies[i] = -200e6 + i * 1e6;
    in.noise_W.assign(401, 1e-20);
    in.noise_added_W.assign(401, 5e-21);
    in.noise_total_W.assign(401, 1.5e-20);

    filt.node().inputs[0] = &in;
    filt.update(0.0);

    const auto &out = filt.node().outputs[0];
    REQUIRE(out.noise_total_W.size() == in.noise_total_W.size());
    for (size_t i = 0; i < out.noise_total_W.size(); ++i) {
        if (out.frequencies[i] <= 300e6)
            REQUIRE(out.noise_total_W[i] == Approx(in.noise_total_W[i]).epsilon(1e-30));
        else
            REQUIRE(out.noise_total_W[i] == Approx(0.0).epsilon(1e-30));
    }
    // The ideal filter adds no noise of its own
    for (double v : out.noise_added_W)
        REQUIRE(v == Approx(0.0).epsilon(1e-30));
}
