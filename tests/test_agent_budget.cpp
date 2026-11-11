#if __has_include("imgui.h") || __has_include("imnodes.h")
#error "agent_api tests must remain independent of the UI"
#endif

#include "agent_api.h"
#include "agent_host.h"
#include "receiver_requirements.h"
#include "rf_budget.h"

#include "agent_api_test_support.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace {

using Json = nlohmann::json;

ReceiverRequirementsConfig bandConfig() {
    ReceiverRequirementsConfig config;
    config.band_start_Hz = 1.0e9;
    config.band_stop_Hz = 2.0e9;
    config.gain = ReceiverGainLimits{10.0, 20.0};
    config.measurement_conditions.input_level_dBm = -30.0;
    return config;
}

// Modes 1 and 2 both accept 10..20 dB of gain; only mode 1 has a budget record by default.
ReceiverRequirementsSet twoModeSet() {
    ReceiverRequirementsSet set;
    set.modes.push_back({"Band 1", bandConfig(), 1});
    set.modes.push_back({"Band 2", bandConfig(), 2});
    set.next_mode_id = 3;
    return set;
}

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
    stage.params = params.is_null() ? nlohmann::ordered_json::object() : std::move(params);
    stage.inputs = std::move(inputs);
    return stage;
}

// One 14 dB amplifier into output RX, budgeted for mode 1; mode 2 has no record.
RfBudget stageBudget() {
    RfBudget budget;
    budget.point_count = 5;
    budget.stages.push_back(idealStage(
        "LNA", "amplifier",
        {{"gain_dB", 14.0}, {"nf_dB", 3.0}, {"enable_nonlinear", true}, {"oip3_dBm", 30.0}},
        {fromInput()}));
    budget.outputs.push_back({"RX", "LNA", 0});
    budget.modes.push_back(RfBudgetModeRecord{1, {}});
    return budget;
}

// A band-pass filter that passes no signal at the first grid point.
RfBudget stopbandBudget() {
    RfBudget budget;
    budget.point_count = 5;
    budget.stages.push_back(idealStage("LNA", "amplifier", {{"gain_dB", 10.0}}, {fromInput()}));
    budget.stages.push_back(idealStage(
        "BPF", "filter", {{"filter_type", 2}, {"fc_low_Hz", 1.2e9}, {"fc_high_Hz", 1.8e9}},
        {fromStage("LNA")}));
    budget.outputs.push_back({"RX", "BPF", 0});
    budget.modes.push_back(RfBudgetModeRecord{1, {}});
    return budget;
}

// A switch whose default throw reaches RX1 only; RX2 is unreached for mode 1.
RfBudget switchBudget() {
    RfBudget budget;
    budget.point_count = 3;
    budget.stages.push_back(idealStage("SW", "rf_switch_spdt", {}, {fromInput()}));
    budget.stages.push_back(
        idealStage("A1", "amplifier", {{"gain_dB", 10.0}}, {fromStage("SW", 0)}));
    budget.stages.push_back(
        idealStage("A2", "amplifier", {{"gain_dB", 10.0}}, {fromStage("SW", 1)}));
    budget.outputs.push_back({"RX1", "A1", 0});
    budget.outputs.push_back({"RX2", "A2", 0});
    budget.modes.push_back(RfBudgetModeRecord{1, {}});
    return budget;
}

// A 32-stage chain, 64 mode records, and 401 points: the largest budget the model allows.
struct CappedBudget {
    RfBudget budget;
    ReceiverRequirementsSet requirements;
};

CappedBudget cappedBudget() {
    CappedBudget capped;
    capped.budget.point_count = 401;
    for (int index = 0; index < 32; ++index) {
        const std::string name = "A" + std::to_string(index);
        capped.budget.stages.push_back(
            idealStage(name, "amplifier", {{"gain_dB", 0.5}, {"nf_dB", 1.0}}, {fromInput()}));
        if (index > 0) {
            const std::string previous = "A" + std::to_string(index - 1);
            capped.budget.stages.back().inputs = {fromStage(previous)};
        }
    }
    capped.budget.outputs.push_back({"RX", "A31", 0});
    for (std::uint32_t id = 1; id <= 64; ++id) {
        capped.budget.modes.push_back(RfBudgetModeRecord{id, {}});
        capped.requirements.modes.push_back({"Band " + std::to_string(id), bandConfig(), id});
    }
    capped.requirements.next_mode_id = 65;
    return capped;
}

