#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "adc_engine.h"
#include "node_graph_engine.h"

using Catch::Approx;
static constexpr double Fs = 1e9;

// ---- helpers ----

static Spectrum makeInput(int n_bins, double f_start, double f_stop) {
    if (n_bins < 2) {
        Spectrum s;
        s.frequencies = {f_start, f_stop};
        s.noise_W = {1e-20, 1e-20};
        s.noise_added_W = {0.0, 0.0};
        s.noise_total_W = {1e-20, 1e-20};
        return s;
    }
    Spectrum s;
    s.frequencies.resize(n_bins);
    double df = (f_stop - f_start) / (n_bins - 1);
    for (int i = 0; i < n_bins; ++i)
        s.frequencies[i] = f_start + i * df;
    s.noise_W.assign(n_bins, 1e-20);
    s.noise_added_W.assign(n_bins, 0.0);
    s.noise_total_W.assign(n_bins, 1e-20);
    return s;
}

// ---- tests ----

TEST_CASE("ADC DDC tone at Fs/4 maps to DC", "[adc]") {
    NodeGraphEngine graph;
    AdcEngine adc(0, graph);
    adc.setFs_Hz(Fs);

    Spectrum in = makeInput(101, 0.0, 500e6);
    in.tones.push_back({Fs / 4.0, -10.0, 0.0}); // 250 MHz

    adc.node().inputs[0] = &in;
    adc.update(0.0);

    const auto &out = adc.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    REQUIRE(out.tones[0].freq_Hz == Approx(0.0).margin(1.0));
}

TEST_CASE("ADC DDC tone at 3Fs/4 aliases to DC", "[adc]") {
    NodeGraphEngine graph;
    AdcEngine adc(1, graph);
    adc.setFs_Hz(Fs);

    Spectrum in = makeInput(101, 0.0, 500e6);
    in.tones.push_back({3.0 * Fs / 4.0, -10.0, 0.0}); // 750 MHz → alias=250 → NCO→0

    adc.node().inputs[0] = &in;
    adc.update(0.0);

    const auto &out = adc.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    REQUIRE(out.tones[0].freq_Hz == Approx(0.0).margin(1.0));
}

TEST_CASE("ADC DDC tone at 0 Hz maps to -Fs/4", "[adc]") {
    NodeGraphEngine graph;
    AdcEngine adc(2, graph);
    adc.setFs_Hz(Fs);

    Spectrum in = makeInput(101, 0.0, 500e6);
    in.tones.push_back({0.0, -20.0, 0.0});

    adc.node().inputs[0] = &in;
    adc.update(0.0);

    const auto &out = adc.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    REQUIRE(out.tones[0].freq_Hz == Approx(-Fs / 4.0).margin(1.0));
}

// Name avoids the "[-Fs/4, Fs/4)" bracket-paren pattern: catch_discover_tests
// (Catch2 v3.4.0) expands the discovered name list unquoted, and an unclosed
// '[' followed by ')' breaks CMake's list splitting, merging this test and
// every name after it into one ';'-joined CTest entry that Catch2 rejects as
// an "Invalid Filter" (observed on CMake 4.x; the test otherwise passes on
// MinGW).
TEST_CASE("ADC DDC output grid spans from -Fs/4 to Fs/4", "[adc]") {
    NodeGraphEngine graph;
    AdcEngine adc(3, graph);
    adc.setFs_Hz(Fs);

    Spectrum in = makeInput(101, 0.0, 500e6);

    adc.node().inputs[0] = &in;
    adc.update(0.0);

    const auto &out = adc.node().outputs[0];
    REQUIRE(out.frequencies.size() >= 2);
    REQUIRE(out.frequencies.front() == Approx(-Fs / 4.0).margin(1e-6));
    double df = out.frequencies[1] - out.frequencies[0];
    double expected_last = -Fs / 4.0 + (out.frequencies.size() - 1) * df;
    REQUIRE(out.frequencies.back() == Approx(expected_last).margin(df / 2.0));
    REQUIRE(out.frequencies.back() > -Fs / 4.0);

    // uniform spacing
    REQUIRE(df > 0.0);
    for (size_t i = 2; i < out.frequencies.size(); ++i) {
        double d = out.frequencies[i] - out.frequencies[i - 1];
        REQUIRE(d == Approx(df).margin(1e-6));
    }
}

TEST_CASE("ADC DDC output fs_Hz is Fs/2", "[adc]") {
    NodeGraphEngine graph;
    AdcEngine adc(4, graph);
    adc.setFs_Hz(Fs);

    Spectrum in = makeInput(101, 0.0, 500e6);

    adc.node().inputs[0] = &in;
    adc.update(0.0);

    REQUIRE(adc.node().outputs[0].fs_Hz == Approx(Fs / 2.0));
}

TEST_CASE("ADC DDC adds NSD noise", "[adc]") {
    NodeGraphEngine graph;
    AdcEngine adc(5, graph);
    adc.setFs_Hz(Fs);
    adc.setNsd_dBm_per_Hz(-150.0);

    Spectrum in = makeInput(101, 0.0, 500e6);

    adc.node().inputs[0] = &in;
    adc.update(0.0);

    const auto &out = adc.node().outputs[0];
    REQUIRE(out.noise_total_W.size() == out.frequencies.size());
    const double in_noise = 1e-20;
    double nsd_W_per_Hz =
        0.001 * std::pow(10.0, -150.0 / 10.0); // matches setNsd_dBm_per_Hz(-150.0)
    double expected_noise_per_bin = in_noise + nsd_W_per_Hz;
    for (size_t i = 0; i < out.noise_total_W.size(); ++i) {
        REQUIRE(out.noise_total_W[i] > in_noise);
        REQUIRE(out.noise_total_W[i] ==
                Approx(expected_noise_per_bin).margin(expected_noise_per_bin * 0.1));
    }
}

TEST_CASE("ADC DDC empty input produces empty output", "[adc]") {
    NodeGraphEngine graph;
    AdcEngine adc(6, graph);
    adc.setFs_Hz(Fs);

    adc.node().inputs[0] = nullptr;
    adc.update(0.0);

    const auto &out = adc.node().outputs[0];
    REQUIRE(out.frequencies.empty());
    REQUIRE(out.tones.empty());
    REQUIRE(out.noise_total_W.empty());
}

TEST_CASE("ADC DDC preserves tone power and phase", "[adc]") {
    NodeGraphEngine graph;
    AdcEngine adc(7, graph);
    adc.setFs_Hz(Fs);

    Spectrum in = makeInput(101, 0.0, 500e6);
    in.tones.push_back({Fs / 4.0, -20.0, 45.0});

    adc.node().inputs[0] = &in;
    adc.update(0.0);

    const auto &out = adc.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    REQUIRE(out.tones[0].power_dBm == Approx(-20.0));
    REQUIRE(out.tones[0].phase_deg == Approx(45.0));
}

TEST_CASE("ADC setFs_Hz clamps invalid values", "[adc]") {
    NodeGraphEngine graph;
    AdcEngine adc(8, graph);

    // Zero should be clamped to 1.0
    adc.setFs_Hz(0.0);
    REQUIRE(adc.fs_Hz() == Approx(1.0));

    // Negative should be clamped to 1.0
    adc.setFs_Hz(-1e9);
    REQUIRE(adc.fs_Hz() == Approx(1.0));

    // Valid value should be accepted
    adc.setFs_Hz(1e9);
    REQUIRE(adc.fs_Hz() == Approx(1e9));
}
