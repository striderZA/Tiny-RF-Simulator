#include "amplifier_engine.h"
#include "component_library.h"
#include "component_registry.h"
#include "node_graph_engine.h"
#include "view_manager.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <random>

static std::string write_temp_json(const std::string &content) {
    static std::random_device rd;
    std::uniform_int_distribution<int> dis(100000, 999999);
    auto path = std::filesystem::temp_directory_path() /
                ("test_component_curve_" + std::to_string(dis(rd)) + ".json");
    std::ofstream ofs(path);
    ofs << content;
    ofs.close();
    return path.string();
}

TEST_CASE("ComponentLibrary loads amplifier nf_db_vs_freq", "[component_library][amp_nf_curve]") {
    std::string json = R"({
        "schema_version": 2,
        "type": "amplifier",
        "part_number": "CURVE-AMP",
        "manufacturer": "TestVendor",
        "parameters": {
            "gain_dB": 20.0,
            "nf_db_vs_freq": [[1.0e8, 1.2], [2.0e8, 1.8], [3.0e8, 2.1]]
        }
    })";

    auto path = write_temp_json(json);
    ComponentLibrary lib;
    lib.loadFile(path);
    auto defs = lib.all();

    REQUIRE(defs.size() == 1);
    REQUIRE(defs[0]->parameters.contains("nf_db_vs_freq"));
    REQUIRE(defs[0]->parameters["nf_db_vs_freq"].size() == 3);

    std::filesystem::remove(path);
}

TEST_CASE("ComponentLibrary rejects unsorted amplifier nf_db_vs_freq",
          "[component_library][amp_nf_curve]") {
    ComponentLibrary lib;
    nlohmann::json params = {{"gain_dB", 20.0}, {"nf_db_vs_freq", {{2.0e8, 1.8}, {1.0e8, 1.2}}}};

    auto issues = lib.validate("amplifier", params);
    REQUIRE_FALSE(issues.empty());
    REQUIRE(std::any_of(issues.begin(), issues.end(), [](const ValidationIssue &issue) {
        return issue.field == "nf_db_vs_freq";
    }));
}

TEST_CASE("ComponentLibrary instantiates amplifier nf_db_vs_freq",
          "[component_library][amp_nf_curve]") {
    std::string json = R"({
        "schema_version": 2,
        "type": "amplifier",
        "part_number": "CURVE-AMP-INSTANTIATE",
        "parameters": {
            "gain_dB": 20.0,
            "nf_db_vs_freq": [[1.0e8, 1.2], [2.0e8, 1.8], [3.0e8, 2.1]]
        }
    })";

    auto path = write_temp_json(json);
    ComponentLibrary lib;
    lib.loadFile(path);
    auto defs = lib.all();
    REQUIRE(defs.size() == 1);

    NodeGraphEngine graph;
    ViewManager view;
    ComponentRegistry registry(graph, view);

    auto *engine = lib.instantiate(*defs[0], 100, registry, graph);
    REQUIRE(engine != nullptr);
    auto *amp = dynamic_cast<AmplifierEngine *>(engine);
    REQUIRE(amp != nullptr);
    REQUIRE(amp->hasNfCurve());

    std::filesystem::remove(path);
}
