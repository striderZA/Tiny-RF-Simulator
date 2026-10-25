#include "s_parameter_data.h"
#include "test_temp_paths.h"
#include "touchstone_parser.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <complex>
#include <filesystem>
#include <fstream>
#include <numbers>

using Catch::Approx;

TEST_CASE("TouchstoneParser parses real .s2p file", "[touchstone]") {
    std::string path =
        std::string(PROJECT_SOURCE_DIR) +
        "/component_data/amplifiers/adm-3844psm/ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    auto result = TouchstoneParser::parse(path);
    REQUIRE(result.has_value());

    const auto &data = *result;
    REQUIRE(data.num_ports == 2);
    REQUIRE(data.reference_impedance == Approx(50.0));
    REQUIRE(data.format == TouchstoneData::Format::DB);
    REQUIRE(data.parameter == TouchstoneData::Parameter::S);
    REQUIRE(data.freq_unit == TouchstoneData::FrequencyUnit::Hz);

    // File has 2650 frequency points (10 MHz to 26.5 GHz, 10 MHz step)
    REQUIRE(data.frequencies.size() == 2650);
    REQUIRE(data.parameters.size() == 2650);

    // First frequency: 10 MHz
    REQUIRE(data.frequencies[0] == Approx(10e6).epsilon(1));
    // Last frequency: 26.5 GHz
    REQUIRE(data.frequencies.back() == Approx(26.5e9).margin(1e6));

    // Each frequency point has 4 S-parameters (S11, S21, S12, S22)
    REQUIRE(data.parameters[0].size() == 4);

    // Verify S11 at first point from raw data:
    // Line 9: 10000000 -20.405844 -4.3232856 ...
    // S11_dB = -20.405844, S11_deg = -4.3232856
    // mag = 10^(-20.405844/20) = 0.09547...
    // S11 = mag * (cos(-4.323deg) + i*sin(-4.323deg))
    double expected_mag = std::pow(10.0, -20.405844 / 20.0);
    double expected_rad = -4.3232856 * std::numbers::pi / 180.0;
    std::complex<double> expected_s11(expected_mag * std::cos(expected_rad),
                                      expected_mag * std::sin(expected_rad));

    REQUIRE(data.parameters[0][0].real() == Approx(expected_s11.real()).epsilon(1e-6));
    REQUIRE(data.parameters[0][0].imag() == Approx(expected_s11.imag()).epsilon(1e-6));
}

TEST_CASE("TouchstoneParser parses synthetic v1.0 1-port file", "[touchstone]") {
    // Create a temporary .s1p file
    std::string tmpfile = std::filesystem::temp_directory_path().string() + "/test_1port.s1p";
    {
        std::ofstream ofs(tmpfile);
        ofs << "! 1-port test file\n";
        ofs << "# MHz S MA R 50\n";
        ofs << "100 0.5 30.0\n";
        ofs << "200 0.4 45.0\n";
    }

    auto result = TouchstoneParser::parse(tmpfile);
    REQUIRE(result.has_value());

    const auto &data = *result;
    REQUIRE(data.num_ports == 1);
    REQUIRE(data.reference_impedance == Approx(50.0));
    REQUIRE(data.format == TouchstoneData::Format::MA);
    REQUIRE(data.parameter == TouchstoneData::Parameter::S);
    REQUIRE(data.freq_unit == TouchstoneData::FrequencyUnit::MHz);

    REQUIRE(data.frequencies.size() == 2);
    REQUIRE(data.frequencies[0] == Approx(100e6));
    REQUIRE(data.frequencies[1] == Approx(200e6));

    REQUIRE(data.parameters[0].size() == 1);
    // S11 at 100 MHz: mag=0.5, angle=30deg
    double rad = 30.0 * std::numbers::pi / 180.0;
    REQUIRE(data.parameters[0][0].real() == Approx(0.5 * std::cos(rad)).epsilon(1e-6));
    REQUIRE(data.parameters[0][0].imag() == Approx(0.5 * std::sin(rad)).epsilon(1e-6));

    std::filesystem::remove(tmpfile);
}

