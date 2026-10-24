#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <optional>

#include "adc_engine.h"
#include "node_graph_engine.h"
#include "spectrum_analyzer_engine.h"

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
    REQUIRE(out.tones[0].power_dBm == Approx(-10.0 - 10.0 * std::log10(2.0)));
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
    REQUIRE(out.tones[0].power_dBm == Approx(-10.0 - 10.0 * std::log10(2.0)));
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

TEST_CASE("ADC DDC maps real-input noise at half density", "[adc]") {
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
    double expected_noise_per_bin = (in_noise + nsd_W_per_Hz) / 2.0;
    for (size_t i = 0; i < out.noise_total_W.size(); ++i) {
        REQUIRE(out.noise_total_W[i] > in_noise / 2.0);
        REQUIRE(out.noise_total_W[i] ==
                Approx(expected_noise_per_bin).margin(expected_noise_per_bin * 0.1));
    }
}

TEST_CASE("ADC DDC conserves real-input flat noise power", "[adc]") {
    NodeGraphEngine graph;
    AdcEngine adc(6, graph);
    adc.setFs_Hz(Fs);
    adc.setNsd_dBm_per_Hz(-150.0);
    adc.setDecimation(1);
    adc.setNcoFsFraction(0.0);

    Spectrum in = makeInput(101, 0.0, Fs / 2.0);
    adc.node().inputs[0] = &in;
    adc.update(0.0);

    const auto &out = adc.node().outputs[0];
    const double df = out.fs_Hz / out.frequencies.size();
    double output_noise = 0.0;
    for (double noise : out.noise_W)
        output_noise += noise * df;
    const double input_noise = 1e-20 * (Fs / 2.0);
    REQUIRE(output_noise == Approx(input_noise).margin(input_noise * 0.01));

    double output_nsd = 0.0;
    for (double noise : out.noise_added_W)
        output_nsd += noise * df;
    const double nsd_W_per_Hz = 0.001 * std::pow(10.0, -150.0 / 10.0);
    REQUIRE(out.noise_added_W.front() == Approx(nsd_W_per_Hz / 2.0));
    REQUIRE(output_nsd ==
            Approx(nsd_W_per_Hz * (Fs / 2.0)).margin(nsd_W_per_Hz * (Fs / 2.0) * 0.01));
}

