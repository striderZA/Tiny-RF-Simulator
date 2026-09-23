#include "spectrum.h"
#include "spectrum_analyzer_engine.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

// Analyzer-owned SNR measurement. The analyzer is the only layer that knows how
// noise density is binned, how the RBW filter integrates it, and where a tone
// lands on the rounded display grid, so the strongest-tone SNR is computed here
// and consumed by hover/reporting layers as a plain optional number.

namespace {

constexpr size_t kBins = 101;
constexpr size_t kCenter = 50;
constexpr double kBinWidth_Hz = 1e6;
constexpr double kNoiseDensity_W_per_Hz = 1e-18;

// 1e-18 W/Hz over a 1 MHz bin => 1e-12 W per bin. A -20 dBm (-50 dBW) tone is
// 1e-5 W, so the 10 MHz RBW-filtered floor (~1e-11 W) sits ~60 dB below it.
constexpr double kStrongestTone_dBm = -20.0;
constexpr double kWeakerTone_dBm = -30.0;
constexpr double kExpectedSnrAt10MHz_dB = 60.0;
constexpr double kMargin_dB = 0.25;

Spectrum makeSpectrum() {
    Spectrum spec;
    spec.frequencies.resize(kBins);
    for (size_t i = 0; i < kBins; ++i)
        spec.frequencies[i] = static_cast<double>(i) * kBinWidth_Hz;
    spec.noise_total_W.assign(kBins, kNoiseDensity_W_per_Hz);
    spec.tones = {{static_cast<double>(kCenter) * kBinWidth_Hz, kStrongestTone_dBm, 0.0},
                  {static_cast<double>(kCenter + 5) * kBinWidth_Hz, kWeakerTone_dBm, 0.0}};
    spec.is_complex_baseband = true;
    return spec;
}

} // namespace

TEST_CASE("SpectrumAnalyzer: strongest-tone SNR follows the RBW filter width", "[spectrum][snr]") {
    Spectrum spec = makeSpectrum();

    SpectrumAnalyzerEngine sa;
    sa.setResBw(10e6);
    std::optional<double> snr_10MHz = sa.computeStrongestToneSNRdB(spec);
    REQUIRE(snr_10MHz.has_value());
    REQUIRE(*snr_10MHz == Catch::Approx(kExpectedSnrAt10MHz_dB).margin(kMargin_dB));

    // A narrower RBW integrates less noise power, so the same tone must read a
    // higher SNR.
    sa.setResBw(5e6);
    std::optional<double> snr_5MHz = sa.computeStrongestToneSNRdB(spec);
    REQUIRE(snr_5MHz.has_value());
    REQUIRE(*snr_5MHz > *snr_10MHz);
    REQUIRE(*snr_5MHz > *snr_10MHz + 2.5);
    REQUIRE(*snr_5MHz == Catch::Approx(63.0).margin(0.5));
}

TEST_CASE("SpectrumAnalyzer: strongest-tone SNR uses full stored tone power on a "
          "real-domain spectrum",
          "[spectrum][snr]") {
    // The real-domain display path splits each tone into its +-fc half-power
    // pair, so its displayed peak is 3 dB low. SNR must be quoted against the
    // stored full tone power instead, matching the complex-baseband result.
    Spectrum complex_spec = makeSpectrum();
    Spectrum real_spec = makeSpectrum();
    real_spec.is_complex_baseband = false;

    SpectrumAnalyzerEngine sa;
    sa.setResBw(10e6);

    std::optional<double> complex_snr = sa.computeStrongestToneSNRdB(complex_spec);
    std::optional<double> real_snr = sa.computeStrongestToneSNRdB(real_spec);
    REQUIRE(complex_snr.has_value());
    REQUIRE(real_snr.has_value());
    REQUIRE(*real_snr == Catch::Approx(kExpectedSnrAt10MHz_dB).margin(kMargin_dB));
    REQUIRE(*real_snr == Catch::Approx(*complex_snr).margin(1e-9));
}

