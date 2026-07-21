#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "component_library.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

using Catch::Approx;

static std::string write_temp_json(const std::string& content) {
    std::string path = "test_component_temp.json";
    std::ofstream ofs(path);
    ofs << content;
    ofs.close();
    return path;
}

TEST_CASE("ComponentLibrary loads valid amplifier JSON", "[library]") {
    std::string json = R"({
        "schema_version": 1,
        "type": "amplifier",
        "part_number": "TEST-AMP-001",
        "manufacturer": "Test Corp",
        "description": "Test amplifier",
        "parameters": {
            "gain_dB": 20.0,
            "nf_dB": 2.5,
            "oip3_dBm": 30.0,
            "p1db_dBm": 15.0
        }
    })";

    auto path = write_temp_json(json);
    ComponentLibrary lib;
    lib.loadFile(path);

    auto defs = lib.all();
    REQUIRE(defs.size() == 1);
    REQUIRE(defs[0]->type == "amplifier");
    REQUIRE(defs[0]->part_number == "TEST-AMP-001");
    REQUIRE(defs[0]->manufacturer == "Test Corp");
    REQUIRE(defs[0]->parameters["gain_dB"].get<double>() == Approx(20.0));
    REQUIRE(defs[0]->parameters["nf_dB"].get<double>() == Approx(2.5));

    std::filesystem::remove(path);
}

TEST_CASE("ComponentLibrary scans directory recursively", "[library]") {
    std::filesystem::create_directories("test_lib/amplifiers/test");

    std::string json1 = R"({"schema_version":1,"type":"amplifier","part_number":"AMP-001","parameters":{"gain_dB":10.0}})";
    std::string json2 = R"({"schema_version":1,"type":"amplifier","part_number":"AMP-002","parameters":{"gain_dB":20.0}})";

    { std::ofstream ofs("test_lib/amplifiers/test/amp1.json"); ofs << json1; }
    { std::ofstream ofs("test_lib/amplifiers/test/amp2.json"); ofs << json2; }

    ComponentLibrary lib;
    lib.scan("test_lib");

    auto defs = lib.all();
    REQUIRE(defs.size() == 2);

    auto amps = lib.byType("amplifier");
    REQUIRE(amps.size() == 2);

    std::filesystem::remove_all("test_lib");
}
