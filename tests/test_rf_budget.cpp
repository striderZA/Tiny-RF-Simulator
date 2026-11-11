#include "circuit_runtime.h"
#include "component_type_registry.h"
#include "rf_budget.h"

#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

std::optional<RfBudgetFeed> fromInput() { return RfBudgetFeed{std::nullopt, 0}; }

std::optional<RfBudgetFeed> fromStage(const std::string &stage, int port = 0) {
    return RfBudgetFeed{stage, port};
}

RfBudgetStage idealStage(const std::string &name, const std::string &type,
                         nlohmann::ordered_json params,
                         std::vector<std::optional<RfBudgetFeed>> inputs) {
    RfBudgetStage stage;
    stage.name = name;
    stage.type = type;
    stage.source = RfBudgetStageSource::Idealized;
    stage.params = std::move(params);
    stage.inputs = std::move(inputs);
    return stage;
}

// Idealized amplifier -> attenuator chain with one output.
RfBudget chainBudget() {
    RfBudget budget;
    budget.stages.push_back(idealStage(
        "LNA", "amplifier",
        {{"gain_dB", 20.0}, {"nf_dB", 2.0}, {"enable_nonlinear", true}, {"oip3_dBm", 30.0}},
        {fromInput()}));
    budget.stages.push_back(
        idealStage("ATT", "attenuator", {{"atten_dB", 6.0}}, {fromStage("LNA")}));
    budget.outputs.push_back({"RX", "ATT", 0});
    return budget;
}

// Splitter into two amplifiers, one output per branch.
RfBudget branchBudget() {
    RfBudget budget;
    budget.stages.push_back(idealStage("SPLIT", "splitter", {}, {fromInput()}));
    budget.stages.push_back(
        idealStage("A1", "amplifier", {{"gain_dB", 10.0}}, {fromStage("SPLIT", 0)}));
    budget.stages.push_back(
        idealStage("A2", "amplifier", {{"gain_dB", 10.0}}, {fromStage("SPLIT", 1)}));
    budget.outputs.push_back({"RX1", "A1", 0});
    budget.outputs.push_back({"RX2", "A2", 0});
    return budget;
}

// Switched filter bank that rejoins at a 2:1 switch.
RfBudget bankBudget() {
    RfBudget budget;
    budget.stages.push_back(idealStage("SW", "rf_switch_spdt", {}, {fromInput()}));
    budget.stages.push_back(idealStage("F1", "filter", {{"filter_type", 0}}, {fromStage("SW", 0)}));
    budget.stages.push_back(idealStage("F2", "filter", {{"filter_type", 0}}, {fromStage("SW", 1)}));
    budget.stages.push_back(
        idealStage("J", "rf_switch_spdt_2to1", {}, {fromStage("F1", 0), fromStage("F2", 0)}));
    budget.outputs.push_back({"RX", "J", 0});
    return budget;
}

// A loop reachable from the budget input: J is fed by LNA and C, and C by J.
RfBudget loopBudget() {
    RfBudget budget;
    budget.stages.push_back(idealStage("LNA", "amplifier", {{"gain_dB", 20.0}}, {fromInput()}));
    budget.stages.push_back(
        idealStage("J", "rf_switch_spdt_2to1", {}, {fromStage("LNA", 0), fromStage("C", 0)}));
    budget.stages.push_back(
        idealStage("C", "attenuator", {{"atten_dB", 3.0}}, {fromStage("J", 0)}));
    budget.outputs.push_back({"RX", "C", 0});
    return budget;
}

struct RefusalCase {
    const char *label;
    RfBudget budget;
    std::string message;
};

} // namespace

TEST_CASE("budgets with chains, branches, and a switched bank are valid", "[rf_budget][model]") {
    CHECK_FALSE(validateRfBudget(chainBudget()).has_value());
    CHECK_FALSE(validateRfBudget(branchBudget()).has_value());
    CHECK_FALSE(validateRfBudget(bankBudget()).has_value());
}