TEST_CASE("SpectrumAnalyzer: strongest-tone SNR ignores non-finite tone powers",
          "[spectrum][snr]") {
    Spectrum spec = makeSpectrum();
    // The loudest entry is unusable, so the greatest *finite* tone wins.
    spec.tones.push_back({60e6, std::numeric_limits<double>::quiet_NaN(), 0.0});

    SpectrumAnalyzerEngine sa;
    sa.setResBw(10e6);
    std::optional<double> snr = sa.computeStrongestToneSNRdB(spec);
    REQUIRE(snr.has_value());
    REQUIRE(*snr == Catch::Approx(kExpectedSnrAt10MHz_dB).margin(kMargin_dB));
}

TEST_CASE("SpectrumAnalyzer: strongest-tone SNR is unavailable without usable input",
          "[spectrum][snr]") {
    SECTION("no tones") {
        Spectrum spec = makeSpectrum();
        spec.tones.clear();
        SpectrumAnalyzerEngine sa;
        sa.setResBw(10e6);
        REQUIRE_FALSE(sa.computeStrongestToneSNRdB(spec).has_value());
    }

    SECTION("one-bin grid") {
        Spectrum spec = makeSpectrum();
        spec.frequencies.resize(1);
        spec.noise_total_W.resize(1);
        spec.tones = {{0.0, kStrongestTone_dBm, 0.0}};
        SpectrumAnalyzerEngine sa;
        sa.setResBw(10e6);
        REQUIRE_FALSE(sa.computeStrongestToneSNRdB(spec).has_value());
    }

    SECTION("strongest tone outside the grid") {
        Spectrum spec = makeSpectrum();
        // Grid spans 0..100 MHz; the strongest tone sits at 500 MHz.
        spec.tones = {{500e6, kStrongestTone_dBm, 0.0},
                      {static_cast<double>(kCenter) * kBinWidth_Hz, kWeakerTone_dBm, 0.0}};
        SpectrumAnalyzerEngine sa;
        sa.setResBw(10e6);
        REQUIRE_FALSE(sa.computeStrongestToneSNRdB(spec).has_value());
    }

    SECTION("tone just above the top of the span") {
        // 100.1 MHz on a 0..100 MHz grid must not round back into the last bin.
        Spectrum spec = makeSpectrum();
        spec.tones = {{100.1e6, kStrongestTone_dBm, 0.0}};
        SpectrumAnalyzerEngine sa;
        sa.setResBw(10e6);
        REQUIRE_FALSE(sa.computeStrongestToneSNRdB(spec).has_value());
    }

    SECTION("tone just below the bottom of the span") {
        // -0.1 MHz is finite and rounds to bin 0, but it is outside the span.
        Spectrum spec = makeSpectrum();
        spec.tones = {{-0.1e6, kStrongestTone_dBm, 0.0}};
        SpectrumAnalyzerEngine sa;
        sa.setResBw(10e6);
        REQUIRE_FALSE(sa.computeStrongestToneSNRdB(spec).has_value());
    }

    SECTION("all-zero noise density") {
        Spectrum spec = makeSpectrum();
        spec.noise_total_W.assign(kBins, 0.0);
        SpectrumAnalyzerEngine sa;
        sa.setResBw(10e6);
        REQUIRE_FALSE(sa.computeStrongestToneSNRdB(spec).has_value());
    }

    SECTION("zero RBW") {
        Spectrum spec = makeSpectrum();
        SpectrumAnalyzerEngine sa;
        sa.setResBw(0.0);
        REQUIRE_FALSE(sa.computeStrongestToneSNRdB(spec).has_value());
    }

    SECTION("non-finite RBW") {
        Spectrum spec = makeSpectrum();
        SpectrumAnalyzerEngine sa;
        sa.setResBw(std::numeric_limits<double>::quiet_NaN());
        REQUIRE_FALSE(sa.computeStrongestToneSNRdB(spec).has_value());
    }

    SECTION("non-finite frequency bin") {
        Spectrum spec = makeSpectrum();
        spec.frequencies[1] = std::numeric_limits<double>::quiet_NaN();
        SpectrumAnalyzerEngine sa;
        sa.setResBw(10e6);
        REQUIRE_FALSE(sa.computeStrongestToneSNRdB(spec).has_value());
    }
}