double asNumber(const nlohmann::json &value) { return value.get<double>(); }

// A per-process library folder holding definitions the shipped library does not.
class TempLibrary {
  public:
    TempLibrary()
        : m_root(std::filesystem::temp_directory_path() /
                 ("agent_budget_library_" + test_temp_paths::processTag() + "_" +
                  std::to_string(++s_count))) {
        std::filesystem::create_directories(m_root);
    }
    ~TempLibrary() {
        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
    }
    TempLibrary(const TempLibrary &) = delete;
    TempLibrary &operator=(const TempLibrary &) = delete;

    void writeDefinition(const std::string &name, const nlohmann::json &definition) const {
        std::ofstream out(m_root / name, std::ios::binary);
        out << definition.dump(2);
    }

    const std::filesystem::path &root() const { return m_root; }

  private:
    static inline int s_count = 0;
    std::filesystem::path m_root;
};

nlohmann::json amplifierDefinition(const std::string &part_number,
                                   const nlohmann::json &parameters) {
    return {{"schema_version", 2},
            {"type", "amplifier"},
            {"part_number", part_number},
            {"manufacturer", "Test"},
            {"description", "budget agent fixture"},
            {"parameters", parameters},
            {"test_conditions", nlohmann::json::object()},
            {"data_files", nlohmann::json::array()},
            {"notes", ""}};
}

// One library amplifier stage with one output, budgeted for mode 1.
RfBudget libraryBudget(const std::string &part_number) {
    RfBudget budget;
    budget.point_count = 5;
    RfBudgetStage stage;
    stage.name = "BAD";
    stage.type = "amplifier";
    stage.source = RfBudgetStageSource::Library;
    stage.part_number = part_number;
    stage.inputs = {fromInput()};
    budget.stages.push_back(std::move(stage));
    budget.outputs.push_back({"RX", "BAD", 0});
    budget.modes.push_back(RfBudgetModeRecord{1, {}});
    return budget;
}

} // namespace

TEST_CASE("budget_get reports verdicts per mode and output", "[agent_budget]") {
    ApiFixture fixture;
    fixture.host.requirements = twoModeSet();
    fixture.host.rf_budget = stageBudget();

    const auto result = fixture.call("budget_get");
    REQUIRE_FALSE(result.is_error);
    CHECK(result.structured.at("epoch") == fixture.api.epoch());
    CHECK(result.structured.at("revision") == fixture.commands.revision());
    CHECK(result.structured.at("dirty") == fixture.commands.isDirty());
    CHECK(result.structured.at("budget").at("format") == 1);
    CHECK(result.structured.at("invalid_reason").is_null());
    CHECK(result.structured.at("not_evaluated_reason").is_null());

    const auto &modes = result.structured.at("modes");
    REQUIRE(modes.size() == 2);
    CHECK(modes.at(0).at("mode_id") == 1);
    CHECK(modes.at(0).at("name") == "Band 1");
    CHECK(modes.at(0).at("status") == "budgeted");
    REQUIRE(modes.at(0).at("outputs").size() == 1);
    CHECK(modes.at(0).at("outputs").at(0).at("output") == "RX");
    const auto &verdict = modes.at(0).at("outputs").at(0).at("verdict");
    CHECK(verdict.at("overall") == "pass");
    CHECK(verdict.at("gain").at("status") == "pass");
    CHECK(asNumber(verdict.at("gain").at("min")) == 14.0);
    CHECK(asNumber(verdict.at("gain").at("max")) == 14.0);
    CHECK(verdict.at("noise_figure").at("status") == "not_configured");
    CHECK(modes.at(0).at("unreached").empty());

    CHECK(modes.at(1).at("mode_id") == 2);
    CHECK(modes.at(1).at("status") == "not_budgeted");
    CHECK(modes.at(1).at("outputs").empty());
    CHECK(result.structured.at("orphaned_mode_ids").empty());
}

