#include "ampf_file.h"
#include "test_temp_paths.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <clocale>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using Catch::Approx;

namespace {

// Test files live in a per-process directory that is removed when the test ends.
class Scratch {
  public:
    Scratch()
        : m_dir(std::filesystem::temp_directory_path() /
                ("rfsim_ampf_file_" + test_temp_paths::processTag() + "_" +
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

// Header lines for a valid file: the magic line, DEVICE, and COLUMNS.
const std::string kHead = "# amp-f v1\nDEVICE A\n# COLUMNS: S21_dB\n";

// A rejection names the file and the line, and the message contains the phrase.
void requireError(const AmpfParseResult &result, const std::string &path, int line,
                  const std::string &phrase) {
    REQUIRE_FALSE(result.file.has_value());
    CHECK(result.error.find(path + ":" + std::to_string(line) + ": ") != std::string::npos);
    CHECK(result.error.find(phrase) != std::string::npos);
}

// A rejection of the whole file names the file with no line number.
void requireFileError(const AmpfParseResult &result, const std::string &path,
                      const std::string &phrase) {
    REQUIRE_FALSE(result.file.has_value());
    CHECK(result.error.rfind(path + ": ", 0) == 0);
    CHECK(result.error.find(phrase) != std::string::npos);
}

} // namespace

TEST_CASE("amp-f line 1 must be exactly '# amp-f v1'", "[ampf][parse]") {
    Scratch scratch;
    const std::string rest = "DEVICE A\n# COLUMNS: S21_dB\nBEGIN\n1000 1.0\nEND\n";
    for (const std::string first :
         {"# amp-f v2", "#amp-f v1", " # amp-f v1", "# amp-f v1 ", "DEVICE A"}) {
        const auto path = scratch.write("magic.ampf", first + "\n" + rest);
        requireError(AmpfParser::parse(path), path, 1, "# amp-f v1");
    }
    const auto empty = scratch.write("empty.ampf", "");
    requireError(AmpfParser::parse(empty), empty, 1, "# amp-f v1");
}

TEST_CASE("amp-f needs one DEVICE line before the first block", "[ampf][parse]") {
    Scratch scratch;
    SECTION("missing") {
        const auto path = scratch.write("no_device.ampf", "# amp-f v1\n# COLUMNS: S21_dB\n");
        requireFileError(AmpfParser::parse(path), path, "no DEVICE line");
    }
    SECTION("duplicate") {
        const auto path =
            scratch.write("dup_device.ampf", "# amp-f v1\nDEVICE A\nDEVICE B\n# COLUMNS: S21_dB\n");
        requireError(AmpfParser::parse(path), path, 3, "duplicate DEVICE");
    }
    SECTION("a block line before DEVICE") {
        const auto path = scratch.write("late_device.ampf",
                                        "# amp-f v1\n# COLUMNS: S21_dB\nCOND TEMP=25\nDEVICE A\n");
        requireError(AmpfParser::parse(path), path, 3, "must precede");
    }
    SECTION("missing part number") {
        const auto path = scratch.write("no_part.ampf", "# amp-f v1\nDEVICE\n# COLUMNS: S21_dB\n");
        requireError(AmpfParser::parse(path), path, 2, "part number");
    }
}

TEST_CASE("amp-f needs one COLUMNS comment before the first block", "[ampf][parse]") {
    Scratch scratch;
    SECTION("missing") {
        const auto path = scratch.write("no_columns.ampf", "# amp-f v1\nDEVICE A\n");
        requireFileError(AmpfParser::parse(path), path, "# COLUMNS");
    }
    SECTION("duplicate") {
        const auto path = scratch.write(
            "dup_columns.ampf", "# amp-f v1\nDEVICE A\n# COLUMNS: S21_dB\n# COLUMNS: NF_dB\n");
        requireError(AmpfParser::parse(path), path, 4, "duplicate # COLUMNS");
    }
    SECTION("no names") {
        const auto path = scratch.write("empty_columns.ampf", "# amp-f v1\nDEVICE A\n# COLUMNS:\n");
        requireError(AmpfParser::parse(path), path, 3, "lists no columns");
    }
    SECTION("repeated name") {
        const auto path = scratch.write("repeat_columns.ampf",
                                        "# amp-f v1\nDEVICE A\n# COLUMNS: S21_dB S21_dB\n");
        requireError(AmpfParser::parse(path), path, 3, "repeats column");
    }
}

TEST_CASE("amp-f data rows hold N+1 fields, each a decimal number or NaN", "[ampf][parse]") {
    Scratch scratch;
    const std::string head = "# amp-f v1\nDEVICE A\n# COLUMNS: S21_dB NF_dB\nBEGIN\n";
    SECTION("too few fields") {
        const auto path = scratch.write("few.ampf", head + "1000 2.0\nEND\n");
        requireError(AmpfParser::parse(path), path, 5, "expected 3");
    }
    SECTION("too many fields") {
        const auto path = scratch.write("many.ampf", head + "1000 2.0 3.0 4.0\nEND\n");
        requireError(AmpfParser::parse(path), path, 5, "expected 3");
    }
    SECTION("lowercase nan") {
        const auto path = scratch.write("nan_lower.ampf", head + "1000 nan 3.0\nEND\n");
        requireError(AmpfParser::parse(path), path, 5, "is not a decimal number or NaN");
    }
    SECTION("inf") {
        const auto path = scratch.write("inf.ampf", head + "1000 inf 3.0\nEND\n");
        requireError(AmpfParser::parse(path), path, 5, "is not a decimal number or NaN");
    }
    SECTION("overflow to infinity") {
        const auto path = scratch.write("overflow.ampf", head + "1000 1e999 3.0\nEND\n");
        requireError(AmpfParser::parse(path), path, 5, "is not a decimal number or NaN");
    }
    SECTION("NaN frequency") {
        const auto path = scratch.write("nan_freq.ampf", head + "NaN 2.0 3.0\nEND\n");
        requireError(AmpfParser::parse(path), path, 5, "is not a decimal number");
    }
}

TEST_CASE("amp-f blocks need BEGIN, END, and at least one data row", "[ampf][parse]") {
    Scratch scratch;
    SECTION("BEGIN without END") {
        const auto path = scratch.write("no_end.ampf", kHead + "BEGIN\n1000 1.0\n");
        requireError(AmpfParser::parse(path), path, 4, "without END");
    }
    SECTION("END without BEGIN") {
        const auto path = scratch.write("end_only.ampf", kHead + "END\n");
        requireError(AmpfParser::parse(path), path, 4, "END without a matching BEGIN");
    }
    SECTION("nested BEGIN") {
        const auto path = scratch.write("nested.ampf", kHead + "BEGIN\n1000 1.0\nBEGIN\n");
        requireError(AmpfParser::parse(path), path, 6, "inside a data block");
    }
    SECTION("empty block") {
        const auto path = scratch.write("empty_block.ampf", kHead + "BEGIN\nEND\n");
        requireError(AmpfParser::parse(path), path, 4, "no data rows");
    }
    SECTION("data row outside BEGIN and END") {
        const auto path = scratch.write("row_outside.ampf", kHead + "1000 1.0\n");
        requireError(AmpfParser::parse(path), path, 4, "outside BEGIN/END");
    }
    SECTION("preamble without BEGIN") {
        const auto path = scratch.write("no_begin.ampf", kHead + "VAR VG=1\n");
        requireError(AmpfParser::parse(path), path, 4, "without a BEGIN");
    }
}

TEST_CASE("amp-f header-only files are valid and hold no blocks", "[ampf][parse]") {
    Scratch scratch;
    const auto path = scratch.write("header_only.ampf", kHead);
    const auto result = AmpfParser::parse(path);
    REQUIRE(result.file.has_value());
    CHECK(result.file->blocks.empty());
    CHECK(result.file->part_number == "A");
    CHECK(result.file->columns == std::vector<std::string>{"S21_dB"});
}

TEST_CASE("amp-f preambles hold at most two VAR lines and no repeated keys", "[ampf][parse]") {
    Scratch scratch;
    SECTION("third VAR line") {
        const auto path = scratch.write(
            "three_var.ampf", kHead + "VAR A=1\nVAR B=2\nVAR C=3\nBEGIN\n1000 1.0\nEND\n");
        requireError(AmpfParser::parse(path), path, 6, "at most two VAR");
    }
    SECTION("VAR key repeated in one block") {
        const auto path =
            scratch.write("repeat_var.ampf", kHead + "VAR A=1\nVAR A=2\nBEGIN\n1000 1.0\nEND\n");
        requireError(AmpfParser::parse(path), path, 5, "repeats");
    }
    SECTION("COND key repeated in one block") {
        const auto path =
            scratch.write("repeat_cond.ampf", kHead + "COND T=1\nCOND T=2\nBEGIN\n1000 1.0\nEND\n");
        requireError(AmpfParser::parse(path), path, 5, "repeats");
    }
    SECTION("COND with two pairs") {
        const auto path =
            scratch.write("two_pairs.ampf", kHead + "COND T=1 U=2\nBEGIN\n1000 1.0\nEND\n");
        requireError(AmpfParser::parse(path), path, 4, "one KEY=VALUE pair");
    }
    SECTION("VAR without a value") {
        const auto path = scratch.write("no_eq.ampf", kHead + "VAR VG\nBEGIN\n1000 1.0\nEND\n");
        requireError(AmpfParser::parse(path), path, 4, "KEY=VALUE pair");
    }
    SECTION("invalid key name") {
        const auto path = scratch.write("bad_key.ampf", kHead + "VAR 1G=2\nBEGIN\n1000 1.0\nEND\n");
        requireError(AmpfParser::parse(path), path, 4, "KEY=VALUE pair");
    }
    SECTION("non-numeric value") {
        const auto path =
            scratch.write("nan_var.ampf", kHead + "VAR VG=NaN\nBEGIN\n1000 1.0\nEND\n");
        requireError(AmpfParser::parse(path), path, 4, "is not a decimal number");
    }
    SECTION("FREQ_UNITS on a VAR line") {
        const auto path =
            scratch.write("units_var.ampf", kHead + "VAR FREQ_UNITS=GHz\nBEGIN\n1000 1.0\nEND\n");
        requireError(AmpfParser::parse(path), path, 4, "only valid on the DEVICE line");
    }
    SECTION("FREQ_UNITS on a COND line") {
        const auto path =
            scratch.write("units_cond.ampf", kHead + "COND FREQ_UNITS=GHz\nBEGIN\n1000 1.0\nEND\n");
        requireError(AmpfParser::parse(path), path, 4, "only valid on the DEVICE line");
    }
}

TEST_CASE("amp-f DEVICE keys and VAR keys cannot overlap, and VAR and COND keys cannot overlap",
          "[ampf][parse]") {
    Scratch scratch;
    const std::string head = "# amp-f v1\nDEVICE A VCC=5\n# COLUMNS: S21_dB\n";
    SECTION("DEVICE key set as a VAR (rule 6)") {
        const auto path = scratch.write("rule6.ampf", head + "VAR VCC=3\nBEGIN\n1000 1.0\nEND\n");
        requireError(AmpfParser::parse(path), path, 4, "rule 6");
    }
    SECTION("VAR in one block, COND in another (rule 7)") {
        const auto path = scratch.write(
            "rule7_cond_after_var.ampf",
            head + "VAR VG=1\nBEGIN\n1000 1.0\nEND\nCOND VG=2\nBEGIN\n1000 2.0\nEND\n");
        requireError(AmpfParser::parse(path), path, 8, "rule 7");
    }
    SECTION("COND in one block, VAR in another (rule 7)") {
        const auto path = scratch.write(
            "rule7_var_after_cond.ampf",
            head + "COND TEMP=1\nBEGIN\n1000 1.0\nEND\nVAR TEMP=2\nBEGIN\n1000 2.0\nEND\n");
        requireError(AmpfParser::parse(path), path, 8, "rule 7");
    }
}

TEST_CASE("amp-f blocks inherit DEVICE defaults unless a COND line overrides them",
          "[ampf][parse]") {
    Scratch scratch;
    const auto path = scratch.write(
        "inherit.ampf", "# amp-f v1\nDEVICE A VCC=5.0 TEMP=25 FREQ_UNITS=GHz\n# COLUMNS: S21_dB\n"
                        "BEGIN\n1 1.0\nEND\n"
                        "COND TEMP=85\nBEGIN\n1 2.0\nEND\n"
                        "VAR VG=1.5\nBEGIN\n1 3.0\nEND\n");
    const auto result = AmpfParser::parse(path);
    REQUIRE(result.file.has_value());
    REQUIRE(result.file->blocks.size() == 3);

    const auto &first = result.file->blocks[0].identity;
    CHECK(first.size() == 2);
    CHECK(first.at("VCC") == Approx(5.0));
    CHECK(first.at("TEMP") == Approx(25.0));

    const auto &second = result.file->blocks[1].identity;
    CHECK(second.size() == 2);
    CHECK(second.at("TEMP") == Approx(85.0));

    const auto &third = result.file->blocks[2].identity;
    CHECK(third.size() == 3);
    CHECK(third.at("TEMP") == Approx(25.0));
    CHECK(third.at("VG") == Approx(1.5));

    // FREQ_UNITS=GHz applies to the whole file: "1" is 1 GHz.
    CHECK(result.file->blocks[0].freq_Hz[0] == Approx(1.0e9));
}

TEST_CASE("amp-f keys absent from DEVICE and COND stay unknown", "[ampf][parse]") {
    Scratch scratch;
    const auto path = scratch.write("unknown.ampf", kHead + "BEGIN\n1000 1.0\nEND\n");
    const auto result = AmpfParser::parse(path);
    REQUIRE(result.file.has_value());
    REQUIRE(result.file->blocks.size() == 1);
    CHECK(result.file->blocks[0].identity.empty());
    CHECK(result.file->device_defaults.empty());
}

TEST_CASE("amp-f blocks with the same identity are rejected at the second block", "[ampf][parse]") {
    Scratch scratch;
    const std::string head = "# amp-f v1\nDEVICE A TEMP=25\n# COLUMNS: S21_dB\n";
    SECTION("two blocks inherit the same default") {
        const auto path = scratch.write("dup_default.ampf",
                                        head + "BEGIN\n1000 1.0\nEND\nBEGIN\n2000 2.0\nEND\n");
        requireError(AmpfParser::parse(path), path, 7, "duplicates");
    }
    SECTION("the same VAR and COND values") {
        const auto path =
            scratch.write("dup_values.ampf", head + "VAR VG=1\nCOND T=2\nBEGIN\n1000 1.0\nEND\n"
                                                    "VAR VG=1\nCOND T=2\nBEGIN\n1000 2.0\nEND\n");
        requireError(AmpfParser::parse(path), path, 11, "duplicates");
    }
    SECTION("a different VAR value is not a duplicate") {
        const auto path = scratch.write(
            "distinct.ampf",
            head + "VAR VG=1\nBEGIN\n1000 1.0\nEND\nVAR VG=2\nBEGIN\n1000 2.0\nEND\n");
        const auto result = AmpfParser::parse(path);
        REQUIRE(result.file.has_value());
        CHECK(result.file->blocks.size() == 2);
    }
}

TEST_CASE("amp-f comments and blank lines are rejected between BEGIN and END", "[ampf][parse]") {
    Scratch scratch;
    SECTION("comment inside a block") {
        const auto path =
            scratch.write("comment_in.ampf", kHead + "BEGIN\n1000 1.0\n# note\nEND\n");
        requireError(AmpfParser::parse(path), path, 6, "comment");
    }
    SECTION("blank line inside a block") {
        const auto path =
            scratch.write("blank_in.ampf", kHead + "BEGIN\n1000 1.0\n\n2000 2.0\nEND\n");
        requireError(AmpfParser::parse(path), path, 6, "blank line");
    }
    SECTION("trailing comment on a data row") {
        const auto path =
            scratch.write("trailing_in.ampf", kHead + "BEGIN\n1000 1.0 # note\nEND\n");
        requireError(AmpfParser::parse(path), path, 5, "comment");
    }
}

TEST_CASE("amp-f comments and blank lines are allowed outside a data block", "[ampf][parse]") {
    Scratch scratch;
    const auto path =
        scratch.write("comments_ok.ampf",
                      "# amp-f v1\n# any comment\n\nDEVICE A  # trailing\n# COLUMNS: S21_dB\n\n"
                      "# before the block\nVAR VG=1 # trailing\n\nBEGIN\n1000 1.0\nEND\n");
    const auto result = AmpfParser::parse(path);
    REQUIRE(result.file.has_value());
    REQUIRE(result.file->blocks.size() == 1);
    CHECK(result.file->blocks[0].identity.at("VG") == Approx(1.0));
}

TEST_CASE("amp-f FREQ_UNITS scales frequencies and defaults to MHz", "[ampf][parse]") {
    struct Case {
        std::string device_suffix;
        std::string freq_token;
        double expected_Hz;
    };
    const std::vector<Case> cases = {
        {"", "1000", 1.0e9},
        {" FREQ_UNITS=Hz", "1000000000", 1.0e9},
        {" FREQ_UNITS=kHz", "1000000", 1.0e9},
        {" FREQ_UNITS=MHz", "1000", 1.0e9},
        {" FREQ_UNITS=GHz", "1", 1.0e9},
    };
    Scratch scratch;
    for (const auto &c : cases) {
        const auto path = scratch.write("units.ampf", "# amp-f v1\nDEVICE A" + c.device_suffix +
                                                          "\n# COLUMNS: S21_dB\nBEGIN\n" +
                                                          c.freq_token + " 1.0\nEND\n");
        const auto result = AmpfParser::parse(path);
        REQUIRE(result.file.has_value());
        REQUIRE(result.file->blocks.size() == 1);
        CHECK(result.file->blocks[0].freq_Hz[0] == Approx(c.expected_Hz));
    }
}

TEST_CASE("amp-f FREQ_UNITS values are exact and appear once on DEVICE", "[ampf][parse]") {
    Scratch scratch;
    SECTION("unknown unit") {
        const auto path =
            scratch.write("thz.ampf", "# amp-f v1\nDEVICE A FREQ_UNITS=THz\n# COLUMNS: S21_dB\n");
        requireError(AmpfParser::parse(path), path, 2, "FREQ_UNITS");
    }
    SECTION("unit case is significant") {
        const auto path = scratch.write("mhz_lower.ampf",
                                        "# amp-f v1\nDEVICE A FREQ_UNITS=mhz\n# COLUMNS: S21_dB\n");
        requireError(AmpfParser::parse(path), path, 2, "FREQ_UNITS");
    }
    SECTION("repeated on DEVICE") {
        const auto path = scratch.write(
            "repeat_units.ampf",
            "# amp-f v1\nDEVICE A FREQ_UNITS=GHz FREQ_UNITS=MHz\n# COLUMNS: S21_dB\n");
        requireError(AmpfParser::parse(path), path, 2, "repeats");
    }
}

TEST_CASE("amp-f accepts CRLF line endings", "[ampf][parse]") {
    Scratch scratch;
    const auto path = scratch.write(
        "crlf.ampf", "# amp-f v1\r\nDEVICE A\r\n# COLUMNS: S21_dB\r\nBEGIN\r\n1000 2.0\r\nEND\r\n");
    const auto result = AmpfParser::parse(path);
    REQUIRE(result.file.has_value());
    REQUIRE(result.file->blocks.size() == 1);
    CHECK(result.file->blocks[0].freq_Hz[0] == Approx(1.0e9));
    CHECK(result.file->blocks[0].columns[0][0] == Approx(2.0));
}

TEST_CASE("amp-f keeps every declared column in order", "[ampf][parse]") {
    Scratch scratch;
    const auto path = scratch.write(
        "columns.ampf", "# amp-f v1\nDEVICE A\n# COLUMNS: S21_dB S21_deg OIP3_dBm OIP2_dBm\n"
                        "BEGIN\n1000 1 2 3 4\nEND\n");
    const auto result = AmpfParser::parse(path);
    REQUIRE(result.file.has_value());
    const AmpfFile &file = *result.file;
    CHECK(file.columns.size() == 4);
    CHECK(file.columnIndex("OIP3_dBm") == 2);
    CHECK(file.columnIndex("OIP2_dBm") == 3);
    CHECK(file.columnIndex("IIP3_dBm") == -1);
    CHECK(file.blocks[0].columns[2][0] == Approx(3.0));
    CHECK(file.blocks[0].columns[3][0] == Approx(4.0));
}

TEST_CASE("amp-f rows may be unsorted, and valueAt interpolates and holds at the ends",
          "[ampf][parse]") {
    Scratch scratch;
    const auto path =
        scratch.write("interp.ampf", kHead + "BEGIN\n2000 20.0\n1000 10.0\n4000 40.0\nEND\n");
    const auto result = AmpfParser::parse(path);
    REQUIRE(result.file.has_value());
    const AmpfBlock &block = result.file->blocks[0];
    CHECK(block.valueAt(0, 1.0e9) == Approx(10.0));
    CHECK(block.valueAt(0, 1.5e9) == Approx(15.0));
    CHECK(block.valueAt(0, 2.0e9) == Approx(20.0));
    CHECK(block.valueAt(0, 3.0e9) == Approx(30.0));
    CHECK(block.valueAt(0, 0.5e9) == Approx(10.0));
    CHECK(block.valueAt(0, 5.0e9) == Approx(40.0));
}

TEST_CASE("amp-f a missing cell makes interpolation across it NaN", "[ampf][parse]") {
    Scratch scratch;
    const auto path =
        scratch.write("gap.ampf", kHead + "BEGIN\n1000 10.0\n2000 NaN\n3000 30.0\nEND\n");
    const auto result = AmpfParser::parse(path);
    REQUIRE(result.file.has_value());
    const AmpfBlock &block = result.file->blocks[0];
    CHECK(block.valueAt(0, 1.0e9) == Approx(10.0));
    CHECK(block.valueAt(0, 3.0e9) == Approx(30.0));
    CHECK(std::isnan(block.valueAt(0, 1.5e9)));
    CHECK(std::isnan(block.valueAt(0, 2.0e9)));
    CHECK(std::isnan(block.valueAt(0, 2.5e9)));
}

TEST_CASE("amp-f files over the size limit are rejected before parsing", "[ampf][parse]") {
    Scratch scratch;
    const auto path = scratch.write("large.ampf", "");
    std::filesystem::resize_file(path, AmpfParser::kMaxFileBytes + 1);
    requireFileError(AmpfParser::parse(path), path, "size limit");
}

TEST_CASE("selectAmpfBlock matches exact identities and reports none, one, or many",
          "[ampf][select]") {
    Scratch scratch;
    const auto path =
        scratch.write("select.ampf", kHead + "VAR VG=2.5\nCOND TEMP=-40\nBEGIN\n1 1.0\nEND\n"
                                             "VAR VG=5.0\nCOND TEMP=-40\nBEGIN\n1 2.0\nEND\n"
                                             "COND TEMP=85\nBEGIN\n1 3.0\nEND\n");
    const auto result = AmpfParser::parse(path);
    REQUIRE(result.file.has_value());
    const AmpfFile &file = *result.file;
    REQUIRE(file.blocks.size() == 3);

    const auto exact = selectAmpfBlock(file, {{"VG", 2.5}, {"TEMP", -40.0}});
    CHECK(exact.match == AmpfMatch::One);
    CHECK(exact.block == &file.blocks[0]);

    const auto single = selectAmpfBlock(file, {{"TEMP", 85.0}});
    CHECK(single.match == AmpfMatch::One);
    CHECK(single.block == &file.blocks[2]);

    const auto partial = selectAmpfBlock(file, {{"TEMP", -40.0}});
    CHECK(partial.match == AmpfMatch::Many);
    CHECK(partial.block == nullptr);

    CHECK(selectAmpfBlock(file, {}).match == AmpfMatch::Many);
    CHECK(selectAmpfBlock(file, {{"TEMP", 25.0}}).match == AmpfMatch::None);
    CHECK(selectAmpfBlock(file, {{"VG", 2.5000001}, {"TEMP", -40.0}}).match == AmpfMatch::None);
}

TEST_CASE("amp-f worked example from the spec parses to three blocks", "[ampf][parse]") {
    Scratch scratch;
    const auto path = scratch.write("worked.ampf",
                                    "# amp-f v1\n"
                                    "# source: HMC1131 datasheet fig.4 - gain and NF vs frequency\n"
                                    "DEVICE HMC1131 VCC=5.0\n"
                                    "# COLUMNS: S21_dB S21_deg NF_dB\n"
                                    "\n"
                                    "VAR VG=2.5\n"
                                    "COND TEMP=-40\n"
                                    "BEGIN\n"
                                    "2500   18.2   45.1   2.1\n"
                                    "2600   17.9   44.8   2.2\n"
                                    "2700   17.6   44.5   2.3\n"
                                    "END\n"
                                    "\n"
                                    "VAR VG=5.0\n"
                                    "COND TEMP=-40\n"
                                    "BEGIN\n"
                                    "2500   19.0   46.2   NaN\n"
                                    "2600   18.8   45.9   NaN\n"
                                    "2700   18.5   45.6   NaN\n"
                                    "END\n"
                                    "\n"
                                    "COND TEMP=85\n"
                                    "BEGIN\n"
                                    "2500   18.0   44.9   2.4\n"
                                    "2600   17.7   44.6   2.5\n"
                                    "2700   NaN    NaN    2.6\n"
                                    "END\n");
    const auto result = AmpfParser::parse(path);
    REQUIRE(result.file.has_value());
    const AmpfFile &file = *result.file;
    REQUIRE(file.blocks.size() == 3);

    // DEVICE defaults reach every block. The TEMP=85 block has no VAR, so VG is unknown there.
    for (const auto &block : file.blocks)
        REQUIRE(block.identity.count("VCC") == 1); // inherited from DEVICE
    REQUIRE(file.blocks[0].identity.count("VG") == 1);
    REQUIRE(file.blocks[0].identity.count("TEMP") == 1);
    REQUIRE(file.blocks[2].identity.count("TEMP") == 1);
    CHECK(file.blocks[0].identity.at("VG") == Approx(2.5));
    CHECK(file.blocks[0].identity.at("TEMP") == Approx(-40.0));
    CHECK(file.blocks[0].identity.at("VCC") == Approx(5.0));
    CHECK(file.blocks[2].identity.count("VG") == 0);
    CHECK(file.blocks[2].identity.at("TEMP") == Approx(85.0));
    CHECK(file.blocks[2].identity.at("VCC") == Approx(5.0));

    // A whole column can be NaN for one block (NF_dB in VG=5.0), and one cell can be NaN.
    const int nf = file.columnIndex("NF_dB");
    const int s21 = file.columnIndex("S21_dB");
    REQUIRE(nf >= 0);
    REQUIRE(s21 >= 0);
    REQUIRE(file.blocks[1].columns.size() == 3);
    CHECK(std::isnan(file.blocks[1].columns[static_cast<size_t>(nf)][0]));
    CHECK(std::isnan(file.blocks[2].columns[static_cast<size_t>(s21)][2]));
}

TEST_CASE("amp-f numbers keep a dot decimal under a comma-decimal numeric locale",
          "[ampf][parse]") {
    // Restores the numeric locale on every exit path, including a failed REQUIRE.
    struct LocaleRestore {
        std::string saved;
        ~LocaleRestore() { std::setlocale(LC_NUMERIC, saved.c_str()); }
    } restore{std::setlocale(LC_NUMERIC, nullptr)};

    bool comma = false;
    for (const char *name : {"German_Germany.1252", "de_DE.UTF-8", "de_DE.utf8", "de_DE"}) {
        if (std::setlocale(LC_NUMERIC, name) && std::localeconv()->decimal_point[0] == ',') {
            comma = true;
            break;
        }
    }
    if (!comma) {
        WARN("no comma-decimal numeric locale is installed; the locale case was not exercised");
        return;
    }

    Scratch scratch;
    const auto path =
        scratch.write("locale.ampf", "# amp-f v1\nDEVICE A VG=2.5\n# COLUMNS: S21_dB\n"
                                     "BEGIN\n1000 1.25\nEND\n");
    const auto result = AmpfParser::parse(path);
    REQUIRE(result.file.has_value());
    REQUIRE(result.file->blocks.size() == 1);
    REQUIRE(result.file->blocks[0].identity.count("VG") == 1);
    CHECK(result.file->blocks[0].identity.at("VG") == Approx(2.5));
    CHECK(result.file->blocks[0].columns[0][0] == Approx(1.25));
}
