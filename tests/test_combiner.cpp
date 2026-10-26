#include "combiner_engine.h"
#include "node_graph_engine.h"
#include "s_parameter_data.h"
#include "spectrum.h"
#include "test_temp_paths.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <string>

using Catch::Matchers::WithinAbs;

namespace {

Spectrum buildTestSpectrum(double freq_Hz, double power_dBm, double phase_deg) {
    Spectrum s;
    s.frequencies = {1e9, 2e9, 3e9};
    s.tones = {{freq_Hz, power_dBm, phase_deg}};
    s.noise_W.assign(3, 1e-21);
    s.noise_added_W.assign(3, 0.0);
    s.noise_total_W.assign(3, 1e-21);
    s.phase_deg.assign(3, 0.0);
    s.fs_Hz = 1e9;
    return s;
}

std::filesystem::path writeFlatMinus3dBS3p() {
    const auto path = std::filesystem::temp_directory_path() /
                      ("flat_minus3db_" + test_temp_paths::processTag() + ".s3p");
    std::ofstream file(path);
    // The combiner reads pairs 6 and 7 for its two input paths; each is 1/sqrt(2), a 3 dB split.
    file << "# Hz S RI R 50\n";
    for (const char *freq : {"5e8", "5e9"}) {
        file << freq;
        for (int pair = 0; pair < 9; ++pair)
            file << (pair == 6 || pair == 7 ? " 0.7071067811865476 0" : " 0 0");
        file << '\n';
    }
    return path;
}

} // namespace

TEST_CASE("Combiner: basic combination with -3 dB loss per input", "[combiner]") {
    NodeGraphEngine graph;
    CombinerEngine combiner(1, graph);

    Spectrum input0 = buildTestSpectrum(1e9, -10.0, 0.0);
    Spectrum input1 = buildTestSpectrum(2e9, -20.0, 0.0);
    combiner.node().inputs[0] = &input0;
    combiner.node().inputs[1] = &input1;

    combiner.update(0.016);

    const auto &output = combiner.node().outputs[0];
    REQUIRE(output.tones.size() == 2);

    // Each tone should have -3 dB loss applied
    REQUIRE_THAT(output.tones[0].power_dBm, WithinAbs(-13.01, 0.01));
    REQUIRE_THAT(output.tones[1].power_dBm, WithinAbs(-23.01, 0.01));
}

TEST_CASE("Combiner: single input connected", "[combiner]") {
    NodeGraphEngine graph;
    CombinerEngine combiner(1, graph);

    Spectrum input0 = buildTestSpectrum(1e9, -10.0, 0.0);
    combiner.node().inputs[0] = &input0;
    combiner.node().inputs[1] = nullptr;

    combiner.update(0.016);

    const auto &output = combiner.node().outputs[0];
    REQUIRE(output.tones.size() == 1);
    REQUIRE_THAT(output.tones[0].power_dBm, WithinAbs(-13.01, 0.01));
}

TEST_CASE("Combiner: both inputs unconnected", "[combiner]") {
    NodeGraphEngine graph;
    CombinerEngine combiner(1, graph);

    combiner.node().inputs[0] = nullptr;
    combiner.node().inputs[1] = nullptr;

    combiner.update(0.016);

    const auto &output = combiner.node().outputs[0];
    REQUIRE(output.tones.empty());
}

TEST_CASE("Combiner: dirty-flag skip", "[combiner]") {
    NodeGraphEngine graph;
    CombinerEngine combiner(1, graph);

    Spectrum input0 = buildTestSpectrum(1e9, -10.0, 0.0);
    combiner.node().inputs[0] = &input0;

    combiner.update(0.016);
    uint64_t gen1 = combiner.node().outputs[0].generation;

    combiner.update(0.016);
    uint64_t gen2 = combiner.node().outputs[0].generation;

    REQUIRE(gen1 == gen2);
}

TEST_CASE("Combiner: hover summary", "[combiner]") {
    NodeGraphEngine graph;
    CombinerEngine combiner(1, graph);

    std::string summary = combiner.hoverSummary();
    REQUIRE(summary.find("Combiner") != std::string::npos);
    REQUIRE(summary.find("2") != std::string::npos);
    REQUIRE(summary.find("1") != std::string::npos);
}