TEST_CASE("TouchstoneParser parses synthetic v1.0 2-port DB file", "[touchstone]") {
    std::string tmpfile = std::filesystem::temp_directory_path().string() + "/test_2port_db.s2p";
    {
        std::ofstream ofs(tmpfile);
        ofs << "! 2-port test file\n";
        ofs << "# GHz S DB R 75\n";
        // freq=1GHz, S11=-20dB/0deg, S21=0dB/90deg, S12=-40dB/180deg, S22=-10dB/-45deg
        ofs << "1.0 -20.0 0.0 0.0 90.0 -40.0 180.0 -10.0 -45.0\n";
    }

    auto result = TouchstoneParser::parse(tmpfile);
    REQUIRE(result.has_value());

    const auto &data = *result;
    REQUIRE(data.num_ports == 2);
    REQUIRE(data.reference_impedance == Approx(75.0));
    REQUIRE(data.format == TouchstoneData::Format::DB);
    REQUIRE(data.freq_unit == TouchstoneData::FrequencyUnit::GHz);

    REQUIRE(data.frequencies.size() == 1);
    REQUIRE(data.frequencies[0] == Approx(1e9));

    REQUIRE(data.parameters[0].size() == 4);
    // S11: -20 dB = 0.1 mag, 0 deg
    REQUIRE(std::abs(data.parameters[0][0]) == Approx(0.1).epsilon(1e-6));
    REQUIRE(data.parameters[0][0].real() == Approx(0.1).epsilon(1e-6));
    REQUIRE(data.parameters[0][0].imag() == Approx(0.0).margin(1e-6));

    // S12: -40 dB = 0.01 mag, 180 deg
    REQUIRE(std::abs(data.parameters[0][1]) == Approx(0.01).epsilon(1e-6));
    REQUIRE(data.parameters[0][1].real() == Approx(-0.01).epsilon(1e-6));
    REQUIRE(data.parameters[0][1].imag() == Approx(0.0).margin(1e-6));

    // S21: 0 dB = 1.0 mag, 90 deg
    REQUIRE(std::abs(data.parameters[0][2]) == Approx(1.0).epsilon(1e-6));
    REQUIRE(data.parameters[0][2].real() == Approx(0.0).margin(1e-6));
    REQUIRE(data.parameters[0][2].imag() == Approx(1.0).epsilon(1e-6));

    // S22: -10 dB = 0.3162... mag, -45 deg
    REQUIRE(std::abs(data.parameters[0][3]) == Approx(0.316227766).epsilon(1e-6));
    REQUIRE(data.parameters[0][3].real() ==
            Approx(0.316227766 * std::cos(-45.0 * std::numbers::pi / 180.0)).epsilon(1e-6));
    REQUIRE(data.parameters[0][3].imag() ==
            Approx(0.316227766 * std::sin(-45.0 * std::numbers::pi / 180.0)).epsilon(1e-6));

    std::filesystem::remove(tmpfile);
}

TEST_CASE("TouchstoneParser returns nullopt for missing file", "[touchstone]") {
    auto result = TouchstoneParser::parse("/nonexistent/path/file.s2p");
    REQUIRE(!result.has_value());
}

TEST_CASE("TouchstoneParser returns nullopt for file without option line", "[touchstone]") {
    std::string tmpfile = std::filesystem::temp_directory_path().string() + "/test_no_option.s2p";
    {
        std::ofstream ofs(tmpfile);
        ofs << "! No option line here\n";
        ofs << "1.0 0.1 0.0 0.0 0.0 0.0 0.0 0.1 0.0\n";
    }
    auto result = TouchstoneParser::parse(tmpfile);
    REQUIRE(!result.has_value());
    std::filesystem::remove(tmpfile);
}

static std::string writeTouchstoneFile(const std::string &name, const std::string &body) {
    std::string path = std::filesystem::temp_directory_path().string() + "/" + name;
    std::ofstream ofs(path);
    ofs << body;
    return path;
}

TEST_CASE("SParameterData renormalises a 75 ohm 1-port file to the 50 ohm system",
          "[touchstone][reference]") {
    // Issue #284: S11 = 0.5 referenced to 75 ohm is Z = 225 ohm, which is
    // S11 = 175/275 = 7/11 = 0.6364 in a 50 ohm system.
    std::string path = writeTouchstoneFile("test_r75_half.s1p", "# GHz S MA R 75\n"
                                                                "1.0 0.5 0.0\n");
    SParameterData sparam;
    REQUIRE(sparam.load(path));
    REQUIRE(sparam.params()[0][0].real() == Approx(7.0 / 11.0).epsilon(1e-9));
    REQUIRE(sparam.params()[0][0].imag() == Approx(0.0).margin(1e-12));
    std::filesystem::remove(path);
}

TEST_CASE("SParameterData renormalises a matched 75 ohm load to -13.98 dB at 50 ohm",
          "[touchstone][reference]") {
    // Issue #284: a perfect 75 ohm match is S11 = 0 referenced to 75 ohm, which is
    // S11 = (75 - 50) / (75 + 50) = 0.2 at 50 ohm, not a perfect match.
    std::string path = writeTouchstoneFile("test_r75_matched.s1p", "# GHz S MA R 75\n"
                                                                   "1.0 0.0 0.0\n");
    SParameterData sparam;
    REQUIRE(sparam.load(path));
    REQUIRE(std::abs(sparam.params()[0][0]) == Approx(0.2).epsilon(1e-9));
    std::filesystem::remove(path);
}

