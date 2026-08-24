// Focused standalone tests for the configurable ADC DDC (decimation + NCO).
// These encode the contract from
// docs/superpowers/specs/2026-08-24-adc-ddc-configuration-design.md before the
// production implementation exists: they currently fail to compile because
// AdcEngine does not yet expose decimation()/setDecimation()/
// ncoFsFraction()/setNcoFsFraction(). Do not weaken them to make them pass.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "adc_engine.h"
#include "node_graph_engine.h"

using Catch::Approx;
static constexpr double Fs = 1e9;

// ---- helpers ----

// Builds a frequency grid over 0..Fs/2 and initializes the noise vectors,
// matching the input shape the ADC's DDC expects.
static Spectrum makeInput(int n_bins) {
    Spectrum s;
    s.frequencies.resize(n_bins);
    double df = (Fs / 2.0) / (n_bins - 1);
    for (int i = 0; i < n_bins; ++i)
        s.frequencies[i] = i * df;
    s.noise_W.assign(n_bins, 1e-20);
    s.noise_added_W.assign(n_bins, 0.0);
    s.noise_total_W.assign(n_bins, 1e-20);
    return s;
}

// ---- tests ----

TEST_CASE("ADC DDC output rate and grid follow decimation", "[adc][config]") {
    NodeGraphEngine graph;
    for (int d : {1, 2, 4, 8}) {
        INFO("decimation=" << d);
        AdcEngine adc(100 + d, graph);
        adc.setFs_Hz(Fs);
        adc.setDecimation(d);
        adc.setNcoFsFraction(0.0);

        Spectrum in = makeInput(101);
        adc.node().inputs[0] = &in;
        adc.update(0.0);

        const auto &out = adc.node().outputs[0];
        REQUIRE(out.fs_Hz == Approx(Fs / d));
        REQUIRE(out.is_complex_baseband);
        REQUIRE(out.frequencies.size() >= 2);
        REQUIRE(out.frequencies.front() == Approx(-Fs / (2.0 * d)).margin(1e-6));
        REQUIRE(out.frequencies.back() < Fs / (2.0 * d));
    }
}

TEST_CASE("ADC DDC positive NCO tunes complex tone to DC", "[adc][config]") {
    NodeGraphEngine graph;
    AdcEngine adc(2, graph);
    adc.setFs_Hz(Fs);
    adc.setDecimation(4);
    adc.setNcoFsFraction(0.125); // f_NCO = 125 MHz

    Spectrum in = makeInput(101);
    in.is_complex_baseband = true;
    in.tones.push_back({125e6, -10.0, 30.0});

    adc.node().inputs[0] = &in;
    adc.update(0.0);

    const auto &out = adc.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    REQUIRE(out.tones[0].freq_Hz == Approx(0.0).margin(1.0));
    REQUIRE(out.tones[0].power_dBm == Approx(-10.0));
    REQUIRE(out.tones[0].phase_deg == Approx(30.0));
}

TEST_CASE("ADC DDC NCO plus decimation drops out-of-band complex tone", "[adc][config]") {
    NodeGraphEngine graph;
    AdcEngine adc(3, graph);
    adc.setFs_Hz(Fs);
    adc.setDecimation(4);
    adc.setNcoFsFraction(0.125); // passband is [-Fs/8, Fs/8) = [-125, 125) MHz

    Spectrum in = makeInput(101);
    in.is_complex_baseband = true;
    // 300 MHz - f_NCO = 175 MHz, outside the D=4 output Nyquist span.
    in.tones.push_back({300e6, -10.0, 0.0});

    adc.node().inputs[0] = &in;
    adc.update(0.0);

    const auto &out = adc.node().outputs[0];
    REQUIRE(out.tones.empty());
}

TEST_CASE("ADC DDC empty input reports decimated fs", "[adc][config]") {
    NodeGraphEngine graph;
    AdcEngine adc(4, graph);
    adc.setFs_Hz(Fs);
    adc.setDecimation(8);

    adc.node().inputs[0] = nullptr;
    adc.update(0.0);

    const auto &out = adc.node().outputs[0];
    REQUIRE(out.frequencies.empty());
    REQUIRE(out.fs_Hz == Approx(Fs / 8.0));
}

TEST_CASE("ADC decimation setter accepts only supported values", "[adc][config]") {
    NodeGraphEngine graph;
    AdcEngine adc(5, graph);

    for (int d : {1, 2, 4, 8}) {
        adc.setDecimation(d);
        REQUIRE(adc.decimation() == d);
    }

    // Documented normalization: clamp to nearest supported choice, ties toward
    // the lower value (design spec "Configuration Contract"); 6 -> 4.
    adc.setDecimation(6);
    REQUIRE(adc.decimation() == 4);
}

TEST_CASE("ADC NCO fraction setter accepts normalized bounds", "[adc][config]") {
    NodeGraphEngine graph;
    AdcEngine adc(6, graph);

    adc.setNcoFsFraction(-0.5);
    REQUIRE(adc.ncoFsFraction() == Approx(-0.5));

    adc.setNcoFsFraction(0.5);
    REQUIRE(adc.ncoFsFraction() == Approx(0.5));
}

TEST_CASE("ADC DDC configuration serialization round trip", "[adc][config]") {
    NodeGraphEngine graph;
    AdcEngine src(7, graph);
    src.setFs_Hz(Fs);
    src.setNsd_dBm_per_Hz(-150.0);
    src.setDecimation(8);
    src.setNcoFsFraction(-0.125);

    nlohmann::json j = src.serialize();

    AdcEngine dst(8, graph);
    dst.deserialize(j);
    REQUIRE(dst.fs_Hz() == Approx(Fs));
    REQUIRE(dst.nsd_dBm_per_Hz() == Approx(-150.0));
    REQUIRE(dst.decimation() == 8);
    REQUIRE(dst.ncoFsFraction() == Approx(-0.125));
}

TEST_CASE("ADC DDC legacy JSON gets compatibility defaults", "[adc][config]") {
    NodeGraphEngine graph;
    AdcEngine adc(9, graph);
    adc.deserialize(nlohmann::json{{"sample_rate_Hz", Fs}, {"nsd_dBm_per_Hz", -150.0}});

    REQUIRE(adc.fs_Hz() == Approx(Fs));
    REQUIRE(adc.nsd_dBm_per_Hz() == Approx(-150.0));
    REQUIRE(adc.decimation() == 2);
    REQUIRE(adc.ncoFsFraction() == Approx(0.25));
}

TEST_CASE("ADC DDC invalid JSON values normalize", "[adc][config]") {
    NodeGraphEngine graph;
    AdcEngine adc(10, graph);
    adc.deserialize(nlohmann::json{{"sample_rate_Hz", Fs},
                                   {"nsd_dBm_per_Hz", -150.0},
                                   {"decimation", 6},
                                   {"nco_fs_fraction", 0.7}});

    // Documented normalization: D=6 -> 4 (nearest, tie toward lower), NCO=0.7 -> 0.5.
    REQUIRE(adc.decimation() == 4);
    REQUIRE(adc.ncoFsFraction() == Approx(0.5));
}
