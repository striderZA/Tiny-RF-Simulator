#include "flow_params.h"
#include "flow_runner.h"
#include "node_graph_engine.h"
#include "signal_generator_engine.h"
#include "test_temp_paths.h"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace {

// Removes the scratch flow file on every exit path, including a failed REQUIRE.
struct ScratchFlowFile {
    std::filesystem::path path;
    ~ScratchFlowFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

} // namespace

TEST_CASE("issue87 params: rejects array indices with leading zeros", "[issue87][params]") {
    nlohmann::json snapshot;
    snapshot["tones"] = nlohmann::json::array({
        nlohmann::json{{"power_dBm", -30.0}},
        nlohmann::json{{"power_dBm", -20.0}},
        nlohmann::json{{"power_dBm", -10.0}},
        nlohmann::json{{"power_dBm", 0.0}},
        nlohmann::json{{"power_dBm", 10.0}},
        nlohmann::json{{"power_dBm", 20.0}},
        nlohmann::json{{"power_dBm", 30.0}},
        nlohmann::json{{"power_dBm", 40.0}},
        nlohmann::json{{"power_dBm", 50.0}},
        nlohmann::json{{"power_dBm", 60.0}},
        nlohmann::json{{"power_dBm", 70.0}},
    });
    std::string error;
    REQUIRE_FALSE(applyConditionValue(snapshot, "tones[00].power_dBm", 1.0, &error));
    REQUIRE(error.find("must not have leading zeros") != std::string::npos);
    error.clear();
    REQUIRE_FALSE(applyConditionValue(snapshot, "tones[01].power_dBm", 1.0, &error));
    REQUIRE(error.find("must not have leading zeros") != std::string::npos);
    error.clear();
    REQUIRE_FALSE(applyConditionValue(snapshot, "tones[0a].power_dBm", 1.0, &error));
    REQUIRE(error.find("non-numeric array index") != std::string::npos);
    error.clear();
    REQUIRE(applyConditionValue(snapshot, "tones[0].power_dBm", 1.0, &error));
    REQUIRE(applyConditionValue(snapshot, "tones[10].power_dBm", 2.0, &error));
}

TEST_CASE("issue87 loader: duplicate array targets cannot use leading-zero aliases",
          "[issue87][loader]") {
    const ScratchFlowFile scratch{
        std::filesystem::temp_directory_path() /
        ("issue87_leading_zero_duplicate_" + test_temp_paths::processTag() + ".flow.json")};
    const std::string path = scratch.path.string();
    {
        std::ofstream out(path);
        REQUIRE(out.is_open());
        out << R"({"version": 1,
            "conditions": [{"component": 100, "path": "tones[0].power_dBm", "values": [1]},
                           {"component": 100, "path": "tones[00].power_dBm", "values": [2]}],
            "measure": [{"component": 100, "metric": "power_dBm"}]})";
        REQUIRE(out.good());
    }
    const auto loaded = LoadFlowFile(path);
    REQUIRE(loaded.ok);
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(100, graph);
    gen.addTone(1e9, -30.0, 0.0);
    std::vector<IComponentEngine *> comps{&gen};
    const std::vector<FlowError> issues = ValidateFlow(loaded.spec, comps);
    REQUIRE_FALSE(issues.empty());
    REQUIRE(issues.front().code == FlowErrorCode::PathNotApplicable);
    REQUIRE(issues.front().message.find("array index must not have leading zeros") !=
            std::string::npos);
}