TEST_CASE("Combiner: S-param mode", "[combiner][sparam]") {
    NodeGraphEngine graph;
    CombinerEngine combiner(1, graph);

    combiner.setSParamFilepath(std::string(PROJECT_SOURCE_DIR) +
                               "/component_data/splitters/mpd-0226ch/MPD-0226CH_CH_25C_F.s3p");
    REQUIRE(combiner.sparamMode());

    Spectrum input0 = buildTestSpectrum(1e9, -10.0, 0.0);
    Spectrum input1 = buildTestSpectrum(2e9, -20.0, 0.0);
    combiner.node().inputs[0] = &input0;
    combiner.node().inputs[1] = &input1;

    combiner.update(0.016);

    const auto &output = combiner.node().outputs[0];
    REQUIRE(output.tones.size() == 2);

    // Verify S-parameters are applied (tones should be modified by S21/S31)
    REQUIRE(output.tones[0].power_dBm != -10.0);
    REQUIRE(output.tones[1].power_dBm != -20.0);
}

TEST_CASE("Combiner: manual mode preserves IN0 per-bin phase", "[combiner][phase]") {
    NodeGraphEngine graph;
    CombinerEngine combiner(1, graph);

    Spectrum input0 = buildTestSpectrum(1e9, -10.0, 30.0);
    input0.phase_deg.assign(3, 30.0);
    Spectrum input1 = buildTestSpectrum(2e9, -20.0, 0.0);
    combiner.node().inputs[0] = &input0;
    combiner.node().inputs[1] = &input1;

    combiner.update(0.016);

    const auto &output = combiner.node().outputs[0];
    REQUIRE(output.phase_deg.size() == 3);
    for (double p : output.phase_deg) {
        REQUIRE_THAT(p, WithinAbs(30.0, 1e-9));
    }
}

TEST_CASE("Combiner: S-param mode adds S21 phase to IN0 per-bin phase",
          "[combiner][phase][sparam]") {
    NodeGraphEngine graph;
    CombinerEngine combiner(1, graph);

    const std::string path = std::string(PROJECT_SOURCE_DIR) +
                             "/component_data/splitters/mpd-0226ch/MPD-0226CH_CH_25C_F.s3p";
    combiner.setSParamFilepath(path);
    REQUIRE(combiner.sparamMode());

    SParameterData data;
    REQUIRE(data.load(path));
    const int idx_S21 = 2 * data.numPorts() + 0;
    const double expected_shift_deg =
        std::arg(data.interpolate(1e9, idx_S21)) * 180.0 / std::numbers::pi;
    REQUIRE(std::abs(expected_shift_deg) > 1.0);

    Spectrum input0 = buildTestSpectrum(1e9, -10.0, 30.0);
    input0.phase_deg.assign(3, 30.0);
    Spectrum input1 = buildTestSpectrum(2e9, -20.0, 0.0);
    combiner.node().inputs[0] = &input0;
    combiner.node().inputs[1] = &input1;

    combiner.update(0.016);

    const auto &output = combiner.node().outputs[0];
    REQUIRE(output.phase_deg.size() == 3);
    REQUIRE_THAT(output.phase_deg[0], WithinAbs(30.0 + expected_shift_deg, 1e-6));
}

TEST_CASE("Combiner: non-reciprocal 3-port S31 drives the first path", "[combiner][sparam]") {
    const auto path = std::filesystem::temp_directory_path() /
                      ("non_reciprocal_" + test_temp_paths::processTag() + ".s3p");
    {
        std::ofstream file(path);
        file << "# GHz S RI R 50\n1";
        for (int pair = 0; pair < 9; ++pair) {
            const double magnitude = pair == 2 ? 0.1 : (pair == 6 ? 0.5 : 1.0);
            file << ' ' << magnitude << " 0";
        }
        file << '\n';
    }

    NodeGraphEngine graph;
    CombinerEngine combiner(1, graph);
    combiner.setSParamFilepath(path.string());
    REQUIRE(combiner.sparamMode());

    Spectrum input0 = buildTestSpectrum(1e9, -10.0, 0.0);
    combiner.node().inputs[0] = &input0;
    combiner.node().inputs[1] = nullptr;
    combiner.update(0.016);

    const auto &output = combiner.node().outputs[0];
    REQUIRE(output.tones.size() == 1);
    REQUIRE_THAT(output.tones[0].power_dBm, WithinAbs(-16.0206, 0.001));
}

