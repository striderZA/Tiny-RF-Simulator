#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "touchstone_parser.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numbers>

using Catch::Approx;

TEST_CASE("TouchstoneParser parses real .s2p file", "[touchstone]") {
    std::string path = std::string(PROJECT_SOURCE_DIR) + "/component_data/amplifiers/adm-3844psm/ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
    auto result = TouchstoneParser::parse(path);
    REQUIRE(result.has_value());

    const auto& data = *result;
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

    const auto& data = *result;
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

    const auto& data = *result;
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
    REQUIRE(data.parameters[0][3].real() == Approx(0.316227766 * std::cos(-45.0 * std::numbers::pi / 180.0)).epsilon(1e-6));
    REQUIRE(data.parameters[0][3].imag() == Approx(0.316227766 * std::sin(-45.0 * std::numbers::pi / 180.0)).epsilon(1e-6));

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