TEST_CASE("format 1 round-trips", "[rf_budget][model]") {
    RfBudget budget = chainBudget();
    budget.stages[1].source = RfBudgetStageSource::Library;
    budget.stages[1].part_number = "ATT-6";
    budget.stages[1].params = nlohmann::ordered_json::object();
    budget.point_count = 51;
    budget.modes.push_back({1, {{"ATT", {{"atten_dB", 10.0}}}}});
    REQUIRE_FALSE(validateRfBudget(budget).has_value());

    const nlohmann::json encoded = rfBudgetToJson(budget);
    CHECK(encoded["format"] == kRfBudgetFormat);
    CHECK(encoded["point_count"] == 51);
    REQUIRE(encoded["stages"].is_array());
    CHECK(encoded["stages"][0]["source"] == "idealized");
    CHECK(encoded["stages"][0]["params"]["gain_dB"] == 20.0);
    CHECK(encoded["stages"][1]["source"] == "library");
    CHECK(encoded["stages"][1]["part_number"] == "ATT-6");
    CHECK_FALSE(encoded["stages"][1].contains("params"));
    CHECK(encoded["stages"][0]["inputs"][0]["from"] == "input");
    CHECK(encoded["stages"][1]["inputs"][0]["from"] == "stage");
    CHECK(encoded["stages"][1]["inputs"][0]["stage"] == "LNA");
    CHECK(encoded["modes"][0]["mode_id"] == 1);

    const RfBudget decoded = rfBudgetFromJson(encoded);
    CHECK(decoded.invalid_reason.empty());
    CHECK(decoded == budget);

    // An unfed input is null on the wire and nullopt in memory.
    RfBudget partial = bankBudget();
    partial.stages.back().inputs[1] = std::nullopt;
    REQUIRE_FALSE(validateRfBudget(partial).has_value());
    const nlohmann::json partial_json = rfBudgetToJson(partial);
    REQUIRE(partial_json["stages"].back()["inputs"].size() == 2);
    CHECK(partial_json["stages"].back()["inputs"][1].is_null());
    const RfBudget partial_decoded = rfBudgetFromJson(partial_json);
    CHECK(partial_decoded.invalid_reason.empty());
    CHECK(partial_decoded == partial);
    REQUIRE(partial_decoded.stages.back().inputs.size() == 2);
    CHECK_FALSE(partial_decoded.stages.back().inputs[1].has_value());
}

