#include "attenuator_engine.h"
#include "combiner_engine.h"
#include "equalizer_engine.h"
#include "ideal_filter_engine.h"
#include "node_graph_engine.h"
#include "signal_generator_engine.h"
#include "spectrum.h"
#include "test_temp_paths.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <string>

using Catch::Approx;

static std::filesystem::path write_sparam_file(const std::string &name, int ports) {
    auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream file(path);
    file << "# GHz S RI R 50\n";
    for (double frequency : {0.1, 2.0}) {
        double magnitude = frequency == 0.1 ? 0.2 : 0.8;
        double real = magnitude * std::cos(std::numbers::pi / 6.0);
        double imag = magnitude * std::sin(std::numbers::pi / 6.0);
        file << frequency;
        const int pairs = ports * ports;
        for (int pair = 0; pair < pairs; ++pair) {
            // 2-port is column-major (S21 at pair 1); 3-port is row-major (S31, S32 at pairs 6, 7).
            const bool transfer = ports == 2 ? pair == 1 : (pair == 6 || pair == 7);
            file << (transfer ? " " + std::to_string(real) + " " + std::to_string(imag) : " 0 0");
        }
        file << '\n';
    }
    return path;
}

static Spectrum symmetric_spectrum() {
    Spectrum input;
    input.frequencies = {-1e9, 1e9};
    input.tones = {{-1e9, -10.0, 0.0}, {1e9, -10.0, 0.0}};
    input.noise_W = {1e-20, 1e-20};
    input.noise_added_W = {0.0, 0.0};
    input.noise_total_W = {1e-20, 1e-20};
    input.phase_deg = {0.0, 0.0};
    return input;
}

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

TEST_CASE("S-parameter engines use conjugate responses on symmetric frequencies",
          "[sparam][issue266]") {
    const auto s2p = write_sparam_file("issue266_slope.s2p", 2);
    const auto s3p = write_sparam_file("issue266_slope.s3p", 3);
    const auto input = symmetric_spectrum();

    auto check_response = [](const char *engine, const Spectrum &output, size_t offset = 0) {
        INFO(engine);
        REQUIRE(output.tones.size() >= offset + 2);
        REQUIRE(output.tones[offset].power_dBm == Approx(output.tones[offset + 1].power_dBm));
        REQUIRE(output.tones[offset].phase_deg == Approx(-output.tones[offset + 1].phase_deg));
        const double expected_magnitude = 0.2 + 0.6 * 0.9 / 1.9;
        REQUIRE(output.tones[offset + 1].power_dBm ==
                Approx(-10.0 + 20.0 * std::log10(expected_magnitude)).margin(0.01));
        REQUIRE(output.tones[offset].phase_deg == Approx(-30.0).margin(0.01));
        REQUIRE(output.tones[offset + 1].phase_deg == Approx(30.0).margin(0.01));
        REQUIRE(output.noise_total_W.size() == 2);
        REQUIRE(output.noise_total_W[0] == Approx(output.noise_total_W[1]));
    };

    NodeGraphEngine filter_graph;
    IdealFilterEngine filter(0, filter_graph);
    filter.setSParamFilepath(s2p.string());
    filter.node().inputs[0] = &input;
    filter.update(0.0);
    check_response("filter", filter.node().outputs[0]);

    NodeGraphEngine equalizer_graph;
    EqualizerEngine equalizer(0, equalizer_graph);
    equalizer.setSParamFilepath(s2p.string());
    equalizer.node().inputs[0] = &input;
    equalizer.update(0.0);
    check_response("equalizer", equalizer.node().outputs[0]);

    NodeGraphEngine attenuator_graph;
    AttenuatorEngine attenuator(0, attenuator_graph);
    attenuator.setSParamFilepath(s2p.string());
    attenuator.node().inputs[0] = &input;
    attenuator.update(0.0);
    check_response("attenuator", attenuator.node().outputs[0]);

    NodeGraphEngine combiner_graph;
    CombinerEngine combiner(0, combiner_graph);
    combiner.setSParamFilepath(s3p.string());
    combiner.setSParamMode(true);
    combiner.node().inputs[0] = &input;
    combiner.node().inputs[1] = &input;
    combiner.update(0.0);
    check_response("combiner0", combiner.node().outputs[0], 0);
    check_response("combiner2", combiner.node().outputs[0], 2);
}