TEST_CASE("SParameterData leaves a thru unchanged when both ports are 75 ohm",
          "[touchstone][reference]") {
    // Same reference on both ports: S21 = S12 = 1 is unchanged by the transform.
    std::string path =
        writeTouchstoneFile("test_r75_thru.s2p", "# GHz S RI R 75\n"
                                                 "1.0 0.0 0.0 1.0 0.0 1.0 0.0 0.0 0.0\n");
    SParameterData sparam;
    REQUIRE(sparam.load(path));
    REQUIRE(std::abs(sparam.params()[0][0]) == Approx(0.0).margin(1e-12));
    REQUIRE(std::abs(sparam.params()[0][1] - std::complex<double>(1.0, 0.0)) ==
            Approx(0.0).margin(1e-12));
    REQUIRE(std::abs(sparam.params()[0][2] - std::complex<double>(1.0, 0.0)) ==
            Approx(0.0).margin(1e-12));
    REQUIRE(std::abs(sparam.params()[0][3]) == Approx(0.0).margin(1e-12));
    std::filesystem::remove(path);
}

TEST_CASE("TouchstoneParser keeps file-referenced values and reports the file reference",
          "[touchstone][reference]") {
    std::string path = writeTouchstoneFile("test_r75_parse.s1p", "# GHz S MA R 75\n"
                                                                 "1.0 0.5 0.0\n");
    auto result = TouchstoneParser::parse(path);
    REQUIRE(result.has_value());
    REQUIRE(result->reference_impedance == Approx(75.0));
    REQUIRE(result->parameters[0][0].real() == Approx(0.5));
    std::filesystem::remove(path);
}

TEST_CASE("TouchstoneParser reads v2.0 [Reference] as the reference impedance",
          "[touchstone][reference]") {
    std::string path =
        writeTouchstoneFile("test_v2_reference.s2p", "[Version] 2.0\n"
                                                     "# GHz S RI R 50\n"
                                                     "[Number of Ports] 2\n"
                                                     "[Two-Port Data Order] 21_12\n"
                                                     "[Reference] 75 75\n"
                                                     "[Network Data]\n"
                                                     "1.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0\n"
                                                     "[End]\n");
    auto result = TouchstoneParser::parse(path);
    REQUIRE(result.has_value());
    REQUIRE(result->reference_impedance == Approx(75.0));
    std::filesystem::remove(path);
}

TEST_CASE("TouchstoneParser reads a v2.0 [Reference] that spans lines", "[touchstone][reference]") {
    std::string path =
        writeTouchstoneFile("test_v2_reference_multiline.s1p", "[Version] 2.0\n"
                                                               "# GHz S RI R 50\n"
                                                               "[Number of Ports] 1\n"
                                                               "[Reference]\n"
                                                               "75\n"
                                                               "75\n"
                                                               "[Network Data]\n"
                                                               "1.0 0.0 0.0\n"
                                                               "[End]\n");
    auto result = TouchstoneParser::parse(path);
    REQUIRE(result.has_value());
    REQUIRE(result->reference_impedance == Approx(75.0));
    REQUIRE(result->parameters.size() == 1);
    std::filesystem::remove(path);
}

TEST_CASE("TouchstoneParser lets v2.0 [Reference] override the option line R",
          "[touchstone][reference]") {
    std::string path = writeTouchstoneFile("test_v2_reference_override.s1p", "[Version] 2.0\n"
                                                                             "# GHz S RI R 50\n"
                                                                             "[Number of Ports] 1\n"
                                                                             "[Reference] 75\n"
                                                                             "[Network Data]\n"
                                                                             "1.0 0.0 0.0\n"
                                                                             "[End]\n");
    auto result = TouchstoneParser::parse(path);
    REQUIRE(result.has_value());
    REQUIRE(result->reference_impedance == Approx(75.0));
    std::filesystem::remove(path);
}

TEST_CASE("TouchstoneParser rejects v2.0 [Reference] with differing per-port values",
          "[touchstone][reference]") {
    // Per-port references are not renormalised, so they are refused rather than
    // silently treated as a single reference.
    std::string path =
        writeTouchstoneFile("test_v2_reference_mixed.s2p", "[Version] 2.0\n"
                                                           "# GHz S RI R 50\n"
                                                           "[Number of Ports] 2\n"
                                                           "[Two-Port Data Order] 21_12\n"
                                                           "[Reference] 50 75\n"
                                                           "[Network Data]\n"
                                                           "1.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0\n"
                                                           "[End]\n");
    REQUIRE_FALSE(TouchstoneParser::parse(path).has_value());
    std::filesystem::remove(path);
}