TEST_CASE("budget_cascade pages per-frequency series", "[agent_budget]") {
    ApiFixture fixture;
    fixture.host.requirements = twoModeSet();
    fixture.host.rf_budget = stageBudget();

    const auto first =
        fixture.call("budget_cascade", {{"mode_id", 1}, {"output", "RX"}, {"count", 2}});
    REQUIRE_FALSE(first.is_error);
    CHECK(first.structured.at("mode_id") == 1);
    CHECK(first.structured.at("mode_name") == "Band 1");
    CHECK(first.structured.at("output") == "RX");
    CHECK(first.structured.at("point_count") == 5);
    CHECK(first.structured.at("start_index") == 0);
    CHECK(first.structured.at("next_index") == 2);
    CHECK(first.structured.at("frequencies_Hz").size() == 2);
    REQUIRE(first.structured.at("stages").size() == 1);
    const auto &stage = first.structured.at("stages").at(0);
    CHECK(stage.at("stage") == "LNA");
    CHECK(stage.at("gain_db").at("values").size() == 2);
    CHECK(stage.at("gain_db").at("reasons").size() == 2);
    CHECK(first.structured.at("verdict").at("gain").at("status") == "pass");
    CHECK_FALSE(first.structured.at("reasons").empty());

    const auto last = fixture.call(
        "budget_cascade", {{"mode_id", 1}, {"output", "RX"}, {"start_index", 4}, {"count", 2}});
    REQUIRE_FALSE(last.is_error);
    CHECK(last.structured.at("start_index") == 4);
    CHECK(last.structured.at("next_index").is_null());
    CHECK(last.structured.at("frequencies_Hz").size() == 1);
}

TEST_CASE("budget_cascade writes infinities as strings and NaN as null", "[agent_budget]") {
    ApiFixture fixture;
    fixture.host.requirements = twoModeSet();
    fixture.host.rf_budget = stopbandBudget();

    const auto result = fixture.call("budget_cascade", {{"mode_id", 1}, {"output", "RX"}});
    REQUIRE_FALSE(result.is_error);
    const auto &stages = result.structured.at("stages");
    REQUIRE(stages.size() == 2);
    const auto &bpf = stages.at(1);
    CHECK(bpf.at("stage") == "BPF");
    // A stopband point is a real -infinity, not an absent value.
    CHECK(bpf.at("gain_db").at("values").at(0) == "-Infinity");
    CHECK(bpf.at("nf_db").at("values").at(0) == "Infinity");
    // NaN still marks a point with no value, and its reason explains it.
    CHECK(bpf.at("nf_share_percent").at("values").at(0).is_null());
    const auto reason = bpf.at("gain_db").at("reasons").at(0).get<std::uint16_t>();
    CHECK(result.structured.at("reasons").at(reason) ==
          "Stage 'BPF' passes no signal at this frequency.");

    const auto get = fixture.call("budget_get");
    REQUIRE_FALSE(get.is_error);
    const auto &gain =
        get.structured.at("modes").at(0).at("outputs").at(0).at("verdict").at("gain");
    CHECK(gain.at("min") == "-Infinity");
}

TEST_CASE("budget_get reports a library part whose engine params throw", "[agent_budget]") {
    ApiFixture fixture;
    fixture.host.requirements = twoModeSet();
    TempLibrary library_dir;
    // `enable_nonlinear` is unchecked by the library schema, so this definition loads but its
    // engine deserialize() throws; the budget must report a stage reason, not an INTERNAL error.
    library_dir.writeDefinition(
        "badnl.json",
        amplifierDefinition("BADNL-3", {{"gain_dB", 10.0}, {"enable_nonlinear", "yes"}}));
    fixture.library.scan(library_dir.root().string());
    fixture.host.rf_budget = libraryBudget("BADNL-3");

    const auto result = fixture.call("budget_get");
    REQUIRE_FALSE(result.is_error);
    const auto &mode = result.structured.at("modes").at(0);
    CHECK(mode.at("mode_id") == 1);
    CHECK(mode.at("outputs").empty());
    REQUIRE(mode.at("unreached").size() == 1);
    const auto reason = mode.at("unreached").at(0).at("reason").get<std::string>();
    CHECK(reason.find("Library part 'BADNL-3' did not load: ") == 0);
    CHECK(reason.find("type must be boolean") != std::string::npos);
}