TEST_CASE("refusals name the offending stage", "[rf_budget][model]") {
    std::vector<RefusalCase> cases;

    {
        RfBudget budget = chainBudget();
        while (budget.stages.size() <= kMaxRfBudgetStages)
            budget.stages.push_back(idealStage("X" + std::to_string(budget.stages.size()),
                                               "attenuator", {}, {fromStage("LNA")}));
        cases.push_back({"too_many_stages", budget, "At most 32 stages are allowed."});
    }
    {
        RfBudget budget = chainBudget();
        while (budget.outputs.size() <= kMaxRfBudgetOutputs)
            budget.outputs.push_back({"RX" + std::to_string(budget.outputs.size()), "ATT", 0});
        cases.push_back({"too_many_outputs", budget, "At most 8 outputs are allowed."});
    }
    {
        RfBudget budget = chainBudget();
        for (std::size_t i = 0; i <= kMaxRfBudgetModeRecords; ++i)
            budget.modes.push_back({static_cast<std::uint32_t>(i + 1), {}});
        cases.push_back({"too_many_modes", budget, "At most 64 mode records are allowed."});
    }
    {
        RfBudget budget = chainBudget();
        budget.point_count = 1;
        cases.push_back({"one_point", budget, "Point count must be between 2 and 401."});
    }
    {
        RfBudget budget = chainBudget();
        budget.point_count = 402;
        cases.push_back({"too_many_points", budget, "Point count must be between 2 and 401."});
    }
    {
        RfBudget budget = chainBudget();
        budget.outputs.clear();
        cases.push_back({"no_output", budget, "The budget needs at least one output."});
    }
    {
        RfBudget budget = chainBudget();
        budget.stages[0].name.clear();
        cases.push_back({"empty_stage_name", budget, "Stage name must not be empty."});
    }
    {
        RfBudget budget = chainBudget();
        budget.stages[0].name = std::string(65, 'a');
        cases.push_back({"long_stage_name", budget, "Stage name must be at most 64 characters."});
    }
    {
        RfBudget budget = chainBudget();
        budget.stages[0].name = "L#NA";
        cases.push_back({"hash_stage_name", budget, "Stage name must not contain '#'."});
    }
    {
        RfBudget budget = chainBudget();
        budget.stages[0].name = " LNA";
        cases.push_back(
            {"padded_stage_name", budget, "Stage name must not have leading or trailing spaces."});
    }
    {
        RfBudget budget = chainBudget();
        budget.stages[1].name = "lna";
        cases.push_back({"duplicate_stage_name", budget, "A stage named 'lna' already exists."});
    }
    {
        RfBudget budget = chainBudget();
        budget.outputs[0].name.clear();
        cases.push_back({"empty_output_name", budget, "Output name must not be empty."});
    }
    {
        RfBudget budget = chainBudget();
        budget.outputs[0].name = "R#X";
        cases.push_back({"hash_output_name", budget, "Output name must not contain '#'."});
    }
    {
        RfBudget budget = chainBudget();
        budget.outputs.push_back({"rx", "ATT", 0});
        cases.push_back({"duplicate_output_name", budget, "An output named 'rx' already exists."});
    }
    {
        RfBudget budget = chainBudget();
        budget.stages[0].source = RfBudgetStageSource::Library;
        budget.stages[0].params = nlohmann::ordered_json::object();
        cases.push_back({"library_without_part", budget,
                         "Stage 'LNA' is a library stage and needs a part number."});
    }
    {
        RfBudget budget = chainBudget();
        budget.stages[0].source = RfBudgetStageSource::Library;
        budget.stages[0].part_number = "AM1143";
        cases.push_back(
            {"library_with_params", budget, "Stage 'LNA' is a library stage and takes no params."});
    }
    {
        RfBudget budget = chainBudget();
        budget.stages[0].part_number = "AM1143";
        cases.push_back(
            {"idealized_with_part", budget, "Stage 'LNA' is idealized and takes no part number."});
    }
    {
        RfBudget budget = chainBudget();
        budget.stages[1].type = "mixer";
        cases.push_back({"mixer_stage", budget,
                         "Mixer stages are not supported: the budget keeps every signal at its "
                         "input frequency."});
    }
    {
        RfBudget budget = chainBudget();
        budget.stages[1].type = "combiner";
        cases.push_back(
            {"combiner_stage", budget, "Stage type 'combiner' is not supported in a budget."});
    }
    {
        RfBudget budget = chainBudget();
        budget.stages[1].inputs.clear();
        cases.push_back({"missing_feeds", budget, "Stage 'ATT' needs 1 input feeds."});
    }
    {
        RfBudget budget = chainBudget();
        budget.stages[1].inputs = {std::nullopt};
        cases.push_back({"unfed_input", budget, "Stage 'ATT' input 0 is not fed."});
    }
    {
        RfBudget budget = bankBudget();
        budget.stages[3].inputs = {std::nullopt, std::nullopt};
        cases.push_back({"no_fed_input", budget, "Stage 'J' has no fed input."});
    }
    {
        RfBudget budget = chainBudget();
        budget.stages[1].inputs = {fromStage("NOPE")};
        cases.push_back({"unknown_feed_stage", budget,
                         "Stage 'ATT' input 0 is fed from unknown stage 'NOPE'."});
    }
    {
        RfBudget budget = chainBudget();
        budget.stages[1].inputs = {fromStage("LNA", 1)};
        cases.push_back({"feed_port_out_of_range", budget,
                         "Stage 'ATT' input 0 is fed from port 1 of stage 'LNA', which has 1 "
                         "outputs."});
    }
    {
        RfBudget budget = chainBudget();
        budget.stages[1].inputs = {fromStage("ATT")};
        cases.push_back({"self_feed", budget, "Stage 'ATT' feeds itself."});
    }
    {
        RfBudget budget = chainBudget();
        budget.stages[1].inputs = {fromInput()};
        cases.push_back({"two_input_feeds", budget,
                         "Exactly one stage input must be fed from the budget input."});
    }
    { cases.push_back({"loop", loopBudget(), "The stages form a loop through 'J'."}); }
    {
        RfBudget budget = chainBudget();
        budget.outputs.push_back({"RX2", "ATT", 0});
        cases.push_back(
            {"fan_out", budget, "Port 0 of stage 'ATT' feeds more than one input or output."});
    }
    {
        RfBudget budget = chainBudget();
        budget.stages[1].inputs = {fromStage("D")};
        budget.stages.push_back(idealStage("D", "attenuator", {}, {fromStage("ATT")}));
        budget.outputs[0] = {"RX", "LNA", 0};
        cases.push_back({"unreached", budget, "Stage 'ATT' is not reached from the budget input."});
    }
    {
        RfBudget budget = chainBudget();
        budget.outputs[0] = {"RX", "NOPE", 0};
        cases.push_back(
            {"output_unknown_stage", budget, "Output 'RX' reads unknown stage 'NOPE'."});
    }
    {
        RfBudget budget = chainBudget();
        budget.outputs[0].port = 1;
        cases.push_back({"output_port_out_of_range", budget,
                         "Output 'RX' reads port 1 of stage 'ATT', which has 1 outputs."});
    }
    {
        RfBudget budget = chainBudget();
        budget.modes.push_back({0, {}});
        cases.push_back({"mode_id_zero", budget, "Mode id 0 is not valid."});
    }
    {
        RfBudget budget = chainBudget();
        budget.modes.push_back({3, {}});
        budget.modes.push_back({3, {{"ATT", {{"atten_dB", 4.0}}}}});
        cases.push_back({"duplicate_mode_id", budget, "Mode id 3 has two budget records."});
    }
    {
        RfBudget budget = chainBudget();
        budget.modes.push_back({1, {{"NOPE", {{"atten_dB", 4.0}}}}});
        cases.push_back({"mode_unknown_stage", budget, "Mode 1 overrides unknown stage 'NOPE'."});
    }
    {
        // Stage names are unique case-insensitively, but an override key is matched exactly:
        // 'amp1' is not a name the budget can apply, so accepting it would silently drop it.
        RfBudget budget = chainBudget();
        budget.stages[0].name = "AMP1";
        budget.stages[1].inputs = {fromStage("AMP1")};
        budget.modes.push_back({1, {{"amp1", {{"atten_dB", 4.0}}}}});
        cases.push_back(
            {"mode_override_case_mismatch", budget, "Mode 1 overrides unknown stage 'amp1'."});
    }
    {
        // Feeds and outputs name a stage exactly, as the cascade walk does.
        RfBudget budget = chainBudget();
        budget.stages[1].inputs = {fromStage("lna")};
        cases.push_back({"feed_stage_case_mismatch", budget,
                         "Stage 'ATT' input 0 is fed from unknown stage 'lna'."});
    }
    {
        RfBudget budget = chainBudget();
        budget.outputs[0] = {"RX", "att", 0};
        cases.push_back(
            {"output_stage_case_mismatch", budget, "Output 'RX' reads unknown stage 'att'."});
    }
    {
        RfBudget budget = chainBudget();
        budget.modes.push_back(
            {1, {{"ATT", {{"atten_dB", std::numeric_limits<double>::infinity()}}}}});
        cases.push_back(
            {"mode_non_finite", budget, "Mode 1 override 'ATT.atten_dB' must be a finite number."});
    }

    for (const auto &refusal : cases) {
        INFO(refusal.label);
        const auto reason = validateRfBudget(refusal.budget);
        REQUIRE(reason.has_value());
        CHECK(*reason == refusal.message);
    }
}

