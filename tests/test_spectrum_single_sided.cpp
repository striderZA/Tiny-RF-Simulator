#include "spectrum_single_sided_test_support.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

using Catch::Approx;

// Single-sided fold rules: a real-domain trace folds its negative-frequency half onto the
// positive half, so a lone real tone reads its full level at +f (not the -3 dB double-sided
// image), coincident tones add as phasors, and independent noise bins merge (+3 dB per bin).

TEST_CASE("SpectrumAnalyzer single-sided: a real -3 dBm tone at 100 MHz reads -3 dBm at +100 MHz",
          "[spectrum][single_sided]") {
    const Spectrum spec = makeSpsRealToneScene({{100e6, -3.0, 0.0}});
    const std::vector<const Spectrum *> specs = {&spec};

    SpectrumAnalyzerEngine sa;
    SECTION("double-sided: the tone is split across +100 and -100 MHz at -6 dBm each") {
        configureSpsDeterministic(sa, SpectrumSides::DoubleSided);
        const SpsReadout r = spsReadoutOf(sa, specs);
        CHECK(spsValueNear(r.axis, r.dBm, 100e6) == Approx(-6.0103).margin(0.01));
        CHECK(spsValueNear(r.axis, r.dBm, -100e6) == Approx(-6.0103).margin(0.01));
    }
    SECTION("single-sided: the same tone reads -3 dBm at +100 MHz and nothing below 0 Hz") {
        configureSpsDeterministic(sa, SpectrumSides::SingleSided);
        const SpsReadout r = spsReadoutOf(sa, specs);
        CHECK(spsValueNear(r.axis, r.dBm, 100e6) == Approx(-3.0).margin(0.01));
        CHECK(r.axis.front() == Approx(0.0));
    }
}

TEST_CASE("SpectrumAnalyzer single-sided: two in-phase real cosines at one |f| add as phasors",
          "[spectrum][single_sided]") {
    // Two entries, one cosine each: +100 MHz at -3 dBm and a second cosine at -100 MHz with the
    // same power and phase. Both are cos(wt) here, so their amplitudes add: 2 sqrt(P) squared is
    // 4P, +6 dB over one tone (+3.02 dBm). A power sum would give only 2P (+0.01 dBm).
    const Spectrum spec = makeSpsRealToneScene({{100e6, -3.0, 0.0}, {-100e6, -3.0, 0.0}});
    const std::vector<const Spectrum *> specs = {&spec};

    SpectrumAnalyzerEngine sa;
    SECTION("single-sided folds to 4P at +100 MHz") {
        configureSpsDeterministic(sa, SpectrumSides::SingleSided);
        const SpsReadout r = spsReadoutOf(sa, specs);
        CHECK(spsValueNear(r.axis, r.dBm, 100e6) == Approx(3.0206).margin(0.02));
    }
    SECTION("double-sided keeps each side at 2P, and the two sides fold to the same 4P") {
        configureSpsDeterministic(sa, SpectrumSides::DoubleSided);
        const SpsReadout r = spsReadoutOf(sa, specs);
        const double up = spsValueNear(r.axis, r.dBm, 100e6);
        const double down = spsValueNear(r.axis, r.dBm, -100e6);
        CHECK(up == Approx(0.0103).margin(0.02));
        CHECK(down == Approx(0.0103).margin(0.02));
        const double folded =
            10.0 * std::log10(std::pow(10.0, up / 10.0) + std::pow(10.0, down / 10.0));
        CHECK(folded == Approx(3.0206).margin(0.02));
    }
}

TEST_CASE("SpectrumAnalyzer single-sided: opposite-phase coincident cosines cancel",
          "[spectrum][single_sided]") {
    // cos(wt) + cos(-wt + pi) = 0: the folded bin holds no power.
    const Spectrum spec = makeSpsRealToneScene({{100e6, -3.0, 0.0}, {-100e6, -3.0, 180.0}});

    SpectrumAnalyzerEngine sa;
    configureSpsDeterministic(sa, SpectrumSides::SingleSided);
    const SpsReadout r = spsReadoutOf(sa, {&spec});
    CHECK(spsValueNear(r.axis, r.dBm, 100e6) <= -170.0);
}