TEST_CASE("TouchstoneParser honors v2.0 two-port data order", "[touchstone]") {
    const auto parse_v2 = [](const std::string &filename, const std::string &order) {
        const auto path = std::filesystem::temp_directory_path() / filename;
        {
            std::ofstream file(path);
            file << "[Version] 2.0\n";
            file << "[Two-Port Data Order] " << order << "\n";
            file << "# Hz S RI R 50\n";
            file << "[Network Data]\n";
            if (order == "12_21")
                file << "1e9 1 0 0.1 0 2 0 1 0\n";
            else
                file << "1e9 1 0 2 0 0.1 0 1 0\n";
            file << "[End]\n";
        }
        return std::pair{path, TouchstoneParser::parse(path.string())};
    };

    auto [path_12_21, result_12_21] = parse_v2("test_touchstone_12_21.s2p", "12_21");
    REQUIRE(result_12_21.has_value());
    REQUIRE(result_12_21->parameters[0][2].real() == Approx(2.0));
    REQUIRE(result_12_21->parameters[0][2].imag() == Approx(0.0));
    std::filesystem::remove(path_12_21);

    auto [path_21_12, result_21_12] = parse_v2("test_touchstone_21_12.s2p", "21_12");
    REQUIRE(result_21_12.has_value());
    REQUIRE(result_21_12->parameters[0][2].real() == Approx(2.0));
    std::filesystem::remove(path_21_12);

    const auto path_missing =
        std::filesystem::temp_directory_path() / "test_touchstone_missing_order.s2p";
    {
        std::ofstream file(path_missing);
        file << "[Version] 2.0\n";
        file << "# Hz S RI R 50\n";
        file << "[Network Data]\n";
        file << "1e9 1 0 0.1 0 2 0 1 0\n";
        file << "[End]\n";
    }
    REQUIRE_FALSE(TouchstoneParser::parse(path_missing.string()).has_value());
    std::filesystem::remove(path_missing);
}

TEST_CASE("TouchstoneParser stores 3-port and 4-port matrices in row-major order", "[touchstone]") {
    for (const int ports : {3, 4}) {
        const auto tmpfile =
            std::filesystem::temp_directory_path() /
            ("test_row_major_" + std::to_string(ports) + "port.s" + std::to_string(ports) + "p");
        {
            std::ofstream ofs(tmpfile);
            ofs << "# GHz S RI R 50\n1.0";
            for (int pair = 0; pair < ports * ports; ++pair)
                ofs << ' ' << pair + 1 << " 0";
            ofs << '\n';
        }

        const auto result = TouchstoneParser::parse(tmpfile.string());
        REQUIRE(result.has_value());
        REQUIRE(result->parameters[0].size() == ports * ports);
        for (int row = 0; row < ports; ++row) {
            for (int col = 0; col < ports; ++col) {
                const int pair = row * ports + col;
                REQUIRE(result->parameters[0][pair].real() == Approx(pair + 1));
                REQUIRE(result->parameters[0][pair].imag() == Approx(0.0));
            }
        }
        std::filesystem::remove(tmpfile);
    }
}

TEST_CASE("TouchstoneParser matches the .S1P extension case-insensitively", "[touchstone]") {
    // Issue #287: an upper-case extension was ignored, so this 1-port file loaded as 2 ports.
    std::string tmpfile = std::filesystem::temp_directory_path().string() + "/ONEPORT.S1P";
    {
        std::ofstream ofs(tmpfile);
        ofs << "# GHz S MA R 50\n";
        ofs << "1.0 0.5 0.0\n";
        ofs << "2.0 0.5 0.0\n";
        ofs << "3.0 0.5 0.0\n";
    }

    auto result = TouchstoneParser::parse(tmpfile);
    std::filesystem::remove(tmpfile);
    REQUIRE(result.has_value());
    REQUIRE(result->num_ports == 1);
    REQUIRE(result->frequencies.size() == 3);
    REQUIRE(result->parameters[0].size() == 1);
}

TEST_CASE("TouchstoneParser reads a multi-digit port count from the extension", "[touchstone]") {
    // Issue #287: ".s10p" was read as 1 port. One point of a 10-port file has 1 + 10*10*2 values.
    std::string tmpfile = std::filesystem::temp_directory_path().string() + "/TEN.s10p";
    {
        std::ofstream ofs(tmpfile);
        ofs << "# GHz S RI R 50\n";
        ofs << "1.0";
        for (int i = 0; i < 200; ++i)
            ofs << " 0.0";
        ofs << "\n";
    }

    auto result = TouchstoneParser::parse(tmpfile);
    std::filesystem::remove(tmpfile);
    REQUIRE(result.has_value());
    REQUIRE(result->num_ports == 10);
    REQUIRE(result->frequencies.size() == 1);
    REQUIRE(result->parameters[0].size() == 100);
}