TEST_CASE("Combiner: matched grids sum noise per bin with -3 dB loss", "[combiner][issue278]") {
    NodeGraphEngine graph;
    CombinerEngine combiner(1, graph);

    Spectrum in0 = buildTestSpectrum(1e9, -10.0, 0.0);
    Spectrum in1 = buildTestSpectrum(2e9, -20.0, 0.0);
    in0.noise_total_W = {4e-21, 2e-21, 1e-21};
    in1.noise_total_W = {8e-21, 6e-21, 3e-21};
    combiner.node().inputs[0] = &in0;
    combiner.node().inputs[1] = &in1;

    combiner.update(0.0);

    const auto &out = combiner.node().outputs[0];
    REQUIRE(out.noise_total_W.size() == 3);
    REQUIRE_THAT(out.noise_total_W[0], WithinAbs(0.5 * (4e-21 + 8e-21), 1e-30));
    REQUIRE_THAT(out.noise_total_W[1], WithinAbs(0.5 * (2e-21 + 6e-21), 1e-30));
    REQUIRE_THAT(out.noise_total_W[2], WithinAbs(0.5 * (1e-21 + 3e-21), 1e-30));
}

TEST_CASE("Combiner: a kT input through one port reads 3.01 dB NF in manual mode",
          "[combiner][noise][issue277]") {
    NodeGraphEngine graph;
    CombinerEngine combiner(1, graph);
    constexpr double kT = 1.3806e-23 * 290.0;

    Spectrum in0 = buildTestSpectrum(1e9, -10.0, 0.0);
    in0.noise_total_W.assign(3, kT);
    combiner.node().inputs[0] = &in0;
    combiner.node().inputs[1] = nullptr;
    combiner.update(0.0);

    // Port 1 is absent, so it is a 290 K termination: 0.5*kT through port 0 plus 0.5*kT
    // from the absent port gives kT out, the attenuator model at gain 0.5.
    const auto &out = combiner.node().outputs[0];
    REQUIRE(out.noise_total_W.size() == 3);
    for (double density : out.noise_total_W) {
        REQUIRE(density / kT == Catch::Approx(1.0).epsilon(1e-9));
        REQUIRE_THAT(10.0 * std::log10(density / (0.5 * kT)), WithinAbs(3.0103, 1e-3));
    }
}

TEST_CASE("Combiner: S-param and manual modes agree on thermal noise for a 3 dB part",
          "[combiner][noise][sparam][issue277]") {
    constexpr double kT = 1.3806e-23 * 290.0;
    const auto path = writeFlatMinus3dBS3p();

    Spectrum src0 = buildTestSpectrum(1e9, -10.0, 0.0);
    src0.noise_total_W.assign(3, kT);
    Spectrum src1 = buildTestSpectrum(2e9, -20.0, 0.0);
    src1.noise_total_W.assign(3, kT);

    // Each case drives one or both inputs with a kT source; an absent input is a 290 K termination.
    struct InputCase {
        bool in0;
        bool in1;
    };
    for (const InputCase c :
         {InputCase{true, true}, InputCase{true, false}, InputCase{false, true}}) {
        const Spectrum *a = c.in0 ? &src0 : nullptr;
        const Spectrum *b = c.in1 ? &src1 : nullptr;

        NodeGraphEngine manual_graph;
        CombinerEngine manual(1, manual_graph);
        manual.node().inputs[0] = a;
        manual.node().inputs[1] = b;
        manual.update(0.0);

        NodeGraphEngine sparam_graph;
        CombinerEngine sparam(2, sparam_graph);
        sparam.setSParamFilepath(path.string());
        REQUIRE(sparam.sparamMode());
        sparam.node().inputs[0] = a;
        sparam.node().inputs[1] = b;
        sparam.update(0.0);

        const auto &manual_noise = manual.node().outputs[0].noise_total_W;
        const auto &sparam_noise = sparam.node().outputs[0].noise_total_W;
        REQUIRE(manual_noise.size() == 3);
        REQUIRE(sparam_noise.size() == 3);
        for (size_t i = 0; i < 3; ++i) {
            REQUIRE(sparam_noise[i] / kT == Catch::Approx(1.0).epsilon(1e-9));
            REQUIRE(manual_noise[i] / kT == Catch::Approx(sparam_noise[i] / kT).epsilon(1e-9));
        }
    }

    std::filesystem::remove(path);
}

