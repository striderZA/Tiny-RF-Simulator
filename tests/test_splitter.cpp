#include "node_graph_engine.h"
#include "s_parameter_data.h"
#include "splitter_engine.h"
#include "spectrum.h"
#include "test_temp_paths.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

using Catch::Matchers::WithinAbs;

namespace {

constexpr double kT = 1.3806e-23 * 290.0;

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

// Row-major 3-port file. Pair index = row * 3 + col, (row, col) = (output, input).
// Row 1 = output 0, row 2 = output 1, column 0 = the input.
std::filesystem::path writeSplitterS3p(const std::string &tag, double s21_mag,
                                       double s21_phase_deg, double s31_mag,
                                       double s31_phase_deg) {
    const auto path =
        std::filesystem::temp_directory_path() / ("split_s3p_" + tag + "_" +
                                                  test_temp_paths::processTag() + ".s3p");
    std::ofstream file(path);
    file << "# Hz S MA R 50\n";
    for (const char *freq : {"5e8", "5e9"}) {
        file << freq;
        for (int pair = 0; pair < 9; ++pair) {
            double mag = 0.0, ph = 0.0;
            if (pair == 3) {
                mag = s21_mag;
                ph = s21_phase_deg;
            } else if (pair == 6) {
                mag = s31_mag;
                ph = s31_phase_deg;
            }
            file << ' ' << mag << ' ' << ph;
        }
        file << '\n';
    }
    return path;
}

} // namespace

TEST_CASE("Splitter: manual mode splits tones and noise at -3.01 dB", "[splitter]") {
    NodeGraphEngine graph;
    SplitterEngine splitter(1, graph);

    Spectrum in = buildTestSpectrum(1e9, -10.0, 30.0);
    splitter.node().inputs[0] = &in;
    splitter.update(0.016);

    const auto &out0 = splitter.node().outputs[0];
    REQUIRE(out0.tones.size() == 1);
    REQUIRE_THAT(out0.tones[0].power_dBm, WithinAbs(-10.0 - 3.0102999566398, 1e-6));
    REQUIRE_THAT(out0.tones[0].phase_deg, WithinAbs(30.0, 1e-9));
    REQUIRE(out0.noise_total_W.size() == 3);
    REQUIRE(out0.noise_added_W[0] == Catch::Approx(kT * 0.5).margin(1e-30));
}

TEST_CASE("Splitter: S-param mode drives both outputs from the input port", "[splitter][sparam]") {
    const auto path = writeSplitterS3p("both", 0.7071067811865476, 0.0, 0.5, 90.0);

    NodeGraphEngine graph;
    SplitterEngine splitter(1, graph);
    splitter.setSParamFilepath(path.string());
    REQUIRE(splitter.sparamMode());
    REQUIRE(splitter.sparamLoaded());
    REQUIRE(splitter.sparamData().numPorts() == 3);

    Spectrum in = buildTestSpectrum(1e9, -10.0, 30.0);
    in.noise_total_W.assign(3, kT);
    splitter.node().inputs[0] = &in;
    splitter.update(0.016);

    const auto &out0 = splitter.node().outputs[0];
    const auto &out1 = splitter.node().outputs[1];

    // Output 0 sees S21 = 0.7071 (=-3.01 dB, 0 deg).
    REQUIRE(out0.tones.size() == 1);
    REQUIRE_THAT(out0.tones[0].power_dBm, WithinAbs(-13.0103, 1e-3));
    REQUIRE_THAT(out0.tones[0].phase_deg, WithinAbs(30.0, 1e-6));
    // Output 1 sees S31 = 0.5 (-6.02 dB, 90 deg).
    REQUIRE(out1.tones.size() == 1);
    REQUIRE_THAT(out1.tones[0].power_dBm, WithinAbs(-16.0206, 1e-3));
    REQUIRE_THAT(out1.tones[0].phase_deg, WithinAbs(120.0, 1e-6));

    // Passive attenuator rule per output: gain*nin + kT(1-gain).
    for (const auto *out : {&out0, &out1}) {
        for (size_t i = 0; i < 3; ++i) {
            const double gain = out == &out0 ? 0.5 : 0.25;
            REQUIRE(out->noise_W[i] == Catch::Approx(gain * kT).margin(1e-30));
            REQUIRE(out->noise_added_W[i] == Catch::Approx(kT * (1.0 - gain)).margin(1e-30));
        }
    }
}