TEST_CASE(
    "SpectrumAnalyzer single-sided: a negative-frequency tone folds with its phase conjugated",
    "[spectrum][single_sided]") {
    // A real cosine at -f with phase theta is the cosine at +f with phase -theta. Here the +100 MHz
    // phasor is e^{j30} and the folded -100 MHz phasor is e^{-j60}: |e^{j30} + e^{-j60}|^2 = 2.0,
    // so the sum is 2P (+3.01 dB over one tone). Without the conjugate it would be 3.73P.
    const Spectrum spec = makeSpsRealToneScene({{100e6, -3.0, 30.0}, {-100e6, -3.0, 60.0}});

    SpectrumAnalyzerEngine sa;
    configureSpsDeterministic(sa, SpectrumSides::SingleSided);
    const SpsReadout r = spsReadoutOf(sa, {&spec});
    CHECK(spsValueNear(r.axis, r.dBm, 100e6) == Approx(0.0103).margin(0.02));
}

TEST_CASE("SpectrumAnalyzer single-sided: tones at distinct frequencies keep their own levels",
          "[spectrum][single_sided]") {
    const Spectrum spec = makeSpsRealToneScene({{100e6, -3.0, 0.0}, {200e6, -10.0, 0.0}});

    SpectrumAnalyzerEngine sa;
    configureSpsDeterministic(sa, SpectrumSides::SingleSided);
    const SpsReadout r = spsReadoutOf(sa, {&spec});
    CHECK(spsValueNear(r.axis, r.dBm, 100e6) == Approx(-3.0).margin(0.01));
    CHECK(spsValueNear(r.axis, r.dBm, 200e6) == Approx(-10.0).margin(0.01));
}

TEST_CASE("SpectrumAnalyzer single-sided: a flat real noise floor reads +3 dB per bin",
          "[spectrum][single_sided][noise]") {
    const Spectrum spec = makeSpsSpectrum(kSpsNoiseDensity_W_per_Hz, false);
    const std::vector<const Spectrum *> specs = {&spec};

    SpectrumAnalyzerEngine sa;
    configureSpsDeterministic(sa, SpectrumSides::DoubleSided);
    const SpsReadout doubled = spsReadoutOf(sa, specs);
    const double double_at_100MHz = spsValueNear(doubled.axis, doubled.dBm, 100e6);
    const double double_at_dc = spsValueNear(doubled.axis, doubled.dBm, 0.0);
    const double double_average = sa.computeAverageNoiseLevel(specs);
    CHECK(double_at_100MHz == Approx(kSpsNoiseBin_dBm).margin(0.02));

    configureSpsDeterministic(sa, SpectrumSides::SingleSided);
    const SpsReadout folded = spsReadoutOf(sa, specs);
    SECTION("an interior bin holds two independent bins: +3.01 dB") {
        CHECK(spsValueNear(folded.axis, folded.dBm, 100e6) ==
              Approx(kSpsNoiseBin_dBm + 3.0103).margin(0.02));
    }
    SECTION("the 0 Hz bin has no mirror, so it is not doubled") {
        const double folded_dc = spsValueNear(folded.axis, folded.dBm, 0.0);
        CHECK(folded_dc < double_at_dc + 1.5);
        CHECK(folded_dc > double_at_dc - 1.5);
    }
    SECTION("the average noise level reads the folded floor, about +3 dB") {
        const double folded_average = sa.computeAverageNoiseLevel(specs);
        CHECK(folded_average - double_average == Approx(3.01).margin(0.05));
    }
}

TEST_CASE("SpectrumAnalyzer single-sided: a tone within one bin of 0 Hz lands in the DC bin once",
          "[spectrum][single_sided]") {
    // +4 MHz is 0.4 of a bin from DC. Its mirror is the same tone, so it folds into bin 0 at the
    // full level, not at +3 dB (duplicated) or -3 dB (half).
    const Spectrum spec = makeSpsRealToneScene({{4e6, -3.0, 0.0}});

    SpectrumAnalyzerEngine sa;
    configureSpsDeterministic(sa, SpectrumSides::SingleSided);
    const SpsReadout r = spsReadoutOf(sa, {&spec});
    CHECK(spsValueNear(r.axis, r.dBm, 0.0) == Approx(-3.0).margin(0.05));
}