TEST_CASE("Combiner: refuses inputs on different frequency grids", "[combiner][issue278]") {
    NodeGraphEngine graph;
    CombinerEngine combiner(1, graph);

    // IN0 on 1-3 GHz with no noise; IN1 on 10-12 GHz with noise at 10 GHz.
    // Bin-index pairing would put the 10 GHz noise at the 1 GHz output bin.
    Spectrum in0 = buildTestSpectrum(1e9, -10.0, 0.0);
    in0.noise_total_W = {0.0, 0.0, 0.0};
    Spectrum in1;
    in1.frequencies = {10e9, 11e9, 12e9};
    in1.noise_W.assign(3, 0.0);
    in1.noise_added_W.assign(3, 0.0);
    in1.noise_total_W = {1e-20, 0.0, 0.0};
    in1.phase_deg.assign(3, 0.0);
    in1.fs_Hz = 1e9;
    combiner.node().inputs[0] = &in0;
    combiner.node().inputs[1] = &in1;

    combiner.update(0.0);

    const auto &out = combiner.node().outputs[0];
    REQUIRE(out.tones.empty());
    REQUIRE(out.noise_total_W.size() == 3);
    for (double n : out.noise_total_W)
        REQUIRE(n == 0.0);
}

TEST_CASE("Combiner: refuses complex-baseband input combined with RF input",
          "[combiner][issue278]") {
    NodeGraphEngine graph;
    CombinerEngine combiner(1, graph);

    Spectrum adc = buildTestSpectrum(1e9, -10.0, 0.0);
    adc.is_complex_baseband = true;
    Spectrum rf = buildTestSpectrum(1e9, -20.0, 0.0);
    combiner.node().inputs[0] = &adc;
    combiner.node().inputs[1] = &rf;

    combiner.update(0.0);

    const auto &out = combiner.node().outputs[0];
    REQUIRE(out.tones.empty());
    REQUIRE(out.is_complex_baseband == true);
    for (double n : out.noise_total_W)
        REQUIRE(n == 0.0);
}

TEST_CASE("Combiner: S-parameter mode refuses mismatched grids", "[combiner][issue278][sparam]") {
    NodeGraphEngine graph;
    CombinerEngine combiner(1, graph);
    combiner.setSParamFilepath(std::string(PROJECT_SOURCE_DIR) +
                               "/component_data/splitters/mpd-0226ch/MPD-0226CH_CH_25C_F.s3p");
    REQUIRE(combiner.sparamMode());

    Spectrum in0 = buildTestSpectrum(1e9, -10.0, 0.0);
    in0.noise_total_W = {1e-20, 1e-20, 1e-20};
    Spectrum in1;
    in1.frequencies = {10e9, 11e9, 12e9};
    in1.noise_W.assign(3, 0.0);
    in1.noise_added_W.assign(3, 0.0);
    in1.noise_total_W = {1e-20, 0.0, 0.0};
    in1.phase_deg.assign(3, 0.0);
    in1.fs_Hz = 1e9;
    combiner.node().inputs[0] = &in0;
    combiner.node().inputs[1] = &in1;

    combiner.update(0.0);

    const auto &out = combiner.node().outputs[0];
    REQUIRE(out.tones.empty());
    REQUIRE(out.noise_total_W.size() == 3);
    for (double n : out.noise_total_W)
        REQUIRE(n == 0.0);
}

TEST_CASE("Combiner: hover summary reports the refusal reason", "[combiner][issue278]") {
    NodeGraphEngine graph;
    CombinerEngine combiner(1, graph);

    Spectrum adc = buildTestSpectrum(1e9, -10.0, 0.0);
    adc.is_complex_baseband = true;
    Spectrum rf = buildTestSpectrum(1e9, -20.0, 0.0);
    combiner.node().inputs[0] = &adc;
    combiner.node().inputs[1] = &rf;
    combiner.update(0.0);

    REQUIRE(combiner.hoverSummary().find("REFUSED") != std::string::npos);
    REQUIRE(combiner.hoverSummary().find("complex-baseband") != std::string::npos);

    combiner.node().inputs[1] = nullptr;
    combiner.update(0.0);
    REQUIRE(combiner.hoverSummary().find("REFUSED") == std::string::npos);
}
