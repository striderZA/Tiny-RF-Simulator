#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <numbers>

#include "adc_engine.h"
#include "node_graph_engine.h"

using Catch::Approx;

static constexpr double Fs = 1e9;
static constexpr double FC = 2.4e9;
static constexpr double BW = 10e6;
static constexpr int D = 20;
static constexpr int N_SAMPLES = 10000;

TEST_CASE("ADC with empty input produces NSD-based noise", "[adc]") {
    NodeGraphEngine graph;
    AdcEngine adc(1, graph);
    adc.setFs_Hz(Fs);
    adc.setNsd_dBm_per_Hz(-155.0);
    adc.setNSamples(N_SAMPLES);
    adc.setDecimation(D);

    adc.node().inputs[0] = Spectrum();
    adc.update(0.0);

    const auto& iq = adc.iqOutput();
    REQUIRE(iq.samples.size() == N_SAMPLES / D);
    REQUIRE(iq.sample_rate_Hz == Approx(Fs / D));

    // Output should be non-zero (ADC NSD noise)
    double power = 0.0;
    for (auto s : iq.samples)
        power += std::norm(s);
    power /= iq.samples.size();
    REQUIRE(power > 0.0);
}

TEST_CASE("ADC tone at channel center shifts to DC via DDC", "[adc]") {
    NodeGraphEngine graph;
    AdcEngine adc(2, graph);
    adc.setFs_Hz(Fs);
    adc.setFChannel_Hz(FC);
    adc.setBw_Hz(BW);
    adc.setDecimation(D);
    adc.setNSamples(N_SAMPLES);
    adc.setNsd_dBm_per_Hz(-200.0);

    Spectrum spec;
    spec.tones = {{FC, 0.0, 0.0}};
    adc.node().inputs[0] = spec;
    adc.update(0.0);

    const auto& iq = adc.iqOutput();
    std::complex<double> mean = 0;
    for (auto s : iq.samples) mean += s;
    mean /= static_cast<double>(iq.samples.size());
    REQUIRE(std::abs(mean) > 0.01);
}

TEST_CASE("ADC aliases tone correctly for odd and even Nyquist zones", "[adc]") {
    NodeGraphEngine graph;

    // Odd zone: tone at channel center directly
    {
        AdcEngine adc(3, graph);
        adc.setFs_Hz(Fs);
        adc.setFChannel_Hz(100e6);
        adc.setBw_Hz(50e6);
        adc.setDecimation(D);
        adc.setNSamples(N_SAMPLES);
        adc.setNsd_dBm_per_Hz(-200.0);

        Spectrum spec;
        spec.tones = {{100e6, 0.0, 0.0}};
        adc.node().inputs[0] = spec;
        adc.update(0.0);

        std::complex<double> mean = 0;
        for (auto s : adc.iqOutput().samples) mean += s;
        mean /= static_cast<double>(adc.iqOutput().samples.size());
        REQUIRE(std::abs(mean) > 0.01);
    }

    // Even zone: f_RF in zone 4 (even) for Fs=1e9
    // Zone 4 spans 1.5e9 to 2.0e9. f_RF = 1.6e9.
    // Alias = Fs - (f_RF - 1.5*Fs) ... actually alias = Fs - f_RF_mod_Fs for upper half.
    // f_RF_mod_Fs = 1.6e9 % 1e9 = 600e6. 600e6 > Fs/2, so alias = Fs - 600e6 = 400e6.
    // We set f_channel to the same f_RF so DDC shifts alias to DC
    {
        double f_even = 1.6e9;
        AdcEngine adc(4, graph);
        adc.setFs_Hz(Fs);
        adc.setFChannel_Hz(f_even);
        adc.setBw_Hz(50e6);
        adc.setDecimation(D);
        adc.setNSamples(N_SAMPLES);
        adc.setNsd_dBm_per_Hz(-200.0);

        Spectrum spec;
        spec.tones = {{f_even, 0.0, 0.0}};
        adc.node().inputs[0] = spec;
        adc.update(0.0);

        std::complex<double> mean = 0;
        for (auto s : adc.iqOutput().samples) mean += s;
        mean /= static_cast<double>(adc.iqOutput().samples.size());
        REQUIRE(std::abs(mean) > 0.01);
    }
}

TEST_CASE("ADC NSD change affects output noise power", "[adc]") {
    NodeGraphEngine graph;

    auto measure_noise = [&](double nsd) -> double {
        AdcEngine adc(5, graph);
        adc.setFs_Hz(Fs);
        adc.setNsd_dBm_per_Hz(nsd);
        adc.setNSamples(N_SAMPLES);
        adc.setDecimation(D);
        adc.node().inputs[0] = Spectrum();
        adc.update(0.0);

        double power = 0.0;
        for (auto s : adc.iqOutput().samples)
            power += std::norm(s);
        return power / adc.iqOutput().samples.size();
    };

    double p_lo = measure_noise(-160.0);
    double p_hi = measure_noise(-154.0);

    // 6 dB higher NSD → ~4× higher noise power
    REQUIRE(p_hi > p_lo * 3.0);
}

TEST_CASE("ADC decimation filter reduces out-of-band signal", "[adc]") {
    NodeGraphEngine graph;
    AdcEngine adc(6, graph);
    adc.setFs_Hz(Fs);
    adc.setFChannel_Hz(FC);
    adc.setBw_Hz(BW);
    adc.setDecimation(D);
    adc.setNSamples(N_SAMPLES);
    adc.setNsd_dBm_per_Hz(-200.0);

    // In-band tone at f_channel
    double power_inband = 0.0;
    {
        Spectrum spec;
        spec.tones = {{FC, 0.0, 0.0}};
        adc.node().inputs[0] = spec;
        adc.update(0.0);
        for (auto s : adc.iqOutput().samples)
            power_inband += std::norm(s);
        REQUIRE(power_inband > 0.0);
    }

    // Out-of-band tone at channel+Fs/D (decimation alias boundary)
    double power_outband = 0.0;
    {
        Spectrum spec;
        spec.tones = {{FC + Fs / D, 0.0, 0.0}};
        adc.node().inputs[0] = spec;
        adc.update(0.0);
        for (auto s : adc.iqOutput().samples)
            power_outband += std::norm(s);
        REQUIRE(power_outband < power_inband * 0.1);
    }
}

TEST_CASE("ADC produces diagnostic FFT Spectrum output", "[adc]") {
    NodeGraphEngine graph;
    AdcEngine adc(7, graph);
    adc.setFs_Hz(Fs);
    adc.setFChannel_Hz(FC);
    adc.setBw_Hz(BW);
    int d = 16;
    int ns = 8192;
    adc.setDecimation(d);
    adc.setNSamples(ns);
    adc.setNsd_dBm_per_Hz(-200.0);

    Spectrum spec;
    spec.tones = {{FC, 0.0, 0.0}};
    adc.node().inputs[0] = spec;
    adc.update(0.0);

    const auto& out = adc.node().outputs[0];
    int N_expected = ns / d; // 512, power of 2 → no FFT padding
    REQUIRE(out.frequencies.size() == N_expected);
    double fs_out = Fs / d;
    REQUIRE(out.frequencies[0] == Approx(-fs_out / 2.0).margin(fs_out / 10.0));
    REQUIRE(out.noise_total_W.size() == out.frequencies.size());
    REQUIRE(out.tones.empty());
}