TEST_CASE("budget_edit treats an integer parameter equal to its float value", "[agent_budget]") {
    ApiFixture fixture;
    fixture.host.requirements = twoModeSet();
    fixture.host.rf_budget = stageBudget();
    const auto before = fixture.commands.revision();

    Json payload = rfBudgetToJson(stageBudget());
    REQUIRE(payload["stages"][0]["params"]["gain_dB"] == 14.0);
    payload["stages"][0]["params"]["gain_dB"] = 14; // same number, integer JSON type
    const auto result =
        fixture.call("budget_edit", {{"epoch", fixture.api.epoch()}, {"budget", payload}});
    REQUIRE_FALSE(result.is_error);
    CHECK(fixture.commands.revision() == before);
    CHECK(fixture.host.checkpoint_begins == 0);
    CHECK(fixture.host.checkpoint_commits == 0);
}

TEST_CASE("budget_cascade refuses an unreached output with NOT_REACHED", "[agent_budget]") {
    ApiFixture fixture;
    fixture.host.requirements = twoModeSet();
    fixture.host.rf_budget = switchBudget();

    const auto result = fixture.call("budget_cascade", {{"mode_id", 1}, {"output", "RX2"}});
    const auto &error = errorFor(result);
    CHECK(error.at("code") == "NO_MEASUREMENT");
    CHECK(error.at("details").at("reason") == "NOT_REACHED");
    CHECK(error.at("message") == "Switch 'SW' selects T1 in this mode.");
}

TEST_CASE("budget_edit replaces the budget with one checkpoint", "[agent_budget]") {
    ApiFixture fixture;
    fixture.host.requirements = twoModeSet();
    fixture.host.rf_budget = RfBudget{};
    const auto before = fixture.commands.revision();

    const auto result = fixture.call(
        "budget_edit", {{"epoch", fixture.api.epoch()}, {"budget", rfBudgetToJson(stageBudget())}});
    REQUIRE_FALSE(result.is_error);
    REQUIRE(fixture.host.rf_budget.has_value());
    CHECK(*fixture.host.rf_budget == stageBudget());
    CHECK(fixture.commands.revision() == before + 1);
    CHECK(fixture.host.checkpoint_begins == 1);
    CHECK(fixture.host.checkpoint_commits == 1);
    CHECK(fixture.host.last_checkpoint_summary == "rf budget");
    // The edit answers with the budget_get shape.
    CHECK(result.structured.contains("modes"));
    CHECK(result.structured.at("budget").at("format") == 1);
}

TEST_CASE("an identical budget_edit changes nothing", "[agent_budget]") {
    ApiFixture fixture;
    fixture.host.requirements = twoModeSet();
    fixture.host.rf_budget = stageBudget();
    const auto before = fixture.commands.revision();

    const auto result = fixture.call(
        "budget_edit", {{"epoch", fixture.api.epoch()}, {"budget", rfBudgetToJson(stageBudget())}});
    REQUIRE_FALSE(result.is_error);
    CHECK(*fixture.host.rf_budget == stageBudget());
    CHECK(fixture.commands.revision() == before);
    CHECK(fixture.host.checkpoint_begins == 0);
    CHECK(fixture.host.checkpoint_commits == 0);
}