TEST_CASE("decode errors keep only the reason", "[rf_budget][model]") {
    const nlohmann::json stage = {{"name", "LNA"},
                                  {"type", "amplifier"},
                                  {"source", "idealized"},
                                  {"inputs", nlohmann::json::array({{{"from", "input"}}})}};
    const nlohmann::json output = {{"name", "RX"}, {"stage", "LNA"}, {"port", 0}};
    const std::vector<std::pair<std::string, std::pair<nlohmann::json, std::string>>> cases = {
        {"not_object", {nlohmann::json::array(), "RF budget must be a JSON object."}},
        {"unsupported_format", {nlohmann::json{{"format", 2}}, "Unsupported RF budget format 2."}},
        {"stages_not_list",
         {nlohmann::json{{"format", 1}, {"stages", "x"}}, "RF budget stages must be a list."}},
        {"stage_not_object",
         {nlohmann::json{{"format", 1}, {"stages", nlohmann::json::array({1})}},
          "Stage 1: Stage must be a JSON object."}},
        {"stage_name_not_string",
         {nlohmann::json{{"format", 1}, {"stages", nlohmann::json::array({{{"name", 1}}})}},
          "Stage 1: Stage name must be a string."}},
        {"point_count_not_integer",
         {nlohmann::json{{"format", 1}, {"point_count", 1.5}}, "Point count must be an integer."}},
        {"outputs_not_list",
         {nlohmann::json{{"format", 1}, {"stages", nlohmann::json::array({stage})}, {"outputs", 3}},
          "RF budget outputs must be a list."}},
        {"output_unknown_port",
         {nlohmann::json{{"format", 1},
                         {"stages", nlohmann::json::array({stage})},
                         {"outputs", nlohmann::json::array(
                                         {{{"name", "RX"}, {"stage", "LNA"}, {"port", 1}}})}},
          "Output 'RX' reads port 1 of stage 'LNA', which has 1 outputs."}},
        {"input_port_above_int",
         {nlohmann::json{
              {"format", 1},
              {"stages", nlohmann::json::array(
                             {{{"name", "LNA"},
                               {"type", "amplifier"},
                               {"source", "idealized"},
                               {"inputs", nlohmann::json::array(
                                              {{{"from", "input"}, {"port", 4294967296}}})}}})},
              {"outputs", nlohmann::json::array({output})}},
          "Stage 1: Stage input 0: port must be a non-negative integer."}},
        {"output_port_above_int",
         {nlohmann::json{
              {"format", 1},
              {"stages", nlohmann::json::array({stage})},
              {"outputs",
               nlohmann::json::array({{{"name", "RX"}, {"stage", "LNA"}, {"port", 4294967296}}})}},
          "Output 1: Output port must be a non-negative integer."}},
    };

    for (const auto &[label, entry] : cases) {
        INFO(label);
        const RfBudget decoded = rfBudgetFromJson(entry.first);
        CHECK(decoded.invalid_reason == entry.second);
        CHECK(decoded.stages.empty());
        CHECK(decoded.outputs.empty());
        CHECK(decoded.modes.empty());
        CHECK(decoded.point_count == kDefaultRfBudgetPoints);
    }

    // A structurally valid object still runs the model validation.
    nlohmann::json duplicate_names = {{"format", 1},
                                      {"stages", nlohmann::json::array({stage, stage})},
                                      {"outputs", nlohmann::json::array({output})}};
    CHECK(rfBudgetFromJson(duplicate_names).invalid_reason ==
          "A stage named 'LNA' already exists.");
}

