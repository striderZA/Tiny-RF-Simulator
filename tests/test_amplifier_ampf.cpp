#include "amplifier_engine.h"
#include "node_graph_engine.h"
#include "nonlinear_model.h"
#include "test_temp_paths.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using Catch::Approx;

namespace {

// The magic line and DEVICE line that begin every amp-f file in these tests.
const std::string kHead = "# amp-f v1\nDEVICE TEST\n";

// A network section (S RI) so a legacy AMP file meets the MathWorks rule for S, Y, Z, or POUT.
const std::string kNetwork = "S RI R 50\nFREQ GHZ\n1.0 0.5 0.0 2.0 0.0 0.0 0.0 0.3 0.0\n";

// Writes files in a per-process directory that is removed when the test ends.
class Scratch {
  public:
    Scratch()
        : m_dir(std::filesystem::temp_directory_path() /
                ("rfsim_amplifier_ampf_" + test_temp_paths::processTag() + "_" +
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

// Products of the scalar model with fixed intercepts, in ideal mode at the given gain.
NonlinearModel::Result scalarProducts(double oip2_dBm, double oip3_dBm,
                                      const std::vector<Spectrum::Tone> &tones, double gain_dB) {
    NonlinearModel model;
    model.setOIP2_dBm(oip2_dBm);
    model.setOIP3_dBm(oip3_dBm);
    model.setEnabled(true);
    const double amplitude = std::sqrt(std::pow(10.0, gain_dB / 10.0));
    return model.process(tones, [amplitude](double) { return amplitude; });
}

// Runs one update() with the tones as input and returns the output tones.
std::vector<Spectrum::Tone> runAmplifier(AmplifierEngine &amp,
                                         const std::vector<Spectrum::Tone> &tones) {
    Spectrum source;
    source.tones = tones;
    amp.node().inputs[0] = &source;
    amp.update(0.0);
    amp.node().inputs[0] = nullptr;
    return amp.node().outputs[0].tones;
}

const std::vector<Spectrum::Tone> kTwoTones = {{1.0e9, -30.0, 0.0}, {1.3e9, -30.0, 0.0}};

} // namespace

TEST_CASE("amp-f OIP3 column sets the third-order products of each tone", "[ampf][nonlinear]") {
    Scratch scratch;
    const auto path = scratch.write(
        "oip3.ampf", kHead + "# COLUMNS: OIP3_dBm\nBEGIN\n1000 10.0\n1300 20.0\nEND\n");

    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setGain_dB(20.0);
    amp.setOIP3_dBm(0.0); // the file's column replaces this scalar
    amp.setEnableNonlinear(true);
    amp.setAmpfFilepath(path);
    REQUIRE(amp.ampfLoaded());
    REQUIRE(amp.ampfBlockSelected());

    const auto out = runAmplifier(amp, kTwoTones);
    const auto at_f1 = scalarProducts(100.0, 10.0, kTwoTones, 20.0); // 10 dBm at 1.0 GHz
    const auto at_f2 = scalarProducts(100.0, 20.0, kTwoTones, 20.0); // 20 dBm at 1.3 GHz
    CHECK(powerAt(out, 3.0e9) == Approx(powerAt(at_f1.extra_tones, 3.0e9)));
    CHECK(powerAt(out, 0.7e9) == Approx(powerAt(at_f1.extra_tones, 0.7e9)));
    CHECK(powerAt(out, 3.9e9) == Approx(powerAt(at_f2.extra_tones, 3.9e9)));
    CHECK(powerAt(out, 1.6e9) == Approx(powerAt(at_f2.extra_tones, 1.6e9)));
}

TEST_CASE("amp-f OIP2 column sets the second harmonic and the IM2 products", "[ampf][nonlinear]") {
    Scratch scratch;
    const auto path = scratch.write(
        "oip2.ampf", kHead + "# COLUMNS: OIP2_dBm\nBEGIN\n1000 10.0\n1300 20.0\nEND\n");

    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setGain_dB(20.0);
    amp.setEnableNonlinear(true);
    amp.setAmpfFilepath(path);
    REQUIRE(amp.ampfBlockSelected());

    const auto out = runAmplifier(amp, kTwoTones);
    const auto at_f1 = scalarProducts(10.0, 100.0, kTwoTones, 20.0);
    const auto at_f2 = scalarProducts(20.0, 100.0, kTwoTones, 20.0);
    // The second harmonic of each tone uses that tone's OIP2.
    CHECK(powerAt(out, 2.0e9) == Approx(powerAt(at_f1.extra_tones, 2.0e9)));
    CHECK(powerAt(out, 2.6e9) == Approx(powerAt(at_f2.extra_tones, 2.6e9)));
    // The IM2 products use the lower OIP2, 10 dBm, so they come from the first reference.
    CHECK(powerAt(out, 0.3e9) == Approx(powerAt(at_f1.extra_tones, 0.3e9)));
    CHECK(powerAt(out, 2.3e9) == Approx(powerAt(at_f1.extra_tones, 2.3e9)));
}

TEST_CASE("A NaN in the selected block falls back to the scalar and leaves no NaN tones",
          "[ampf][nonlinear]") {
    Scratch scratch;
    const auto path =
        scratch.write("nan.ampf", kHead + "# COLUMNS: OIP3_dBm\nBEGIN\n1000 NaN\n1300 20.0\nEND\n");

    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setGain_dB(20.0);
    amp.setOIP3_dBm(12.0);
    amp.setEnableNonlinear(true);
    amp.setAmpfFilepath(path);
    REQUIRE(amp.ampfBlockSelected());

    const auto out = runAmplifier(amp, kTwoTones);
    for (const auto &tone : out) {
        CHECK(std::isfinite(tone.freq_Hz));
        CHECK(std::isfinite(tone.power_dBm));
    }
    // At 1.0 GHz the NaN takes the scalar 12 dBm. At 1.3 GHz the file's 20 dBm applies.
    const auto scalar = scalarProducts(100.0, 12.0, kTwoTones, 20.0);
    const auto at_f2 = scalarProducts(100.0, 20.0, kTwoTones, 20.0);
    CHECK(powerAt(out, 3.0e9) == Approx(powerAt(scalar.extra_tones, 3.0e9)));
    CHECK(powerAt(out, 3.9e9) == Approx(powerAt(at_f2.extra_tones, 3.9e9)));
}

TEST_CASE("amp-f OP1dB column seeds OIP3 at 9.6 dB above P1dB when OIP3 is absent",
          "[ampf][nonlinear]") {
    Scratch scratch;
    const auto path = scratch.write(
        "op1db.ampf", kHead + "# COLUMNS: OP1dB_dBm\nBEGIN\n1000 0.4\n1300 0.4\nEND\n");

    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setGain_dB(20.0);
    amp.setEnableNonlinear(true);
    amp.setAmpfFilepath(path);
    REQUIRE(amp.ampfBlockSelected());

    // 0.4 dBm P1dB plus 9.6 dB is an OIP3 of 10 dBm.
    const auto out = runAmplifier(amp, kTwoTones);
    const auto reference = scalarProducts(100.0, 10.0, kTwoTones, 20.0);
    CHECK(powerAt(out, 3.0e9) == Approx(powerAt(reference.extra_tones, 3.0e9)));
    CHECK(powerAt(out, 3.9e9) == Approx(powerAt(reference.extra_tones, 3.9e9)));
}

TEST_CASE("amp-f IIP3 column gives OIP3 as IIP3 plus the gain where OIP3 is absent",
          "[ampf][nonlinear]") {
    Scratch scratch;
    const auto path = scratch.write(
        "iip3.ampf",
        kHead + "# COLUMNS: OIP3_dBm IIP3_dBm\nBEGIN\n1000 NaN -10.0\n1300 NaN -8.0\nEND\n");

    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setGain_dB(20.0);
    amp.setEnableNonlinear(true);
    amp.setAmpfFilepath(path);
    REQUIRE(amp.ampfBlockSelected());

    // With 20 dB of gain, OIP3 is 10 dBm at 1.0 GHz and 12 dBm at 1.3 GHz.
    const auto out = runAmplifier(amp, kTwoTones);
    const auto at_f1 = scalarProducts(100.0, 10.0, kTwoTones, 20.0);
    const auto at_f2 = scalarProducts(100.0, 12.0, kTwoTones, 20.0);
    CHECK(powerAt(out, 3.0e9) == Approx(powerAt(at_f1.extra_tones, 3.0e9)));
    CHECK(powerAt(out, 3.9e9) == Approx(powerAt(at_f2.extra_tones, 3.9e9)));
}

TEST_CASE("amp-f selection picks the block with the selected VAR and COND values",
          "[ampf][select]") {
    Scratch scratch;
    const auto path =
        scratch.write("select.ampf", kHead + "# COLUMNS: OIP3_dBm\n"
                                             "VAR VG=1\nBEGIN\n1000 10.0\n1300 10.0\nEND\n"
                                             "VAR VG=2\nBEGIN\n1000 30.0\n1300 30.0\nEND\n");

    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setGain_dB(20.0);
    amp.setOIP3_dBm(0.0);
    amp.setEnableNonlinear(true);
    amp.setAmpfFilepath(path);

    // Two blocks and no selection: the file is loaded, but no block is active.
    CHECK(amp.ampfLoaded());
    CHECK_FALSE(amp.ampfBlockSelected());
    CHECK_FALSE(amp.ampfError().empty());

    amp.setAmpfSelection({{"VG", 2.0}});
    REQUIRE(amp.ampfBlockSelected());
    CHECK(amp.ampfError().empty());
    const auto out = runAmplifier(amp, kTwoTones);
    const auto reference = scalarProducts(100.0, 30.0, kTwoTones, 20.0);
    CHECK(powerAt(out, 3.0e9) == Approx(powerAt(reference.extra_tones, 3.0e9)));

    // A selection that matches no block falls back to the scalar and says why.
    amp.setAmpfSelection({{"VG", 3.0}});
    CHECK_FALSE(amp.ampfBlockSelected());
    CHECK_FALSE(amp.ampfError().empty());
    const auto fallback_out = runAmplifier(amp, kTwoTones);
    const auto fallback = scalarProducts(100.0, 0.0, kTwoTones, 20.0);
    CHECK(powerAt(fallback_out, 3.0e9) == Approx(powerAt(fallback.extra_tones, 3.0e9)));
}

TEST_CASE("A rejected amp-f file keeps its path and error; with no AMP file, the scalar applies",
          "[ampf][parse]") {
    Scratch scratch;
    const auto path = scratch.write(
        "rejected.ampf", "# amp-f v2\nDEVICE TEST\n# COLUMNS: OIP3_dBm\nBEGIN\n1000 10.0\nEND\n");

    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setGain_dB(20.0);
    amp.setOIP3_dBm(0.0);
    amp.setEnableNonlinear(true);
    amp.setAmpfFilepath(path);

    CHECK_FALSE(amp.ampfLoaded());
    CHECK(amp.ampfFilepath() == path);
    CHECK(amp.ampfError().find(path + ":1:") != std::string::npos);

    const auto out = runAmplifier(amp, kTwoTones);
    const auto scalar = scalarProducts(100.0, 0.0, kTwoTones, 20.0);
    CHECK(powerAt(out, 3.0e9) == Approx(powerAt(scalar.extra_tones, 3.0e9)));
}

TEST_CASE("An amp-f block replaces the intercepts of a legacy AMP file", "[ampf][nonlinear]") {
    Scratch scratch;
    const auto legacy =
        scratch.write("legacy.amp", kNetwork + "OIP3 dBm\nFREQ GHZ\n1.0 10.0\n1.3 10.0\n");
    const auto ampf = scratch.write(
        "block.ampf", kHead + "# COLUMNS: OIP3_dBm\nBEGIN\n1000 30.0\n1300 30.0\nEND\n");

    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setGain_dB(20.0);
    amp.setOIP3_dBm(0.0);
    amp.setEnableNonlinear(true);
    amp.setAmpFilepath(legacy);
    REQUIRE(amp.ampLoaded());
    amp.setAmpfFilepath(ampf);
    REQUIRE(amp.ampfBlockSelected());

    const auto out = runAmplifier(amp, kTwoTones);
    const auto reference = scalarProducts(100.0, 30.0, kTwoTones, 20.0);
    CHECK(powerAt(out, 3.0e9) == Approx(powerAt(reference.extra_tones, 3.0e9)));
}

TEST_CASE("A legacy AMP OIP3 applies unless an amp-f block is active", "[ampf][nonlinear]") {
    Scratch scratch;
    const auto legacy = scratch.write("precedence_legacy.amp",
                                      kNetwork + "OIP3 dBm\nFREQ GHZ\n1.0 10.0\n1.3 10.0\n");
    const auto rejected =
        scratch.write("precedence_rejected.ampf",
                      "# amp-f v2\nDEVICE TEST\n# COLUMNS: OIP3_dBm\nBEGIN\n1000 30.0\nEND\n");
    const auto unmatched =
        scratch.write("precedence_unmatched.ampf",
                      kHead + "# COLUMNS: OIP3_dBm\nVAR VG=1\nBEGIN\n1000 30.0\n1300 30.0\nEND\n"
                              "VAR VG=2\nBEGIN\n1000 30.0\n1300 30.0\nEND\n");
    const auto nan_block = scratch.write(
        "precedence_nan.ampf", kHead + "# COLUMNS: OIP3_dBm\nBEGIN\n1000 NaN\n1300 NaN\nEND\n");

    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setGain_dB(20.0);
    amp.setOIP3_dBm(0.0);
    amp.setEnableNonlinear(true);
    amp.setAmpFilepath(legacy);
    REQUIRE(amp.ampLoaded());

    // The legacy file gives 10 dBm at both tones. It has no OIP2, so OIP2 stays at the
    // scalar model default of 100 dBm.
    const auto legacy_ref = scalarProducts(100.0, 10.0, kTwoTones, 20.0);

    SECTION("a rejected amp-f file leaves the legacy OIP3 in use") {
        amp.setAmpfFilepath(rejected);
        REQUIRE_FALSE(amp.ampfBlockSelected());
        CHECK_FALSE(amp.ampfError().empty());
        const auto out = runAmplifier(amp, kTwoTones);
        CHECK(powerAt(out, 3.0e9) == Approx(powerAt(legacy_ref.extra_tones, 3.0e9)));
        CHECK(powerAt(out, 2.3e9) == Approx(powerAt(legacy_ref.extra_tones, 2.3e9)));
    }

    SECTION("an unmatched amp-f file leaves the legacy OIP3 in use") {
        amp.setAmpfFilepath(unmatched);
        REQUIRE(amp.ampfLoaded());
        REQUIRE_FALSE(amp.ampfBlockSelected());
        CHECK_FALSE(amp.ampfError().empty());
        const auto out = runAmplifier(amp, kTwoTones);
        CHECK(powerAt(out, 3.0e9) == Approx(powerAt(legacy_ref.extra_tones, 3.0e9)));
    }

    SECTION("an active block replaces the legacy file, and NaN falls back to the scalar") {
        amp.setOIP3_dBm(20.0);
        amp.setAmpfFilepath(nan_block);
        REQUIRE(amp.ampfBlockSelected());
        const auto out = runAmplifier(amp, kTwoTones);
        const auto scalar_ref = scalarProducts(100.0, 20.0, kTwoTones, 20.0);
        CHECK(powerAt(out, 3.0e9) == Approx(powerAt(scalar_ref.extra_tones, 3.0e9)));
    }
}

TEST_CASE("serialize and deserialize keep the amp-f reference and block selection",
          "[ampf][serialize]") {
    Scratch scratch;
    const auto path =
        scratch.write("round_trip.ampf", kHead + "# COLUMNS: OIP3_dBm\n"
                                                 "VAR VG=1\nBEGIN\n1000 10.0\n1300 10.0\nEND\n"
                                                 "VAR VG=2\nBEGIN\n1000 30.0\n1300 30.0\nEND\n");

    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setGain_dB(20.0);
    amp.setOIP3_dBm(0.0);
    amp.setEnableNonlinear(true);
    amp.setAmpfFilepath(path);
    amp.setAmpfSelection({{"VG", 2.0}});
    REQUIRE(amp.ampfBlockSelected());

    const auto saved = amp.serialize();
    CHECK(saved.at("ampf_filepath") == path);
    CHECK(saved.at("ampf_block").at("VG").get<double>() == Approx(2.0));

    AmplifierEngine restored(1, graph);
    restored.deserialize(saved);
    CHECK(restored.ampfFilepath() == path);
    REQUIRE(restored.ampfLoaded());
    REQUIRE(restored.ampfBlockSelected());
    CHECK(restored.ampfSelection().size() == 1);
    CHECK(restored.ampfSelection().at("VG") == Approx(2.0));

    const auto original_out = runAmplifier(amp, kTwoTones);
    const auto restored_out = runAmplifier(restored, kTwoTones);
    CHECK(powerAt(restored_out, 3.0e9) == Approx(powerAt(original_out, 3.0e9)));
}