TEST_CASE("Splitter: S-param phase carries into per-bin phase", "[splitter][sparam][phase]") {
    const auto path = writeSplitterS3p("phase", 0.7071067811865476, 0.0, 0.7071067811865476, 45.0);

    NodeGraphEngine graph;
    SplitterEngine splitter(1, graph);
    splitter.setSParamFilepath(path.string());

    Spectrum in = buildTestSpectrum(1e9, -10.0, 30.0);
    in.phase_deg.assign(3, 10.0);
    splitter.node().inputs[0] = &in;
    splitter.update(0.016);

    const auto &out0 = splitter.node().outputs[0];
    const auto &out1 = splitter.node().outputs[1];
    REQUIRE_THAT(out0.phase_deg[0], WithinAbs(10.0, 1e-6));
    REQUIRE_THAT(out1.phase_deg[0], WithinAbs(55.0, 1e-6));
}

TEST_CASE("Splitter: flat -3 dB 3-port file matches manual mode exactly",
          "[splitter][sparam]") {
    const auto path = writeSplitterS3p("flat", 0.7071067811865476, 0.0, 0.7071067811865476, 0.0);

    NodeGraphEngine graph;
    SplitterEngine manual(1, graph);
    Spectrum in = buildTestSpectrum(1e9, -10.0, 0.0);
    manual.node().inputs[0] = &in;
    manual.update(0.016);

    SplitterEngine sparam(1, graph);
    sparam.setSParamFilepath(path.string());
    sparam.node().inputs[0] = &in;
    sparam.update(0.016);

    for (size_t o = 0; o < 2; ++o) {
        const auto &a = manual.node().outputs[o];
        const auto &b = sparam.node().outputs[o];
        REQUIRE(b.tones.size() == a.tones.size());
        for (size_t i = 0; i < b.tones.size(); ++i)
            REQUIRE(b.tones[i].power_dBm == Catch::Approx(a.tones[i].power_dBm).margin(1e-4));
        for (size_t i = 0; i < 3; ++i) {
            REQUIRE(b.noise_W[i] == Catch::Approx(a.noise_W[i]).margin(1e-30));
            REQUIRE(b.noise_added_W[i] == Catch::Approx(a.noise_added_W[i]).margin(1e-30));
            REQUIRE(b.noise_total_W[i] == Catch::Approx(a.noise_total_W[i]).margin(1e-30));
        }
    }
}

TEST_CASE("Splitter: a 2-port file falls back to manual mode", "[splitter][sparam]") {
    const auto path = std::filesystem::temp_directory_path() /
                      ("split_s2p_" + test_temp_paths::processTag() + ".s2p");
    {
        std::ofstream file(path);
        file << "# Hz S MA R 50\n5e8 0.7 0 0.7 0\n5e9 0.7 0 0.7 0\n";
    }

    NodeGraphEngine graph;
    SplitterEngine splitter(1, graph);
    splitter.setSParamFilepath(path.string());
    REQUIRE_FALSE(splitter.sparamMode());

    Spectrum in = buildTestSpectrum(1e9, -10.0, 0.0);
    splitter.node().inputs[0] = &in;
    splitter.update(0.016);

    // Manual-mode split, not the S-parameter values.
    const auto &out0 = splitter.node().outputs[0];
    REQUIRE_THAT(out0.tones[0].power_dBm, WithinAbs(-13.0102999566398, 1e-6));
}

TEST_CASE("Splitter: non-3-port serialization falls back to manual mode",
          "[splitter][sparam]") {
    NodeGraphEngine graph;
    SplitterEngine splitter(1, graph);
    nlohmann::json j = {{"sparam_mode", true},
                        {"sparam_filepath", std::string("does_not_exist.s3p")}};
    splitter.deserialize(j);
    REQUIRE_FALSE(splitter.sparamMode());

    // Round-trip a loaded 3-port file.
    const auto path = writeSplitterS3p("rt", 0.5, 0.0, 0.5, 0.0);
    SplitterEngine loaded(1, graph);
    loaded.setSParamFilepath(path.string());
    REQUIRE(loaded.sparamMode());
    nlohmann::json round = nlohmann::json::parse(loaded.serialize().dump());
    REQUIRE(round.value("sparam_mode", false));
    REQUIRE(round.value("sparam_filepath", std::string()) == path.string());
}

TEST_CASE("Splitter: hover summary", "[splitter]") {
    NodeGraphEngine graph;
    SplitterEngine splitter(1, graph);

    std::string summary = splitter.hoverSummary();
    REQUIRE(summary.find("Split") != std::string::npos);
    REQUIRE(summary.find("2") != std::string::npos);
}