TEST_CASE("budget equality compares numbers by value, not by JSON type", "[rf_budget][model]") {
    RfBudget left = chainBudget();
    RfBudget right = chainBudget();
    // ordered_json keeps 20 as an integer and 20.0 as a float, but a parameter means the number.
    right.stages[0].params["gain_dB"] = 20;
    CHECK(left == right);

    right.stages[0].params["gain_dB"] = 20.5;
    CHECK_FALSE(left == right);
}

TEST_CASE("stage type table matches the engines", "[rf_budget][model]") {
    const auto &types = rfBudgetStageTypes();
    CHECK(types.size() == 8);
    CircuitRuntime runtime;
    for (const auto &entry : types) {
        INFO(entry.type);
        const auto *descriptor = ComponentTypeRegistry::instance().find(std::string(entry.type));
        REQUIRE(descriptor != nullptr);
        IComponentEngine *engine = runtime.createComponent(descriptor->create);
        REQUIRE(engine != nullptr);
        CHECK(engine->numInputPins() == entry.inputs);
        CHECK(engine->numOutputPins() == entry.outputs);
    }
}

TEST_CASE("add stage feeds the first stage from the budget input", "[rf_budget][edit]") {
    RfBudget budget;
    std::string error;
    REQUIRE(addRfBudgetStage(budget, "amplifier", error));
    REQUIRE(budget.stages.size() == 1);
    CHECK(budget.stages[0].name == "Stage 1");
    CHECK(budget.stages[0].type == "amplifier");
    REQUIRE(budget.stages[0].inputs.size() == 1);
    REQUIRE(budget.stages[0].inputs[0].has_value());
    CHECK_FALSE(budget.stages[0].inputs[0]->stage.has_value());

    REQUIRE(addRfBudgetStage(budget, "rf_switch_spdt_2to1", error));
    REQUIRE(budget.stages.size() == 2);
    CHECK(budget.stages[1].name == "Stage 2");
    REQUIRE(budget.stages[1].inputs.size() == 2);
    // Only one stage may be fed from the budget input.
    CHECK_FALSE(budget.stages[1].inputs[0].has_value());
    CHECK_FALSE(budget.stages[1].inputs[1].has_value());

    CHECK_FALSE(addRfBudgetStage(budget, "mixer", error));
    CHECK(error == "Mixer stages are not supported: the budget keeps every signal at its input "
                   "frequency.");
    CHECK_FALSE(addRfBudgetStage(budget, "combiner", error));
    CHECK(error == "Stage type 'combiner' is not supported in a budget.");
}

