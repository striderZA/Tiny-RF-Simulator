// Issue #239: applyRBW() and applyVBW() turn a width ratio (width / bin width)
// into an int kernel half-width or window. An out-of-range or non-finite double
// cannot be converted to int, and a width that is zero, negative, or non-finite
// defines no filter. The contract pinned here: such a width passes the input
// through unchanged, and a very large ratio is clamped to the grid before the
// conversion without changing any output the unclamped filter produced.
#include "spectrum.h"
#include "spectrum_analyzer_engine.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kBinWidth_Hz = 1.0;

// Sixteen distinct bins, so any filter that changes the trace shows up.
const std::vector<double> kPower = {1.0, 4.0, 2.0, 8.0,  1.0, 0.5, 3.0, 6.0,
                                    2.0, 9.0, 1.5, 0.25, 5.0, 2.5, 7.0, 0.75};

// Gaussian RBW with every grid bin in reach. The engine scales sigma so that the
// kernel sums to about RBW / bin width. A kernel as wide as the grid reaches every
// bin from every position, so this is the output for any RBW at least that wide.
std::vector<double> wholeGridGaussian(const std::vector<double> &p, double rbw_Hz,
                                      double binWidth) {
    const double sigma = (rbw_Hz / binWidth) / std::sqrt(2.0 * kPi);
    std::vector<double> out(p.size(), 0.0);
    for (size_t i = 0; i < p.size(); ++i) {
        for (size_t j = 0; j < p.size(); ++j) {
            const double d = static_cast<double>(j) - static_cast<double>(i);
            out[i] += p[j] * std::exp(-0.5 * d * d / (sigma * sigma));
        }
    }
    return out;
}

// The output of a VBW window at least as wide as the trace: the whole-trace mean.
std::vector<double> wholeTraceMean(const std::vector<double> &p) {
    double sum = 0.0;
    for (double v : p) {
        sum += v;
    }
    return std::vector<double>(p.size(), sum / static_cast<double>(p.size()));
}

Spectrum makeSpectrum(size_t bins) {
    Spectrum spec;
    spec.frequencies.resize(bins);
    for (size_t i = 0; i < bins; ++i) {
        spec.frequencies[i] = static_cast<double>(i) * 1e6;
    }
    spec.noise_total_W.assign(bins, 1e-18);
    spec.tones = {{10e6, -20.0, 0.0}};
    spec.is_complex_baseband = true;
    return spec;
}

} // namespace

TEST_CASE("RBW: a zero, negative, or non-finite RBW passes the trace through", "[spectrum][rbw]") {
    SpectrumAnalyzerEngine sa;
    SECTION("zero") { sa.setResBw(0.0); }
    SECTION("negative") { sa.setResBw(-1e6); }
    SECTION("NaN") { sa.setResBw(kNaN); }
    SECTION("infinite") { sa.setResBw(kInf); }
    REQUIRE(sa.applyRBW(kPower, kBinWidth_Hz) == kPower);
}

TEST_CASE("RBW: a zero, negative, or non-finite bin width passes the trace through",
          "[spectrum][rbw]") {
    SpectrumAnalyzerEngine sa;
    sa.setResBw(1e6);
    SECTION("zero") { REQUIRE(sa.applyRBW(kPower, 0.0) == kPower); }
    SECTION("negative") { REQUIRE(sa.applyRBW(kPower, -kBinWidth_Hz) == kPower); }
    SECTION("NaN") { REQUIRE(sa.applyRBW(kPower, kNaN) == kPower); }
    SECTION("infinite") { REQUIRE(sa.applyRBW(kPower, kInf) == kPower); }
}

TEST_CASE("RBW: a width far narrower than one bin leaves the trace unchanged", "[spectrum][rbw]") {
    SpectrumAnalyzerEngine sa;
    sa.setResBw(1e-300); // sigma underflows to zero; the kernel must stay the identity
    const std::vector<double> out = sa.applyRBW(kPower, kBinWidth_Hz);
    REQUIRE(out.size() == kPower.size());
    for (size_t i = 0; i < out.size(); ++i) {
        REQUIRE(out[i] == Catch::Approx(kPower[i]));
    }
}

TEST_CASE("RBW: a ratio past the int range is clamped to the grid", "[spectrum][rbw]") {
    SpectrumAnalyzerEngine sa;
    sa.setResBw(1e12); // 1e12 bins: far beyond INT_MAX
    const std::vector<double> out = sa.applyRBW(kPower, kBinWidth_Hz);
    const std::vector<double> expected = wholeGridGaussian(kPower, 1e12, kBinWidth_Hz);
    REQUIRE(out.size() == expected.size());
    for (size_t i = 0; i < out.size(); ++i) {
        REQUIRE(out[i] == Catch::Approx(expected[i]));
    }
}

