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
#include <vector>

using Catch::Approx;

namespace {

// A network section (S RI) so each file meets the MathWorks rule for S, Y, Z, or POUT data.
const std::string kNetwork = "S RI R 50\nFREQ GHZ\n1.0 0.5 0.0 2.0 0.0 0.0 0.0 0.3 0.0\n";

// Writes AMP files in a per-process directory that is removed when the test ends.
class Scratch {
  public:
    Scratch()
        : m_dir(std::filesystem::temp_directory_path() /
                ("rfsim_amp_engine_" + test_temp_paths::processTag() + "_" +
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

// Power of the tone at freq_Hz. The test fails when no tone sits there.
double powerAt(const std::vector<Spectrum::Tone> &tones, double freq_Hz) {
    for (const auto &tone : tones) {
        if (std::abs(tone.freq_Hz - freq_Hz) < 1.0)
            return tone.power_dBm;
    }
    FAIL("no tone at " << freq_Hz << " Hz");
    return 0.0;
}

// Products of the scalar model at one fixed intercept, for the given tones and gain.
NonlinearModel::Result scalarProducts(double oip3_dBm, const std::vector<Spectrum::Tone> &tones,
                                      double gain_dB) {
    NonlinearModel model;
    model.setOIP3_dBm(oip3_dBm);
    model.setEnabled(true);
    const double amplitude = std::sqrt(std::pow(10.0, gain_dB / 10.0));
    return model.process(tones, [amplitude](double) { return amplitude; });
}

} // namespace

TEST_CASE("AMP OIP3 table sets the third-order products of each tone", "[amp][nonlinear]") {
    Scratch scratch;
    const auto path =
        scratch.write("oip3_table.amp", kNetwork + "OIP3 dBm\nFREQ GHZ\n1.0 10.0\n1.3 20.0\n");

    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setGain_dB(20.0);
    amp.setOIP3_dBm(-50.0); // the AMP table replaces this scalar
    amp.setEnableNonlinear(true);
    amp.setAmpFilepath(path);
    REQUIRE(amp.ampLoaded());

    const std::vector<Spectrum::Tone> tones = {{1.0e9, -30.0, 0.0}, {1.3e9, -30.0, 0.0}};
    Spectrum source;
    source.tones = tones;
    amp.node().inputs[0] = &source;
    amp.update(0.0);
    const auto &out = amp.node().outputs[0].tones;

    const auto at_f1 = scalarProducts(10.0, tones, 20.0); // intercept at 1.0 GHz
    const auto at_f2 = scalarProducts(20.0, tones, 20.0); // intercept at 1.3 GHz
    // Third harmonic of each tone.
    CHECK(powerAt(out, 3.0e9) == Approx(powerAt(at_f1.extra_tones, 3.0e9)));
    CHECK(powerAt(out, 3.9e9) == Approx(powerAt(at_f2.extra_tones, 3.9e9)));
    // IM3 products. 2f1 - f2 and 2f1 + f2 square f1; 2f2 + f1 and |f1 - 2f2| square f2.
    CHECK(powerAt(out, 0.7e9) == Approx(powerAt(at_f1.extra_tones, 0.7e9)));
    CHECK(powerAt(out, 3.3e9) == Approx(powerAt(at_f1.extra_tones, 3.3e9)));
    CHECK(powerAt(out, 1.6e9) == Approx(powerAt(at_f2.extra_tones, 1.6e9)));
    CHECK(powerAt(out, 3.6e9) == Approx(powerAt(at_f2.extra_tones, 3.6e9)));
}

TEST_CASE("AMP IIP3 section sets OIP3 as IIP3 plus the gain at each frequency",
          "[amp][nonlinear]") {
    Scratch scratch;
    const auto path =
        scratch.write("iip3_table.amp", kNetwork + "IIP3 dBm\nFREQ GHZ\n1.0 -10.0\n1.3 -8.0\n");

    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setGain_dB(20.0);
    amp.setEnableNonlinear(true);
    amp.setAmpFilepath(path);
    REQUIRE(amp.ampLoaded());

    const std::vector<Spectrum::Tone> tones = {{1.0e9, -30.0, 0.0}, {1.3e9, -30.0, 0.0}};
    Spectrum source;
    source.tones = tones;
    amp.node().inputs[0] = &source;
    amp.update(0.0);
    const auto &out = amp.node().outputs[0].tones;

    // With 20 dB of gain, OIP3 is 10 dBm at 1.0 GHz and 12 dBm at 1.3 GHz.
    const auto at_f1 = scalarProducts(10.0, tones, 20.0);
    const auto at_f2 = scalarProducts(12.0, tones, 20.0);
    CHECK(powerAt(out, 3.0e9) == Approx(powerAt(at_f1.extra_tones, 3.0e9)));
    CHECK(powerAt(out, 3.9e9) == Approx(powerAt(at_f2.extra_tones, 3.9e9)));
}

TEST_CASE("Derived AMP intercepts keep every product finite at a vanishing gain",
          "[amp][nonlinear]") {
    Scratch scratch;
    const auto path =
        scratch.write("vanishing_gain.amp", kNetwork + "IIP3 dBm\nFREQ GHZ\n1.0 0.0\n");

    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setGain_dB(-300.0); // OIP3 = IIP3 + G would fall far below the -100 dBm floor
    amp.setEnableNonlinear(true);
    amp.setAmpFilepath(path);
    REQUIRE(amp.ampLoaded());

    Spectrum source;
    source.tones = {{1.0e9, -30.0, 0.0}, {1.3e9, -30.0, 0.0}};
    amp.node().inputs[0] = &source;
    amp.update(0.0);
    for (const auto &tone : amp.node().outputs[0].tones)
        CHECK(std::isfinite(tone.power_dBm));
}

TEST_CASE("AMP reference and intercepts survive serialize and deserialize", "[amp][project]") {
    Scratch scratch;
    const auto path =
        scratch.write("persist.amp", kNetwork + "OIP3 dBm\nFREQ GHZ\n1.0 10.0\n1.3 20.0\n");

    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setAmpFilepath(path);
    const auto saved = amp.serialize();
    REQUIRE(saved.at("amp_filepath") == path);

    AmplifierEngine restored(1, graph);
    restored.deserialize(saved);
    REQUIRE(restored.ampLoaded());
    CHECK(restored.ampFilepath() == path);
    REQUIRE(restored.ampData() != nullptr);
    REQUIRE(restored.ampData()->oip3.has_value());
    CHECK(restored.ampData()->oip3->dBmAt(1.3e9) == Approx(20.0));
}

TEST_CASE("A rejected AMP file keeps its reference and falls back to scalar OIP3",
          "[amp][diagnostics]") {
    Scratch scratch;
    const auto path = scratch.write("rejected.amp", kNetwork + "OIP3 dBX\nFREQ GHZ\n1.0 10.0\n");

    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setGain_dB(20.0);
    amp.setOIP3_dBm(25.0);
    amp.setEnableNonlinear(true);
    amp.setAmpFilepath(path);

    CHECK_FALSE(amp.ampLoaded());
    CHECK(amp.ampFilepath() == path);
    CHECK(amp.ampError().find(path + ":4:") != std::string::npos);

    const std::vector<Spectrum::Tone> tones = {{1.0e9, -30.0, 0.0}, {1.3e9, -30.0, 0.0}};
    Spectrum source;
    source.tones = tones;
    amp.node().inputs[0] = &source;
    amp.update(0.0);
    const auto &out = amp.node().outputs[0].tones;

    // The scalar 25 dBm OIP3 applies at every frequency.
    const auto scalar = scalarProducts(25.0, tones, 20.0);
    CHECK(powerAt(out, 3.0e9) == Approx(powerAt(scalar.extra_tones, 3.0e9)));
    CHECK(powerAt(out, 3.9e9) == Approx(powerAt(scalar.extra_tones, 3.9e9)));
}

TEST_CASE("Clearing the AMP path restores the scalar OIP3", "[amp][nonlinear]") {
    Scratch scratch;
    const auto path =
        scratch.write("cleared.amp", kNetwork + "OIP3 dBm\nFREQ GHZ\n1.0 10.0\n1.3 20.0\n");

    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setGain_dB(20.0);
    amp.setOIP3_dBm(25.0);
    amp.setEnableNonlinear(true);
    amp.setAmpFilepath(path);
    REQUIRE(amp.ampLoaded());
    amp.setAmpFilepath("");

    CHECK_FALSE(amp.ampLoaded());
    CHECK(amp.ampFilepath().empty());
    CHECK(amp.ampError().empty());

    const std::vector<Spectrum::Tone> tones = {{1.0e9, -30.0, 0.0}, {1.3e9, -30.0, 0.0}};
    Spectrum source;
    source.tones = tones;
    amp.node().inputs[0] = &source;
    amp.update(0.0);
    const auto scalar = scalarProducts(25.0, tones, 20.0);
    CHECK(powerAt(amp.node().outputs[0].tones, 3.0e9) ==
          Approx(powerAt(scalar.extra_tones, 3.0e9)));
}