TEST_CASE("stage rename rewrites feeds, outputs, and mode overrides", "[rf_budget][edit]") {
    RfBudget budget = chainBudget();
    budget.modes.push_back(RfBudgetModeRecord{7, {{"ATT", {{"atten_dB", 3.0}}}}});
    std::string error;

    REQUIRE(renameRfBudgetStage(budget, 1, "ATT2", error));
    CHECK(budget.stages[1].name == "ATT2");
    CHECK(budget.outputs[0].stage == "ATT2");
    REQUIRE(budget.modes.size() == 1);
    CHECK(budget.modes[0].overrides.count("ATT2") == 1);
    CHECK(budget.modes[0].overrides.count("ATT") == 0);

    // A case-insensitive duplicate and an invalid name are refused and change nothing.
    CHECK_FALSE(renameRfBudgetStage(budget, 0, "att2", error));
    CHECK(error == "A stage named 'att2' already exists.");
    CHECK(budget.stages[0].name == "LNA");
    CHECK_FALSE(renameRfBudgetStage(budget, 0, " bad name ", error));
    CHECK(error == "Stage name must not have leading or trailing spaces.");
    CHECK(budget.stages[0].name == "LNA");
}

TEST_CASE("stage removal drops the references to it", "[rf_budget][edit]") {
    RfBudget budget = chainBudget();
    budget.modes.push_back(RfBudgetModeRecord{3, {{"ATT", {{"atten_dB", 1.0}}}}});
    REQUIRE(removeRfBudgetStage(budget, 1));
    REQUIRE(budget.stages.size() == 1);
    CHECK(budget.outputs.empty());
    CHECK(budget.modes[0].overrides.empty());

    RfBudget unfed = chainBudget();
    REQUIRE(removeRfBudgetStage(unfed, 0));
    REQUIRE(unfed.stages.size() == 1);
    CHECK_FALSE(unfed.stages[0].inputs[0].has_value());
    CHECK_FALSE(removeRfBudgetStage(unfed, 5));
}

