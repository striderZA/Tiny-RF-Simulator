#include "amplifier_engine.h"
#include "node_graph_engine.h"
#include "nonlinear_model.h"
#include "test_temp_paths.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

using Catch::Approx;

namespace {

// The magic line and DEVICE line that begin every amp-f file in these tests. Frequencies are MHz.
const std::string kHead = "# amp-f v1\nDEVICE TEST\n";

// A network section (S RI) so a legacy AMP file meets the MathWorks rule for S, Y, Z, or POUT.
const std::string kNetwork = "S RI R 50\nFREQ GHZ\n1.0 0.5 0.0 2.0 0.0 0.0 0.0 0.3 0.0\n";

// Writes files in a per-process directory that is removed when the test ends. Copied from
// test_amplifier_ampf.cpp, where the helper is private to that translation unit.
class Scratch {
  public:
    Scratch()
        : m_dir(std::filesystem::temp_directory_path() /
                ("rfsim_intercept_coverage_" + test_temp_paths::processTag() + "_" +
                 std::to_string(++s_count))) {
        std::filesystem::create_directories(m_dir);
    }
    ~Scratch() {
        std::error_code ec;
        std::filesystem::remove_all(m_dir, ec);
    }
    Scratch(const Scratch &) = delete;
    Scratch &operator=(const Scratch &) = delete;

    std::string write(const std::string &name, const std::string &text) const {
        const auto path = m_dir / name;
        std::ofstream out(path, std::ios::binary);
        out << text;
        return path.string();
    }

  private:
    static inline int s_count = 0;
    std::filesystem::path m_dir;
};

} // namespace

TEST_CASE("scalar OIP3 is covered at every frequency", "[intercept][coverage]") {
    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setOIP3_dBm(35.0);
    for (double f : {0.5e9, 1.0e9, 5.0e9}) {
        const auto at = amp.oip3At_Hz(f);
        CHECK(at.covered);
        CHECK(at.reason.empty());
        CHECK(at.oip3_dBm == Approx(35.0));
    }
}

TEST_CASE("AMP OIP3 curve covers only its band", "[intercept][coverage]") {
    Scratch scratch;
    const auto path =
        scratch.write("oip3.amp", kNetwork + "OIP3 dBm\nFREQ GHZ\n1.0 10.0\n1.3 20.0\n");
    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setAmpFilepath(path);
    REQUIRE(amp.ampLoaded());

    const auto inside = amp.oip3At_Hz(1.15e9);
    CHECK(inside.covered);
    CHECK(inside.oip3_dBm == Approx(15.0));

    const auto outside = amp.oip3At_Hz(1.5e9);
    CHECK_FALSE(outside.covered);
    CHECK(outside.oip3_dBm == Approx(20.0));
    CHECK(outside.reason == "AMP OIP3 data does not cover this frequency.");
}

TEST_CASE("AMP IIP3 curve adds the small-signal gain", "[intercept][coverage]") {
    Scratch scratch;
    const auto path =
        scratch.write("iip3.amp", kNetwork + "IIP3 dBm\nFREQ GHZ\n1.0 5.0\n1.3 5.0\n");
    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setGain_dB(10.0);
    amp.setAmpFilepath(path);
    REQUIRE(amp.ampLoaded());

    const auto at = amp.oip3At_Hz(1.15e9);
    CHECK(at.covered);
    CHECK(at.oip3_dBm == Approx(15.0));
}

TEST_CASE("amp-f falls through a missing OIP3 cell to IIP3 plus gain", "[intercept][coverage]") {
    Scratch scratch;
    const auto path = scratch.write(
        "fallthrough.ampf",
        kHead + "# COLUMNS: OIP3_dBm IIP3_dBm\nBEGIN\n1000 NaN 5.0\n1300 NaN 5.0\nEND\n");
    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setGain_dB(10.0);
    amp.setOIP3_dBm(12.0);
    amp.setAmpfFilepath(path);
    REQUIRE(amp.ampfBlockSelected());

    const auto at = amp.oip3At_Hz(1.15e9);
    CHECK(at.covered);
    CHECK(at.oip3_dBm == Approx(15.0));
}

TEST_CASE("amp-f block without intercept columns is not covered", "[intercept][coverage]") {
    Scratch scratch;
    const auto path = scratch.write(
        "oip2_only.ampf", kHead + "# COLUMNS: OIP2_dBm\nBEGIN\n1000 10.0\n1300 10.0\nEND\n");
    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setOIP3_dBm(12.0);
    amp.setAmpfFilepath(path);
    REQUIRE(amp.ampfBlockSelected());

    const auto at = amp.oip3At_Hz(1.15e9);
    CHECK_FALSE(at.covered);
    CHECK(at.oip3_dBm == Approx(12.0));
    CHECK(at.reason == "amp-f block has no OIP3, IIP3, OP1dB, or IP1dB value at this frequency.");
}

TEST_CASE("amp-f OIP3 outside the block's rows is not covered", "[intercept][coverage]") {
    Scratch scratch;
    const auto path = scratch.write(
        "outside.ampf", kHead + "# COLUMNS: OIP3_dBm\nBEGIN\n1000 10.0\n1300 20.0\nEND\n");
    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setAmpfFilepath(path);
    REQUIRE(amp.ampfBlockSelected());

    const auto at = amp.oip3At_Hz(1.5e9);
    CHECK_FALSE(at.covered);
    CHECK(at.oip3_dBm == Approx(20.0));
    CHECK(at.reason == "amp-f data does not cover this frequency.");
}

TEST_CASE("compression level inverts the closed form", "[intercept][nonlinear]") {
    // 1 dB of compression: 20*log10(1 - 10^((P - OIP3)/10)) == -1.
    const double level = NonlinearModel::outputLevelForCompression_dBm(30.0, 1.0);
    CHECK(level ==
          Approx(30.0 + 10.0 * std::log10(1.0 - std::pow(10.0, -1.0 / 20.0))).margin(1e-9));

    NonlinearModel model;
    model.setEnabled(true);
    model.setOIP3_dBm(30.0);
    // GainFn is linear amplitude gain; 1.0 is 0 dB, so the output equals the input level.
    const auto result = model.process({{1.0e9, level, 0.0}}, [](double) { return 1.0; });
    REQUIRE(result.tone_compression_dB.size() == 1);
    CHECK(result.tone_compression_dB[0] == Approx(-1.0).margin(1e-9));
}