TEST_CASE("RBW: a ratio wider than the grid but within int range keeps its output",
          "[spectrum][rbw]") {
    SpectrumAnalyzerEngine sa;
    sa.setResBw(1000.0); // 1000 bins on a 16-bin grid
    const std::vector<double> out = sa.applyRBW(kPower, kBinWidth_Hz);
    const std::vector<double> expected = wholeGridGaussian(kPower, 1000.0, kBinWidth_Hz);
    REQUIRE(out.size() == expected.size());
    for (size_t i = 0; i < out.size(); ++i) {
        REQUIRE(out[i] == Catch::Approx(expected[i]));
    }
}

TEST_CASE("RBW: flat noise density integrates to the RBW over the bin width", "[spectrum][rbw]") {
    constexpr double kRatio = 5.0; // RBW / bin width
    SpectrumAnalyzerEngine sa;
    sa.setResBw(kRatio * kBinWidth_Hz);
    const std::vector<double> flat(41, 1.0); // one unit of power per bin
    const std::vector<double> out = sa.applyRBW(flat, kBinWidth_Hz);
    REQUIRE(out[20] == Catch::Approx(kRatio).margin(0.05));
}

TEST_CASE("VBW: a zero, negative, or non-finite VBW passes the trace through", "[spectrum][vbw]") {
    SpectrumAnalyzerEngine sa;
    SECTION("zero") { sa.setVideoBw(0.0); }
    SECTION("negative") { sa.setVideoBw(-1e6); }
    SECTION("NaN") { sa.setVideoBw(kNaN); }
    SECTION("infinite") { sa.setVideoBw(kInf); }
    REQUIRE(sa.applyVBW(kPower, kBinWidth_Hz) == kPower);
}

TEST_CASE("VBW: a zero, negative, or non-finite bin width passes the trace through",
          "[spectrum][vbw]") {
    SpectrumAnalyzerEngine sa;
    sa.setVideoBw(1e6);
    SECTION("zero") { REQUIRE(sa.applyVBW(kPower, 0.0) == kPower); }
    SECTION("negative") { REQUIRE(sa.applyVBW(kPower, -kBinWidth_Hz) == kPower); }
    SECTION("NaN") { REQUIRE(sa.applyVBW(kPower, kNaN) == kPower); }
    SECTION("infinite") { REQUIRE(sa.applyVBW(kPower, kInf) == kPower); }
}

TEST_CASE("VBW: a window past the int range averages the whole trace", "[spectrum][vbw]") {
    SpectrumAnalyzerEngine sa;
    sa.setVideoBw(1e12); // 1e12 bins: far beyond INT_MAX
    const std::vector<double> out = sa.applyVBW(kPower, kBinWidth_Hz);
    const std::vector<double> expected = wholeTraceMean(kPower);
    REQUIRE(out.size() == expected.size());
    for (size_t i = 0; i < out.size(); ++i) {
        REQUIRE(out[i] == Catch::Approx(expected[i]));
    }
}

TEST_CASE("VBW: an in-range window averages the neighboring bins", "[spectrum][vbw]") {
    SpectrumAnalyzerEngine sa;
    sa.setVideoBw(3.0 * kBinWidth_Hz); // three-bin window
    const std::vector<double> out = sa.applyVBW(kPower, kBinWidth_Hz);
    REQUIRE(out.size() == kPower.size());
    for (size_t i = 0; i < kPower.size(); ++i) {
        double sum = 0.0;
        int count = 0;
        for (int k = -1; k <= 1; ++k) {
            const int j = static_cast<int>(i) + k;
            if (j < 0 || j >= static_cast<int>(kPower.size())) {
                continue;
            }
            sum += kPower[static_cast<size_t>(j)];
            ++count;
        }
        REQUIRE(out[i] == Catch::Approx(sum / count));
    }
}

TEST_CASE("renderSpectrum: a zero RBW or VBW still yields a finite trace", "[spectrum][render]") {
    const Spectrum spec = makeSpectrum(64);
    SpectrumAnalyzerEngine sa;
    sa.setNoiseJitterEnabled(false);
    SECTION("zero RBW") { sa.setResBw(0.0); }
    SECTION("zero VBW") { sa.setVideoBw(0.0); }
    const std::vector<double> trace = sa.renderSpectrum(spec);
    REQUIRE(trace.size() == spec.frequencies.size());
    for (double v : trace) {
        REQUIRE(std::isfinite(v));
    }
}