TEST_CASE("stage type change resizes feeds and clears source fields", "[rf_budget][edit]") {
    RfBudget budget;
    RfBudgetStage stage;
    stage.name = "S";
    stage.type = "amplifier";
    stage.source = RfBudgetStageSource::Library;
    stage.part_number = "AMP-1";
    stage.params = {{"gain_dB", 10.0}};
    stage.inputs = {fromInput()};
    budget.stages.push_back(stage);
    budget.outputs.push_back({"RX", "S", 0});

    setRfBudgetStageType(budget, 0, "rf_switch_spdt_2to1");
    CHECK(budget.stages[0].type == "rf_switch_spdt_2to1");
    REQUIRE(budget.stages[0].inputs.size() == 2);
    REQUIRE(budget.stages[0].inputs[0].has_value());
    CHECK(budget.stages[0].params.empty());
    CHECK(budget.stages[0].part_number.empty());

    setRfBudgetStageType(budget, 0, "splitter");
    REQUIRE(budget.stages[0].inputs.size() == 1);
    CHECK(budget.stages[0].inputs[0].has_value());
}

TEST_CASE("outputs are added with unique names and validated ports", "[rf_budget][edit]") {
    RfBudget budget = chainBudget();
    budget.outputs.clear();
    std::string error;
    REQUIRE(addRfBudgetOutput(budget, 1, 0, error));
    REQUIRE(addRfBudgetOutput(budget, 0, 0, error));
    REQUIRE(budget.outputs.size() == 2);
    CHECK(budget.outputs[0].name == "OUT 1");
    CHECK(budget.outputs[1].name == "OUT 2");
    CHECK(budget.outputs[0].stage == "ATT");

    CHECK_FALSE(addRfBudgetOutput(budget, 1, 1, error));
    CHECK(error == "Output port must be between 0 and 0.");
    CHECK_FALSE(addRfBudgetOutput(budget, 9, 0, error));
    CHECK(error == "Stage index is out of range.");

    REQUIRE(removeRfBudgetOutput(budget, 0));
    CHECK(budget.outputs.size() == 1);
    CHECK_FALSE(removeRfBudgetOutput(budget, 4));
}

TEST_CASE("mode records and overrides are created and erased in place", "[rf_budget][edit]") {
    RfBudget budget = chainBudget();
    RfBudgetModeRecord &record = ensureRfBudgetModeRecord(budget, 5);
    CHECK(record.mode_id == 5);
    CHECK(&ensureRfBudgetModeRecord(budget, 5) == &record);
    CHECK(budget.modes.size() == 1);

    setRfBudgetOverride(budget, 5, "ATT", "atten_dB", 6.0);
    REQUIRE(budget.modes[0].overrides.count("ATT") == 1);
    CHECK(budget.modes[0].overrides.at("ATT").at("atten_dB") == 6.0);

    REQUIRE(eraseRfBudgetOverride(budget, 5, "ATT", "atten_dB"));
    CHECK(budget.modes[0].overrides.empty());
    CHECK(budget.modes.size() == 1); // the record still marks the mode as budgeted
    CHECK_FALSE(eraseRfBudgetOverride(budget, 5, "ATT", "atten_dB"));

    eraseRfBudgetModeRecord(budget, 5);
    CHECK(budget.modes.empty());
}