TEST_CASE("SpectrumAnalyzer: strongest-tone SNR picks the nearest grid entry on a "
          "nonuniform grid",
          "[spectrum][snr]") {
    // The first bin spacing is not representative of the rest of the grid, so a
    // rounded (f - front) / first_spacing lookup lands on the wrong entry. Distinct
    // noise densities at the competing entries separate the two choices numerically.
    SECTION("nearest entry wins over first-spacing rounding") {
        Spectrum spec;
        spec.frequencies = {0.0, 100.0, 101.0, 102.0};
        // Index 1 -> 1e-16 W per bin, index 3 -> 1e-13 W per bin (x100 Hz spacing).
        spec.noise_total_W = {0.0, 1e-18, 0.0, 1e-15};
        // 101.6 Hz: nearest entry is 102 (0.4 Hz away), while first-spacing rounding
        // divides by 100 Hz and picks index 1 (1.6 Hz away + a wrong bin entirely).
        spec.tones = {{101.6, kStrongestTone_dBm, 0.0}};
        spec.is_complex_baseband = true;

        SpectrumAnalyzerEngine sa;
        // RBW far below the bin spacing -> the Gaussian kernel collapses to the
        // identity, so the measured floor is exactly the selected entry's power.
        sa.setResBw(1.0);

        std::optional<double> snr = sa.computeStrongestToneSNRdB(spec);
        REQUIRE(snr.has_value());
        // 1e-5 W against 1e-13 W.
        REQUIRE(*snr == Catch::Approx(80.0).margin(1e-6));
        REQUIRE(std::fabs(*snr - 110.0) > 1.0); // index 1 would have read this
    }

    SECTION("an exact tie resolves to the lower index") {
        Spectrum spec;
        spec.frequencies = {0.0, 1.0, 3.0};
        spec.noise_total_W = {0.0, 1e-18, 1e-15};
        // 2 Hz sits exactly between entries 1 and 3.
        spec.tones = {{2.0, kStrongestTone_dBm, 0.0}};
        spec.is_complex_baseband = true;

        SpectrumAnalyzerEngine sa;
        sa.setResBw(1e-3); // kernel collapses to the identity

        std::optional<double> snr = sa.computeStrongestToneSNRdB(spec);
        REQUIRE(snr.has_value());
        REQUIRE(*snr == Catch::Approx(130.0).margin(1e-6)); // lower index (1e-18 W)
        REQUIRE(std::fabs(*snr - 100.0) > 1.0);             // the upper index's value
    }
}

TEST_CASE("SpectrumAnalyzer: SNR measurement leaves render state untouched", "[spectrum][snr]") {
    Spectrum spec = makeSpectrum();

    SpectrumAnalyzerEngine sa;
    sa.setResBw(10e6);
    sa.setVideoBw(1e6); // VBW window == 1 bin, so it cannot mask cache changes
    sa.setNoiseJitterEnabled(false);
    sa.setTraceMode(TraceMode::MaxHold);

    std::vector<double> before = sa.renderSpectrum(spec);

    for (int i = 0; i < 5; ++i) {
        std::optional<double> snr = sa.computeStrongestToneSNRdB(spec);
        REQUIRE(snr.has_value());
    }

    std::vector<double> after = sa.renderSpectrum(spec);
    REQUIRE(before.size() == after.size());
    for (size_t i = 0; i < before.size(); ++i) {
        REQUIRE(before[i] == after[i]);
    }
}