TEST_CASE("budget_edit refuses bad params with PARAM_REJECTED at the stage", "[agent_budget]") {
    ApiFixture fixture;
    fixture.host.requirements = twoModeSet();
    RfBudget current = stageBudget();
    current.stages.push_back(
        idealStage("ATT", "attenuator", {{"atten_dB", 6.0}}, {fromStage("LNA")}));
    current.outputs[0] = {"RX", "ATT", 0};
    fixture.host.rf_budget = current;
    const auto before = fixture.commands.revision();

    Json payload = rfBudgetToJson(current);
    payload["stages"][1]["params"]["atten_db"] = 6.0;
    const auto result =
        fixture.call("budget_edit", {{"epoch", fixture.api.epoch()}, {"budget", payload}});
    const auto &error = errorFor(result);
    CHECK(error.at("code") == "PARAM_REJECTED");
    CHECK(error.at("details").at("path") == "/budget/stages/1/params");
    CHECK_FALSE(error.contains("op_index"));
    CHECK(*fixture.host.rf_budget == current);
    CHECK(fixture.commands.revision() == before);
    CHECK(fixture.host.checkpoint_begins == 0);
}

TEST_CASE("budget_edit refuses an unknown mode id but keeps a stored orphan", "[agent_budget]") {
    ApiFixture fixture;
    fixture.host.requirements = twoModeSet();
    RfBudget current = stageBudget();
    current.modes.push_back(RfBudgetModeRecord{9, {}});
    fixture.host.rf_budget = current;
    const auto before = fixture.commands.revision();

    Json payload = rfBudgetToJson(current);
    payload["modes"].push_back({{"mode_id", 3}, {"overrides", Json::object()}});
    const auto result =
        fixture.call("budget_edit", {{"epoch", fixture.api.epoch()}, {"budget", payload}});
    const auto &error = errorFor(result);
    CHECK(error.at("code") == "INVALID_ARGUMENT");
    CHECK(error.at("details").at("path") == "/budget/modes/2/mode_id");
    CHECK(error.at("message") == "no requirements mode has id 3");
    CHECK(*fixture.host.rf_budget == current);
    CHECK(fixture.commands.revision() == before);
    CHECK(fixture.host.checkpoint_begins == 0);
}

TEST_CASE("budget results stay under the protocol cap", "[agent_budget]") {
    ApiFixture fixture;
    CappedBudget capped = cappedBudget();
    fixture.host.requirements = capped.requirements;
    fixture.host.rf_budget = capped.budget;

    const auto fits = [](const nlohmann::json &structured) {
        const nlohmann::json envelope{
            {"jsonrpc", "2.0"},
            {"id", 1},
            {"result",
             {{"is_error", false}, {"structured", structured}, {"text", structured.dump()}}}};
        return envelope.dump().size() < (1 << 20);
    };

    const auto get = fixture.call("budget_get");
    REQUIRE_FALSE(get.is_error);
    CHECK(fits(get.structured));

    const auto page =
        fixture.call("budget_cascade", {{"mode_id", 1}, {"output", "RX"}, {"count", 50}});
    REQUIRE_FALSE(page.is_error);
    CHECK(page.structured.at("frequencies_Hz").size() == 50);
    CHECK(page.structured.at("next_index") == 50);
    CHECK(fits(page.structured));
}

TEST_CASE("budget_edit refuses a budget whose reply would not fit, and stores nothing",
          "[agent_budget]") {
    ApiFixture fixture;
    fixture.host.requirements = twoModeSet();
    fixture.host.rf_budget = stageBudget();
    const RfBudget stored = *fixture.host.rf_budget;
    const auto before = fixture.commands.revision();
    // The echo carries a part number in full. This one makes the reply larger than a protocol line,
    // which the server would turn into INTERNAL after the budget was already stored.
    const std::string long_part(600 * 1024, 'P');
    TempLibrary library_dir;
    library_dir.writeDefinition("long.json", amplifierDefinition(long_part, {{"gain_dB", 10.0}}));
    fixture.library.scan(library_dir.root().string());

    const auto result =
        fixture.call("budget_edit", {{"epoch", fixture.api.epoch()},
                                     {"budget", rfBudgetToJson(libraryBudget(long_part))}});
    const auto &error = errorFor(result);
    CHECK(error.at("code") == "INVALID_ARGUMENT");
    CHECK(error.at("details").at("path") == "/budget");
    CHECK(error.at("message").get<std::string>().find("too large") != std::string::npos);
    CHECK(*fixture.host.rf_budget == stored);
    CHECK(fixture.commands.revision() == before);
    CHECK(fixture.host.checkpoint_begins == 0);
}
