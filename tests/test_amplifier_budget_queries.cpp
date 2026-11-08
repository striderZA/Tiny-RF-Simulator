#include "amplifier_engine.h"
#include "node_graph_engine.h"
#include "test_temp_paths.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

using Catch::Approx;

namespace {

// A network section (S RI R 50) so a legacy AMP file meets the MathWorks rule for S, Y, Z,
// or POUT data.
const std::string kNetwork = "S RI R 50\nFREQ GHZ\n1.0 0.5 0.0 2.0 0.0 0.0 0.0 0.3 0.0\n";

// The magic line and DEVICE line that begin every amp-f v1 file in these tests.
const std::string kAmpfHead = "# amp-f v1\nDEVICE TEST\n";

// Writes files in a per-process directory that is removed when the test ends.
class Scratch {
  public:
    Scratch()
        : m_dir(std::filesystem::temp_directory_path() /
                ("rfsim_amplifier_budget_" + test_temp_paths::processTag() + "_" +
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

// An amp-f v1 file with one block: the given COLUMNS header and row values at 1 GHz.
std::string ampfFile(const std::string &columns, const std::string &values) {
    return kAmpfHead + "# COLUMNS: " + columns + "\nBEGIN\n1000 " + values + "\nEND\n";
}

} // namespace

TEST_CASE("Amplifier scalar intercepts are returned per frequency", "[amplifier][budget]") {
    NodeGraphEngine graph;
    AmplifierEngine amp(1, graph);
    amp.deserialize({{"gain_dB", 10.0}, {"oip3_dBm", 30.0}, {"p1db_dBm", 20.0}});
    REQUIRE(amp.oip3At_Hz(1e9) == Approx(30.0));
    REQUIRE(amp.p1dbAt_Hz(1e9) == Approx(20.0));
}

TEST_CASE("Amplifier AMP OIP3 and IIP3 plus gain are returned", "[amplifier][budget]") {
    Scratch scratch;

    SECTION("an OIP3 section is used as given") {
        NodeGraphEngine graph;
        AmplifierEngine amp(1, graph);
        amp.deserialize({{"gain_dB", 10.0}, {"oip3_dBm", 0.0}});
        amp.setAmpFilepath(scratch.write("oip3.amp", kNetwork + "OIP3 dBm\nFREQ GHz\n1.0 32.0\n"));
        REQUIRE(amp.ampLoaded());
        REQUIRE(amp.oip3At_Hz(1e9) == Approx(32.0));
    }

    SECTION("an IIP3 section gives OIP3 as IIP3 plus the gain") {
        NodeGraphEngine graph;
        AmplifierEngine amp(1, graph);
        amp.deserialize({{"gain_dB", 10.0}, {"oip3_dBm", 0.0}});
        amp.setAmpFilepath(scratch.write("iip3.amp", kNetwork + "IIP3 dBm\nFREQ GHz\n1.0 20.0\n"));
        REQUIRE(amp.ampLoaded());
        REQUIRE(amp.oip3At_Hz(1e9) == Approx(30.0));
    }
}

TEST_CASE("Amplifier amp-f intercepts use OP1dB and IP1dB plus gain", "[amplifier][budget]") {
    Scratch scratch;

    SECTION("OIP3 and OP1dB columns are used as given") {
        NodeGraphEngine graph;
        AmplifierEngine amp(1, graph);
        amp.deserialize({{"gain_dB", 10.0}, {"oip3_dBm", 30.0}, {"p1db_dBm", 20.0}});
        amp.setAmpfFilepath(scratch.write(
            "full.ampf", ampfFile("OIP3_dBm IIP3_dBm OP1dB_dBm IP1dB_dBm", "32.0 20.0 22.0 10.0")));
        REQUIRE(amp.ampfLoaded());
        REQUIRE(amp.ampfBlockSelected());
        REQUIRE(amp.oip3At_Hz(1e9) == Approx(32.0));
        REQUIRE(amp.p1dbAt_Hz(1e9) == Approx(22.0));
    }

    SECTION("without OIP3 the gain-scaled IIP3 applies") {
        NodeGraphEngine graph;
        AmplifierEngine amp(1, graph);
        amp.deserialize({{"gain_dB", 10.0}, {"oip3_dBm", 30.0}, {"p1db_dBm", 20.0}});
        amp.setAmpfFilepath(scratch.write(
            "no_oip3.ampf", ampfFile("IIP3_dBm OP1dB_dBm IP1dB_dBm", "20.0 22.0 10.0")));
        REQUIRE(amp.ampfBlockSelected());
        REQUIRE(amp.oip3At_Hz(1e9) == Approx(30.0));
    }

    SECTION("without OP1dB the gain-scaled IP1dB applies") {
        NodeGraphEngine graph;
        AmplifierEngine amp(1, graph);
        amp.deserialize({{"gain_dB", 10.0}, {"oip3_dBm", 30.0}, {"p1db_dBm", 20.0}});
        amp.setAmpfFilepath(scratch.write("no_op1db.ampf", ampfFile("IP1dB_dBm", "10.0")));
        REQUIRE(amp.ampfBlockSelected());
        REQUIRE(amp.p1dbAt_Hz(1e9) == Approx(20.0));
    }
}

TEST_CASE("Amplifier S-parameter gain is used for IIP3 conversion", "[amplifier][budget]") {
    Scratch scratch;

    NodeGraphEngine graph;
    AmplifierEngine amp(1, graph);
    amp.deserialize({{"gain_dB", 10.0}, {"oip3_dBm", 0.0}});

    // S21 = 0.5 at 1 GHz (-6.0206 dB).
    amp.setSParamFilepath(scratch.write("half.s2p", "# Hz S RI R 50\n"
                                                    "1000000000 0 0 0.5 0 0 0 0 0\n"));
    REQUIRE(amp.sparamMode());

    amp.setAmpFilepath(scratch.write("iip3.amp", kNetwork + "IIP3 dBm\nFREQ GHz\n1.0 20.0\n"));
    REQUIRE(amp.ampLoaded());

    // IIP3 20 dBm + S21 gain (-6.0206 dB) = 13.9794 dBm.
    REQUIRE(amp.oip3At_Hz(1e9) == Approx(20.0 + 20.0 * std::log10(0.5)).epsilon(1e-9));
}

TEST_CASE("Amplifier intercepts hold their edge value above the file band", "[amplifier][budget]") {
    Scratch scratch;

    SECTION("an amp-f block holds its last row") {
        NodeGraphEngine graph;
        AmplifierEngine amp(1, graph);
        amp.deserialize({{"gain_dB", 10.0}, {"oip3_dBm", 30.0}, {"p1db_dBm", 20.0}});
        amp.setAmpfFilepath(
            scratch.write("edge.ampf", ampfFile("OIP3_dBm OP1dB_dBm", "32.0 22.0")));
        REQUIRE(amp.ampfBlockSelected());
        // The block's 1 GHz row is the edge value for every frequency above it.
        REQUIRE(amp.oip3At_Hz(5e9) == Approx(32.0));
        REQUIRE(amp.p1dbAt_Hz(5e9) == Approx(22.0));
    }

    SECTION("a legacy AMP OIP3 section holds its last row") {
        NodeGraphEngine graph;
        AmplifierEngine amp(1, graph);
        amp.deserialize({{"gain_dB", 10.0}, {"oip3_dBm", 30.0}, {"p1db_dBm", 20.0}});
        amp.setAmpFilepath(scratch.write("edge.amp", kNetwork + "OIP3 dBm\nFREQ GHz\n1.0 32.0\n"));
        REQUIRE(amp.ampLoaded());
        REQUIRE(amp.oip3At_Hz(5e9) == Approx(32.0));
    }
}
