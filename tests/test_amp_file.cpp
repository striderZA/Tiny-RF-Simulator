#include "amp_file.h"
#include "test_temp_paths.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using Catch::Approx;

namespace {

// A valid network section (S RI, nine values per row). Files that should parse
// include one, because an AMP file needs S, Y, Z, or POUT data.
const std::string kNetwork = "S RI R 50\nFREQ GHZ\n1.0 0.5 0.0 2.0 0.0 0.0 0.0 0.3 0.0\n";

// Test files live in a per-process directory that is removed when the test ends.
class Scratch {
  public:
    Scratch()
        : m_dir(std::filesystem::temp_directory_path() /
                ("rfsim_amp_file_" + test_temp_paths::processTag() + "_" +
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

// A malformed section is reported at its line, and the message names the file.
void requireDiagnostic(const AmpParseResult &result, const std::string &path, int line,
                       const std::string &phrase) {
    REQUIRE_FALSE(result.file.has_value());
    CHECK(result.error.find(path + ":" + std::to_string(line) + ": ") != std::string::npos);
    CHECK(result.error.find(phrase) != std::string::npos);
}

} // namespace

TEST_CASE("AMP scalar OIP3 is one value at every frequency", "[amp][parse]") {
    Scratch scratch;
    const auto result =
        AmpFileParser::parse(scratch.write("scalar.amp", kNetwork + "OIP3 dBm\nFREQ GHz\n30.5\n"));

    REQUIRE(result.file.has_value());
    REQUIRE(result.file->oip3.has_value());
    REQUIRE_FALSE(result.file->iip3.has_value());
    REQUIRE(result.file->oip3->points.size() == 1);
    CHECK(result.file->oip3->dBmAt(1.0e9) == Approx(30.5));
    CHECK(result.file->oip3->dBmAt(5.0e9) == Approx(30.5));
}

TEST_CASE("AMP scalar IP3 ignores the units on its FREQ line", "[amp][parse]") {
    // The MathWorks page says the second header line of a scalar section is ignored.
    Scratch scratch;
    const auto result = AmpFileParser::parse(
        scratch.write("scalar_freq_units.amp", kNetwork + "OIP3 dBm\nFREQ THZ\n30.5\n"));

    REQUIRE(result.file.has_value());
    REQUIRE(result.file->oip3.has_value());
    CHECK(result.file->oip3->dBmAt(1.0e9) == Approx(30.5));
}

TEST_CASE("AMP per-frequency IIP3 sorts rows and reads per-line frequency units", "[amp][parse]") {
    Scratch scratch;
    const auto result = AmpFileParser::parse(scratch.write(
        "per_frequency.amp", kNetwork + "IIP3 dBm\nFREQ GHz\n2.2 14.0\n2.0 10.0\n2100MHZ 12.0\n"));

    REQUIRE(result.file.has_value());
    REQUIRE_FALSE(result.file->oip3.has_value());
    REQUIRE(result.file->iip3.has_value());
    const auto &curve = *result.file->iip3;
    REQUIRE(curve.points.size() == 3);
    CHECK(curve.points[0].freq_Hz == Approx(2.0e9));
    CHECK(curve.points[1].freq_Hz == Approx(2.1e9));
    CHECK(curve.points[2].freq_Hz == Approx(2.2e9));

    // Linear between points; held at the nearest point outside them.
    CHECK(curve.dBmAt(2.05e9) == Approx(11.0));
    CHECK(curve.dBmAt(1.0e9) == Approx(10.0));
    CHECK(curve.dBmAt(3.0e9) == Approx(14.0));
}

TEST_CASE("AMP IP3 units default to W and convert to dBm", "[amp][parse][units]") {
    Scratch scratch;

    SECTION("a header without units means watts") {
        // 1 W is 30 dBm.
        const auto result =
            AmpFileParser::parse(scratch.write("watts.amp", kNetwork + "OIP3\nFREQ GHz\n1.0\n"));
        REQUIRE(result.file.has_value());
        CHECK(result.file->oip3->dBmAt(1.0e9) == Approx(30.0));
    }

    SECTION("a mW header with a W override on one row") {
        // 100 mW is 20 dBm; 10 W is 40 dBm.
        const auto result = AmpFileParser::parse(
            scratch.write("milliwatts.amp", kNetwork + "IIP3 mW\nFREQ MHz\n1000 100\n2000 10W\n"));
        REQUIRE(result.file.has_value());
        CHECK(result.file->iip3->dBmAt(1.0e9) == Approx(20.0));
        CHECK(result.file->iip3->dBmAt(2.0e9) == Approx(40.0));
    }

    SECTION("a dBW header") {
        // -10 dBW is 20 dBm.
        const auto result = AmpFileParser::parse(
            scratch.write("dbw.amp", kNetwork + "OIP3 dBW\nFREQ GHz\n1.0 -10\n"));
        REQUIRE(result.file.has_value());
        CHECK(result.file->oip3->dBmAt(1.0e9) == Approx(20.0));
    }
}

TEST_CASE("AMP frequency units Hz, kHz, MHz, and GHz scale to Hz", "[amp][parse][units]") {
    Scratch scratch;
    const auto result = AmpFileParser::parse(scratch.write(
        "frequency_units.amp", kNetwork + "OIP3 dBm\nFREQ KHZ\n2000000 10\n3000MHZ 20\n"));

    REQUIRE(result.file.has_value());
    const auto &curve = *result.file->oip3;
    CHECK(curve.dBmAt(2.0e9) == Approx(10.0));
    CHECK(curve.dBmAt(2.5e9) == Approx(15.0));
    CHECK(curve.dBmAt(3.0e9) == Approx(20.0));
}

TEST_CASE("AMP comments, blank lines, CRLF endings, and case are accepted", "[amp][parse]") {
    Scratch scratch;
    const std::string text = "* AMP sample with comments\r\n"
                             "! a second comment line\r\n"
                             "\r\n"
                             "s ri r 50 ; trailing comment\r\n"
                             "freq ghz\r\n"
                             "1.0 0.5 0.0 2.0 0.0 0.0 0.0 0.3 0.0 ; row comment\r\n"
                             "\r\n"
                             "oip3 DBM\r\n"
                             "freq GHZ\r\n"
                             "* row comment line\r\n"
                             "2.0GHZ 33.0 ; trailing\r\n";
    const auto result = AmpFileParser::parse(scratch.write("syntax.amp", text));

    REQUIRE(result.file.has_value());
    REQUIRE(result.file->oip3.has_value());
    CHECK(result.file->oip3->dBmAt(1.0e9) == Approx(33.0));
}

TEST_CASE("AMP parser rejects malformed IP3 sections with file and line",
          "[amp][parse][diagnostics]") {
    Scratch scratch;
    struct Case {
        std::string name;
        std::string text;
        int line;
        std::string phrase;
    };
    const std::vector<Case> cases = {
        {"bad_units.amp", kNetwork + "OIP3 dBX\nFREQ GHZ\n2.0 10\n", 4, "unknown IP3 units"},
        {"no_freq_header.amp", kNetwork + "OIP3 dBm\n2.0 10\n", 5, "expected a FREQ header line"},
        {"nan_value.amp", kNetwork + "OIP3 dBm\nFREQ GHZ\n2.0 nan\n", 6,
         "expected a finite IP3 value"},
        {"inf_frequency.amp", kNetwork + "OIP3 dBm\nFREQ GHZ\ninf 10\n", 6,
         "expected a finite frequency"},
        {"three_columns.amp", kNetwork + "OIP3 dBm\nFREQ GHZ\n2.0 10 5\n", 6,
         "expected a frequency and a value"},
        {"duplicate_frequency.amp", kNetwork + "OIP3 dBm\nFREQ GHZ\n2.0 10\n2.0 11\n", 7,
         "duplicate frequency"},
        {"duplicate_section.amp",
         kNetwork + "OIP3 dBm\nFREQ GHZ\n2.0 10\nOIP3 dBm\nFREQ GHZ\n2.0 11\n", 7,
         "duplicate OIP3 section"},
        {"scalar_extra_row.amp", kNetwork + "OIP3 dBm\nFREQ GHZ\n30\n31\n", 7,
         "takes one value line"},
        {"mixed_rows.amp", kNetwork + "OIP3 dBm\nFREQ GHZ\n2.0 10\n30\n", 7, "rows must all hold"},
        {"zero_watts.amp", kNetwork + "OIP3 W\nFREQ GHZ\n2.0 0\n", 6, "must be positive"},
        {"out_of_range.amp", kNetwork + "OIP3 dBm\nFREQ GHZ\n2.0 250\n", 6,
         "outside -100..200 dBm"},
        {"bad_frequency_unit.amp", kNetwork + "OIP3 dBm\nFREQ THZ\n2.0 10\n", 5,
         "unknown frequency unit"},
        {"data_before_header.amp", "2.0 10\n" + kNetwork, 1, "data before a section header"},
    };
    for (const auto &c : cases) {
        CAPTURE(c.name);
        const auto path = scratch.write(c.name, c.text);
        requireDiagnostic(AmpFileParser::parse(path), path, c.line, c.phrase);
    }
}

TEST_CASE("AMP file needs network or power data and an IP3 section", "[amp][parse][diagnostics]") {
    Scratch scratch;

    SECTION("IP3 data alone is not a valid AMP file") {
        const auto path = scratch.write("ip3_only.amp", "OIP3 dBm\nFREQ GHZ\n30\n");
        const auto result = AmpFileParser::parse(path);
        REQUIRE_FALSE(result.file.has_value());
        CHECK(result.error.rfind(path + ": ", 0) == 0); // file-level: no line number
        CHECK(result.error.find("network or power") != std::string::npos);
    }

    SECTION("network data without an IP3 section is rejected") {
        const auto path = scratch.write("no_ip3.amp", kNetwork);
        const auto result = AmpFileParser::parse(path);
        REQUIRE_FALSE(result.file.has_value());
        CHECK(result.error.find("no IIP3 or OIP3 section") != std::string::npos);
    }

    SECTION("POUT data satisfies the network or power rule, and its data is skipped") {
        const std::string text = "POUT dBm\n"
                                 "PIN dBm FREQ=2.1GHz\n"
                                 "0.0 19.0 0.0\n"
                                 "1.0 20.0 0.0\n"
                                 "OIP3 dBm\n"
                                 "FREQ GHz\n"
                                 "30\n";
        const auto result = AmpFileParser::parse(scratch.write("pout.amp", text));
        REQUIRE(result.file.has_value());
        CHECK(result.file->oip3->dBmAt(2.1e9) == Approx(30.0));
    }

    SECTION("a POUT section whose rows are not numeric does not count") {
        const std::string text = "POUT dBm\nPIN dBm FREQ=2.1GHz\nnot a data row\n"
                                 "OIP3 dBm\nFREQ GHz\n30\n";
        const auto path = scratch.write("text_pout.amp", text);
        requireDiagnostic(AmpFileParser::parse(path), path, 1, "no numeric data rows");
    }

    SECTION("noise and noise figure data do not count as network or power data") {
        const std::string text = "NOI\nFREQ GHz\n1.0 10.0 0.5 0.0 0.2\n"
                                 "NF dB\nFREQ GHz\n1.0 10.0\n"
                                 "OIP3 dBm\nFREQ GHz\n30\n";
        const auto result = AmpFileParser::parse(scratch.write("noise_only.amp", text));
        REQUIRE_FALSE(result.file.has_value());
        CHECK(result.error.find("network or power") != std::string::npos);
    }
}

TEST_CASE("AMP S, Y, Z, and POUT sections count only with a numeric data row",
          "[amp][parse][diagnostics]") {
    Scratch scratch;
    struct Marker {
        std::string name;
        std::string header; // the two header lines
        std::string row;    // one numeric data row
    };
    const std::vector<Marker> markers = {
        {"S", "S RI R 50\nFREQ GHZ\n", "1.0 0.5 0.0 2.0 0.0 0.0 0.0 0.3 0.0\n"},
        {"Y", "Y RI\nFREQ GHZ\n", "1.0 0.5 0.0 2.0 0.0 0.0 0.0 0.3 0.0\n"},
        {"Z", "Z RI\nFREQ GHZ\n", "1.0 0.5 0.0 2.0 0.0 0.0 0.0 0.3 0.0\n"},
        {"POUT", "POUT dBm\nPIN dBm FREQ=2.1GHz\n", "0.0 19.0 0.0\n"},
    };
    const std::string ip3 = "OIP3 dBm\nFREQ GHz\n30\n";

    for (const auto &marker : markers) {
        CAPTURE(marker.name);
        // The two header lines with no data row: rejected at the header line.
        const auto empty = scratch.write(marker.name + "_empty.amp", marker.header + ip3);
        requireDiagnostic(AmpFileParser::parse(empty), empty, 1, "no numeric data rows");

        // The same section with one numeric row is a valid file.
        const auto populated =
            scratch.write(marker.name + "_data.amp", marker.header + marker.row + ip3);
        const auto result = AmpFileParser::parse(populated);
        REQUIRE(result.file.has_value());
        REQUIRE(result.file->oip3.has_value());
    }
}

TEST_CASE("AMP file larger than the size cap is rejected before parsing", "[amp][parse][limits]") {
    Scratch scratch;
    const auto path = scratch.write("oversized.amp", kNetwork + "OIP3 dBm\nFREQ GHZ\n30\n");
    std::filesystem::resize_file(path, 256ULL * 1024 * 1024 + 1);

    const auto result = AmpFileParser::parse(path);
    REQUIRE_FALSE(result.file.has_value());
    CHECK(result.error.find("size limit") != std::string::npos);
}
