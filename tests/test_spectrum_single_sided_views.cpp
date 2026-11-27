#include "spectrum_single_sided_test_support.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <vector>

using Catch::Approx;

// Single-sided view behaviour above the fold: complex-baseband traces are shown unchanged, the
// readout, axis, and plot agree on the folded spectrum, and hold modes follow the mode in use.

TEST_CASE("SpectrumAnalyzer single-sided: complex-baseband traces are shown unchanged",
          "[spectrum][single_sided][complex]") {
    Spectrum spec = makeSpsSpectrum(kSpsNoiseDensity_W_per_Hz, true);
    spec.tones = {{-100e6, -3.0, 0.0}, {60e6, -8.0, 0.0}};
    const std::vector<const Spectrum *> specs = {&spec};

    SpectrumAnalyzerEngine sa;
    configureSpsDeterministic(sa, SpectrumSides::DoubleSided);
    const SpsReadout doubled = spsReadoutOf(sa, specs);

    configureSpsDeterministic(sa, SpectrumSides::SingleSided);
    const SpsReadout single = spsReadoutOf(sa, specs);

    CHECK(single.axis == doubled.axis);
    CHECK(single.dBm == doubled.dBm);
    CHECK(single.axis.front() == Approx(-20e9));
    CHECK(spsValueNear(single.axis, single.dBm, -100e6) == Approx(-3.0).margin(0.01));
    CHECK(spsValueNear(single.axis, single.dBm, 60e6) == Approx(-8.0).margin(0.01));
}

TEST_CASE("SpectrumAnalyzer single-sided: the readout, axis, and plot agree on the folded spectrum",
          "[spectrum][single_sided][readout]") {
    const Spectrum spec = makeSpsRealToneScene({{100e6, -3.0, 0.0}});

    SpectrumAnalyzerEngine sa;
    configureSpsDeterministic(sa, SpectrumSides::SingleSided);
    const std::vector<const Spectrum *> specs = {&spec};
    const SpsReadout r = spsReadoutOf(sa, specs);

    CHECK(r.dBm.size() == r.axis.size());
    CHECK(sa.renderCombinedSpectrum(specs) == r.dBm);
    CHECK(sa.displayFrequencies(spec) == r.axis);

    const auto peaks = sa.findPeaks(r.dBm, r.axis, 1);
    REQUIRE(peaks.size() == 1);
    CHECK(peaks.front().freq_Hz == Approx(100e6));
    CHECK(peaks.front().power_dBm == Approx(-3.0).margin(0.01));

    SECTION("the plot starts at 0 Hz when every trace is real-domain") {
        CHECK(sa.plotStartFrequency(specs) == Approx(0.0));
    }
    SECTION("a complex-baseband trace keeps the plot's negative half") {
        const Spectrum complex = makeSpsSpectrum(0.0, true);
        CHECK(sa.plotStartFrequency({&spec, &complex}) == Approx(-20e9));
    }
    SECTION("double-sided keeps the plot start unchanged") {
        configureSpsDeterministic(sa, SpectrumSides::DoubleSided);
        CHECK(sa.plotStartFrequency(specs) == Approx(-20e9));
    }
}

TEST_CASE(
    "SpectrumAnalyzer single-sided: Max Hold keeps the folded value, and a mode change restarts it",
    "[spectrum][single_sided][hold]") {
    Spectrum spec = makeSpsRealToneScene({{100e6, -3.0, 0.0}});
    const std::vector<const Spectrum *> specs = {&spec};

    SpectrumAnalyzerEngine sa;
    configureSpsDeterministic(sa, SpectrumSides::SingleSided);
    sa.setTraceMode(TraceMode::MaxHold);

    // Frame 1: the tone is present. Frame 2: it is gone, and the hold still reads it.
    (void)sa.renderCombinedSpectrum(specs);
    (void)sa.renderCombinedReadoutSpectrum(specs);
    spec.tones.clear();
    const SpsReadout held = spsReadoutOf(sa, specs);
    CHECK(spsValueNear(held.axis, held.dBm, 100e6) == Approx(-3.0).margin(0.01));
    CHECK(spsValueNear(sa.combinedReadoutAxis(specs), sa.renderCombinedSpectrum(specs), 100e6) ==
          Approx(-3.0).margin(0.01));

    SECTION("switching to double-sided restarts the hold: no folded residue") {
        sa.setSpectrumSides(SpectrumSides::DoubleSided);
        const SpsReadout restarted = spsReadoutOf(sa, specs);
        CHECK(spsValueNear(restarted.axis, restarted.dBm, 100e6) < -170.0);
    }
    SECTION("switching back and forth restarts the hold each time") {
        sa.setSpectrumSides(SpectrumSides::DoubleSided);
        sa.setSpectrumSides(SpectrumSides::SingleSided);
        const SpsReadout restarted = spsReadoutOf(sa, specs);
        CHECK(spsValueNear(restarted.axis, restarted.dBm, 100e6) < -170.0);
    }
}

TEST_CASE(
    "SpectrumAnalyzer single-sided: a span that ends below 0 Hz keeps its order and is flagged",
    "[spectrum][single_sided][span]") {
    const Spectrum real = makeSpsRealToneScene({{100e6, -3.0, 0.0}});
    const Spectrum complex = makeSpsSpectrum(0.0, true);
    SpectrumAnalyzerEngine sa;
    configureSpsDeterministic(sa, SpectrumSides::SingleSided);

    SECTION("a span wholly below 0 Hz keeps its own start, which stays below its stop") {
        sa.setStartFrequency(-500e6);
        sa.setStopFrequency(-100e6);
        const std::vector<const Spectrum *> specs = {&real};
        const double start = sa.plotStartFrequency(specs);
        CHECK(start == Approx(-500e6));
        CHECK(start < sa.stopFrequency());
        CHECK(sa.singleSidedSpanHidesFold(specs));
    }
    SECTION("a span ending exactly at 0 Hz is also flagged, and the axis is not inverted") {
        sa.setStartFrequency(-500e6);
        sa.setStopFrequency(0.0);
        const std::vector<const Spectrum *> specs = {&real};
        CHECK(sa.plotStartFrequency(specs) < sa.stopFrequency());
        CHECK(sa.singleSidedSpanHidesFold(specs));
    }
    SECTION("a span crossing 0 Hz starts at 0 Hz and hides nothing") {
        sa.setStartFrequency(-500e6);
        sa.setStopFrequency(500e6);
        const std::vector<const Spectrum *> specs = {&real};
        CHECK(sa.plotStartFrequency(specs) == Approx(0.0));
        CHECK_FALSE(sa.singleSidedSpanHidesFold(specs));
    }
    SECTION("a complex-baseband trace keeps its negative span and is never flagged") {
        sa.setStartFrequency(-500e6);
        sa.setStopFrequency(-100e6);
        const std::vector<const Spectrum *> specs = {&complex};
        CHECK(sa.plotStartFrequency(specs) == Approx(-500e6));
        CHECK_FALSE(sa.singleSidedSpanHidesFold(specs));
    }
}
