#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

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
    double df = out.frequencies[1] - out.frequencies[0];
    REQUIRE(out.frequencies.front() == Approx(-Fs / 4.0 + df / 2.0).margin(1e-6));
    double expected_last = -Fs / 4.0 + df / 2.0 + (out.frequencies.size() - 1) * df;
    REQUIRE(out.frequencies.back() == Approx(expected_last).margin(df / 2.0));
    REQUIRE(out.frequencies.back() > -Fs / 4.0);

    // uniform spacing
    REQUIRE(df > 0.0);
    for (size_t i = 2; i < out.frequencies.size(); ++i) {
        double d = out.frequencies[i] - out.frequencies[i - 1];
        REQUIRE(d == Approx(df).margin(1e-6));
    }
}

TEST_CASE("ADC DDC top-half-bin tone survives analyzer binning", "[adc]") {
    NodeGraphEngine graph;
    AdcEngine adc(6, graph);
    adc.setFs_Hz(Fs);
    adc.setDecimation(2);
    adc.setNcoFsFraction(0.0);

    Spectrum in = makeInput(101, 0.0, 500e6);
    in.tones.push_back({249e6, -10.0, 0.0});

    adc.node().inputs[0] = &in;
    adc.update(0.0);

    const auto &out = adc.node().outputs[0];
    REQUIRE(!out.tones.empty());
    REQUIRE(out.frequencies.size() >= 2);

    // binPowerComponents is private; renderSpectrum is the analyzer's public
    // bin mapping. A surviving +249 MHz tone must dominate the trace; a dropped
    // one leaves only the noise floor everywhere.
    SpectrumAnalyzerEngine analyzer;
    const std::vector<double> trace_dBm = analyzer.renderSpectrum(out);
    REQUIRE(trace_dBm.size() == out.frequencies.size());
    // The -249 MHz image lands in bin 0 at the same power, so a global-peak
    // search is a coin flip. Check the bin whose label is nearest +249 MHz.
    size_t near = 0;
    for (size_t i = 1; i < out.frequencies.size(); ++i)
        if (std::abs(out.frequencies[i] - 249e6) < std::abs(out.frequencies[near] - 249e6))
            near = i;
    double df = out.frequencies[1] - out.frequencies[0];
    REQUIRE(std::abs(out.frequencies[near] - 249e6) <= df);
    REQUIRE(trace_dBm[near] > -40.0);
}

TEST_CASE("ADC DDC lower-edge tone survives analyzer binning", "[adc]") {
    // Default NCO (+0.25 Fs), D=2: RF 0 Hz and RF 1 GHz both alias to exactly -Fs/4.
    // The ADC keeps them at the lower output edge; the analyzer must show them in bin 0.
    for (double rf : {0.0, 1e9}) {
        INFO("rf=" << rf);
        NodeGraphEngine graph;
        AdcEngine adc(rf == 0.0 ? 22 : 23, graph);
        adc.setFs_Hz(Fs);
        adc.setDecimation(2);

        Spectrum in = makeInput(101, 0.0, 500e6);
        in.tones.push_back({rf, -10.0, 0.0});

        adc.node().inputs[0] = &in;
        adc.update(0.0);

        const auto &out = adc.node().outputs[0];
        REQUIRE(out.tones.size() == 1);
        REQUIRE(out.tones[0].freq_Hz == Approx(-Fs / 4.0).margin(1.0));

        SpectrumAnalyzerEngine analyzer;
        const std::vector<double> trace_dBm = analyzer.renderSpectrum(out);
        REQUIRE(trace_dBm.size() == out.frequencies.size());
        REQUIRE(trace_dBm.front() > -40.0);
    }
}

TEST_CASE("ADC DDC lower-edge tone survives analyzer binning across grids", "[adc]") {
    // Real tone placed so the ADC keeps it exactly at -Fs/(2D). Output grids include
    // N=13 at Fs=100 MHz, where the edge sits on a half-bin tie in floating point.
    int id = 40;
    for (double Fs_test : {1e9, 1e8}) {
        for (int D : {2, 4, 8}) {
            for (double nco_frac : {0.0, 0.25}) {
                for (double span_frac : {0.1, 0.12, 0.24, 0.5}) {
                    for (int n_in : {13, 101}) {
                        const double edge = Fs_test / (2.0 * D);
                        const double nco_Hz = nco_frac * Fs_test;
                        const double rf = std::fmod(nco_Hz - edge + 2.0 * Fs_test, Fs_test);
                        INFO("Fs=" << Fs_test << " D=" << D << " nco=" << nco_frac
                                   << " span=" << span_frac << " n=" << n_in << " rf=" << rf);
                        NodeGraphEngine graph;
                        AdcEngine adc(id++, graph);
                        adc.setFs_Hz(Fs_test);
                        adc.setDecimation(D);
                        adc.setNcoFsFraction(nco_frac);

                        Spectrum in = makeInput(n_in, 0.0, span_frac * Fs_test);
                        in.tones.push_back({rf, -10.0, 0.0});

                        adc.node().inputs[0] = &in;
                        adc.update(0.0);

                        const auto &out = adc.node().outputs[0];
                        bool kept = false;
                        for (const auto &t : out.tones)
                            kept = kept || std::abs(t.freq_Hz + edge) <= 1.0;
                        REQUIRE(kept);

                        SpectrumAnalyzerEngine analyzer;
                        const std::vector<double> trace_dBm = analyzer.renderSpectrum(out);
                        REQUIRE(trace_dBm.size() == out.frequencies.size());
                        REQUIRE(trace_dBm.front() > -40.0);
                    }
                }
            }
        }
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
    // Output labels sit at bin centres, so ±250 MHz falls between two labels. The image
    // lands in one of them. Sum the labels strictly within one bin width of the image
    // frequency: the two labels that straddle it, and no others.
    const double df = out.frequencies[1] - out.frequencies[0];
    auto image_power = [&](double f_Hz) {
        double sum = 0.0;
        for (size_t i = 0; i < out.frequencies.size(); ++i)
            if (std::abs(out.frequencies[i] - f_Hz) < df)
                sum += out.noise_total_W[i];
        return sum;
    };
    REQUIRE(image_power(250e6) == Approx(0.5e-12).margin(1e-14));
    REQUIRE(image_power(-250e6) == Approx(0.5e-12).margin(1e-14));
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
