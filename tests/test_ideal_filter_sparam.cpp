#include "ideal_filter_engine.h"
#include "node_graph_engine.h"
#include "signal_generator_engine.h"
#include "test_temp_paths.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>

using Catch::Approx;

static std::string s2p_path() {
    return std::string(PROJECT_SOURCE_DIR) + "/component_data/amplifiers/adm-3844psm/"
                                             "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
}

TEST_CASE("IdealFilter ideal mode unchanged after S-param refactor", "[filter][sparam]") {
    NodeGraphEngine graph;
    IdealFilterEngine flt(0, graph);
    flt.setFilterType(FilterType::LPF);
    flt.setCutoff_Hz(200e6);

    SignalGeneratorEngine gen(1, graph);
    gen.addTone(100e6, -10.0); // in passband
    gen.addTone(300e6, -10.0); // in stopband
    gen.update(0.0);

    flt.node().inputs[0] = &gen.node().outputs[0];
    flt.update(0.0);

    const auto &out = flt.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    REQUIRE(out.tones[0].freq_Hz == Approx(100e6));
}

TEST_CASE("IdealFilter S-param mode loads file and applies S21", "[filter][sparam]") {
    NodeGraphEngine graph;
    IdealFilterEngine flt(0, graph);
    flt.setSParamFilepath(s2p_path());

    REQUIRE(flt.sparamLoaded());
    REQUIRE(flt.sparamMode());

    SignalGeneratorEngine gen(1, graph);
    gen.addTone(1e9, -20.0);
    gen.update(0.0);

    flt.node().inputs[0] = &gen.node().outputs[0];
    flt.update(0.0);

    const auto &out = flt.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    auto S21 = flt.sparamData().interpolate(1e9, 2);
    double expected_gain = 20.0 * std::log10(std::abs(S21));
    REQUIRE(out.tones[0].power_dBm == Approx(-20.0 + expected_gain).margin(0.5));
}

// Flat -3 dB through path: S21 = S12 = 10^(-3/20), S11 = S22 = 0.
static std::filesystem::path writeFlatMinus3dBS2p() {
    const auto path = std::filesystem::temp_directory_path() /
                      ("flat_minus3db_filter_" + test_temp_paths::processTag() + ".s2p");
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "# HZ S RI R 50\n"
           "1000000 0 0 0.707946 0 0.707946 0 0 0\n"
           "10000000000 0 0 0.707946 0 0.707946 0 0 0\n";
    return path;
}

TEST_CASE("IdealFilter S-param noise keeps the passive thermal term", "[filter][sparam][noise]") {
    // Passive model: noise_total = |S21|^2 * noise_in + kT * (1 - |S21|^2).
    // A flat -3 dB file on a kT input must return kT, as the attenuator does.
    const auto path = writeFlatMinus3dBS2p();
    NodeGraphEngine graph;
    IdealFilterEngine flt(0, graph);
    flt.setSParamFilepath(path.string());
    std::filesystem::remove(path);
    REQUIRE(flt.sparamMode());

    SignalGeneratorEngine gen(1, graph);
    gen.addTone(1e9, -30.0); // a tone makes the generator dirty; noise is kT regardless
    gen.update(0.0);
    const auto &in = gen.node().outputs[0];
    constexpr double kT = 1.3806e-23 * 290.0;
    REQUIRE(in.noise_total_W[0] == Approx(kT).epsilon(1e-6));

    flt.node().inputs[0] = &in;
    flt.update(0.0);

    const auto &out = flt.node().outputs[0];
    REQUIRE(!out.noise_total_W.empty());
    REQUIRE(out.noise_total_W.size() == in.noise_total_W.size());
    for (size_t i = 0; i < out.noise_total_W.size(); ++i)
        REQUIRE(out.noise_total_W[i] == Approx(in.noise_total_W[i]).epsilon(1e-6));
}
