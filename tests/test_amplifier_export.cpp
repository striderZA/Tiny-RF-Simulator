#include "amplifier_export_service.h"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

TEST_CASE("exportAmplifier writes json and s2p with nf_db_vs_freq", "[amp_export]") {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "rfsim_amp_export_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);

    AmplifierExportRequest req;
    req.project_root = root;
    req.part_number = "TESTAMP";
    req.manufacturer = "Test Vendor";
    req.gain_db_vs_freq = {{1.0e8, 20.0}, {2.0e8, 21.0}};
    req.nf_db_vs_freq = {{1.0e8, 1.2}, {2.0e8, 1.5}};
    req.input_return_loss_db = 20.0;
    req.output_return_loss_db = 20.0;

    const auto result = exportAmplifier(req);
    REQUIRE(result.ok);
    REQUIRE(fs::exists(result.json_path));
    REQUIRE(fs::exists(result.s2p_path));

    const auto json = nlohmann::json::parse(std::ifstream(result.json_path), nullptr, false);
    REQUIRE_FALSE(json.is_discarded());
    REQUIRE(json["parameters"].contains("nf_db_vs_freq"));
    REQUIRE(json["parameters"]["nf_db_vs_freq"].size() == 2);

    fs::remove_all(root, ec);
}