TEST_CASE("ADC DDC folds noise from every Nyquist zone like tones", "[adc][nyquist]") {
    // Issue #303: a zone-2 noise bump at 750 MHz must alias to -250 MHz (from +750 MHz)
    // and +250 MHz (from its mirror -750 MHz), matching the tone fold. Real-input noise is
    // half density (#264), so the 1e-12 bump appears as 0.5e-12 at each image.
    NodeGraphEngine graph;
    AdcEngine adc(20, graph);
    adc.setFs_Hz(Fs);
    adc.setDecimation(1);
    adc.setNcoFsFraction(0.0);
    adc.setNsd_dBm_per_Hz(-200.0);

    Spectrum in = makeInput(2001, 0.0, 2e9); // 1 MHz grid over 0..2 GHz, 1e-20 W/Hz baseline
    in.noise_total_W[750] = 1e-12;           // 750 MHz bump

    adc.node().inputs[0] = &in;
    adc.update(0.0);

    const auto &out = adc.node().outputs[0];
    auto nearest_index = [&](double f_Hz) {
        size_t best = 0;
        for (size_t i = 0; i < out.frequencies.size(); ++i) {
            if (std::abs(out.frequencies[i] - f_Hz) < std::abs(out.frequencies[best] - f_Hz))
                best = i;
        }
        return best;
    };
    REQUIRE(out.noise_total_W[nearest_index(250e6)] == Approx(0.5e-12).margin(1e-14));
    REQUIRE(out.noise_total_W[nearest_index(-250e6)] == Approx(0.5e-12).margin(1e-14));
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

TEST_CASE("ADC DDC coherently combines coincident images at a rounding-sensitive NCO",
          "[adc][issue305]") {
    NodeGraphEngine graph;
    AdcEngine adc(10, graph);
    adc.setFs_Hz(3e9);
    adc.setDecimation(1);
    adc.setNcoFsFraction(0.35);

    Spectrum in = makeInput(101, 0.0, 2e9);
    in.tones.push_back({1.2e9, -20.0, 0.0});
    in.tones.push_back({1.8e9, -20.0, 0.0}); // = Fs - 1.2 GHz: samples coincide

    adc.node().inputs[0] = &in;
    adc.update(0.0);

    // Each coincident pair must merge into one tone: +3.01 dB over -20 dBm.
    const auto &out = adc.node().outputs[0];
    REQUIRE(out.tones.size() == 2);
    REQUIRE(out.tones[0].freq_Hz == Approx(150e6).margin(1.0));
    REQUIRE(out.tones[0].power_dBm == Approx(-20.0 + 10.0 * std::log10(2.0)).margin(0.01));
    REQUIRE(out.tones[1].freq_Hz == Approx(750e6).margin(1.0));
    REQUIRE(out.tones[1].power_dBm == Approx(-20.0 + 10.0 * std::log10(2.0)).margin(0.01));

    // Analyzer readout: the coherent tone reads -16.99 dBm at its peak, not a half-power split.
    // Search only near 150 MHz: the equal-power 750 MHz tone ties the global maximum. Bins are
    // labelled by left edge (TRS-18 F6), so the peak sits one bin above 150 MHz.
    const SpectrumAnalyzerEngine analyzer;
    const auto trace = analyzer.renderSpectrum(out);
    REQUIRE(trace.size() == out.frequencies.size());
    const double bin_Hz = out.frequencies[1] - out.frequencies[0];
    std::optional<size_t> peak;
    for (size_t i = 0; i < trace.size(); ++i) {
        if (out.frequencies[i] < 100e6 || out.frequencies[i] > 200e6)
            continue;
        if (!peak || trace[i] > trace[*peak])
            peak = i;
    }
    REQUIRE(peak.has_value());
    REQUIRE(trace[*peak] == Approx(-20.0 + 10.0 * std::log10(2.0)).margin(0.05));
    REQUIRE(out.frequencies[*peak] >= 150e6 - bin_Hz / 2.0);
    REQUIRE(out.frequencies[*peak] <= 150e6 + bin_Hz * 1.5);
}

TEST_CASE("ADC DDC coincident-image control at an exact NCO", "[adc][issue305]") {
    NodeGraphEngine graph;
    AdcEngine adc(11, graph);
    adc.setFs_Hz(3e9);
    adc.setDecimation(1);
    adc.setNcoFsFraction(0.25);

    Spectrum in = makeInput(101, 0.0, 2e9);
    in.tones.push_back({1.2e9, -20.0, 0.0});
    in.tones.push_back({1.8e9, -20.0, 0.0});

    adc.node().inputs[0] = &in;
    adc.update(0.0);

    const auto &out = adc.node().outputs[0];
    REQUIRE(out.tones.size() == 2);
    REQUIRE(out.tones[0].freq_Hz == Approx(450e6).margin(1.0));
    REQUIRE(out.tones[0].power_dBm == Approx(-20.0 + 10.0 * std::log10(2.0)).margin(0.01));
    REQUIRE(out.tones[1].freq_Hz == Approx(1050e6).margin(1.0));
    REQUIRE(out.tones[1].power_dBm == Approx(-20.0 + 10.0 * std::log10(2.0)).margin(0.01));
}

TEST_CASE("ADC DDC retains selected real tone phase with physical half-power", "[adc]") {
    NodeGraphEngine graph;
    AdcEngine adc(7, graph);
    adc.setFs_Hz(Fs);

    Spectrum in = makeInput(101, 0.0, 500e6);
    in.tones.push_back({Fs / 4.0, -20.0, 45.0});

    adc.node().inputs[0] = &in;
    adc.update(0.0);

    const auto &out = adc.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    REQUIRE(out.tones[0].power_dBm == Approx(-20.0 - 10.0 * std::log10(2.0)));
    REQUIRE(out.tones[0].phase_deg == Approx(45.0));
}

TEST_CASE("ADC DDC leaves complex input tones unexpanded", "[adc]") {
    NodeGraphEngine graph;
    AdcEngine adc(8, graph);
    adc.setFs_Hz(Fs);

    Spectrum in = makeInput(101, 0.0, 500e6);
    in.is_complex_baseband = true;
    in.tones.push_back({Fs / 4.0, -20.0, 45.0});

    adc.node().inputs[0] = &in;
    adc.update(0.0);

    const auto &out = adc.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    REQUIRE(out.tones[0].freq_Hz == Approx(0.0).margin(1.0));
    REQUIRE(out.tones[0].power_dBm == Approx(-20.0));
    REQUIRE(out.tones[0].phase_deg == Approx(45.0));
}

TEST_CASE("ADC DDC coherently combines real-tone images at one output frequency", "[adc]") {
    NodeGraphEngine graph;
    AdcEngine adc(9, graph);
    adc.setFs_Hz(Fs);

    Spectrum in = makeInput(101, 0.0, 500e6);
    in.tones.push_back({Fs / 4.0, -20.0, 0.0});
    in.tones.push_back({3.0 * Fs / 4.0, -20.0, 0.0});

    adc.node().inputs[0] = &in;
    adc.update(0.0);

    const auto &out = adc.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    REQUIRE(out.tones[0].freq_Hz == Approx(0.0).margin(1.0));
    REQUIRE(out.tones[0].power_dBm == Approx(-20.0 + 10.0 * std::log10(2.0)));
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
