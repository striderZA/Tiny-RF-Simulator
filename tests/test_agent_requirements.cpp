#if __has_include("imgui.h") || __has_include("imnodes.h")
#error "agent_api tests must remain independent of the UI"
#endif

#include "agent_api.h"
#include "agent_host.h"
#include "receiver_requirements.h"

#include "agent_api_test_support.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <utility>

namespace {

// Mode "HG" carries a configured limit set; mode "LG" stays unconfigured, and the selected mode is
// the unconfigured one.
ReceiverRequirementsSet twoModeSet() {
    ReceiverRequirementsSet set;
    ReceiverRequirementsConfig high;
    high.band_start_Hz = 1.0e9;
    high.band_stop_Hz = 2.0e9;
    high.gain = ReceiverGainLimits{10.0, 20.0};
    high.nf_max_dB = 5.0;
    set.modes.push_back({"HG", high});
    set.modes.push_back({"LG", std::nullopt});
    set.selected = 1;
    return set;
}

} // namespace

TEST_CASE("requirements_get returns the host requirements", "[agent_requirements]") {
    ApiFixture fixture;
    fixture.host.requirements = twoModeSet();
    const auto result = fixture.call("requirements_get");
    REQUIRE_FALSE(result.is_error);
    CHECK(result.structured.at("requirements").at("modes").size() == 2);
    CHECK(result.structured.at("requirements").at("selected_mode") == 1);
    CHECK(result.structured.at("invalid_reason").is_null());
    CHECK(result.structured.at("revision") == fixture.commands.revision());
}

TEST_CASE("requirements_edit replaces the requirements with one checkpoint",
          "[agent_requirements]") {
    ApiFixture fixture;
    const auto before = fixture.commands.revision();
    auto next = twoModeSet();
    const auto result =
        fixture.call("requirements_edit", {{"epoch", fixture.api.epoch()},
                                           {"requirements", receiverRequirementsToJson(next)}});
    REQUIRE_FALSE(result.is_error);
    CHECK(*fixture.host.requirements == next);
    CHECK(fixture.commands.revision() == before + 1);
    CHECK(fixture.host.checkpoint_begins == 1);
    CHECK(fixture.host.checkpoint_commits == 1);
    CHECK(fixture.host.last_checkpoint_summary == "receiver requirements");
}

TEST_CASE("requirements_edit with identical requirements changes nothing", "[agent_requirements]") {
    ApiFixture fixture;
    fixture.host.requirements = twoModeSet();
    const auto before = fixture.commands.revision();
    const auto result = fixture.call("requirements_edit",
                                     {{"epoch", fixture.api.epoch()},
                                      {"requirements", receiverRequirementsToJson(twoModeSet())}});
    REQUIRE_FALSE(result.is_error);
    CHECK(*fixture.host.requirements == twoModeSet());
    CHECK(fixture.commands.revision() == before);
    CHECK(fixture.host.checkpoint_begins == 0);
    CHECK(fixture.host.checkpoint_commits == 0);
}

TEST_CASE("requirements_edit refuses invalid requirements without a checkpoint",
          "[agent_requirements]") {
    ApiFixture fixture;
    fixture.host.requirements = twoModeSet();
    const auto before = fixture.commands.revision();
    const auto unchanged = [&] {
        CHECK(*fixture.host.requirements == twoModeSet());
        CHECK(fixture.commands.revision() == before);
        CHECK(fixture.host.checkpoint_begins == 0);
    };
    const auto edit = [&](nlohmann::ordered_json requirements) {
        return fixture.call("requirements_edit", {{"epoch", fixture.api.epoch()},
                                                  {"requirements", std::move(requirements)}});
    };

    SECTION("mode names that differ only by case") {
        ReceiverRequirementsSet duplicate;
        duplicate.modes.push_back({"A", std::nullopt});
        duplicate.modes.push_back({"a", std::nullopt});
        const auto result = edit(receiverRequirementsToJson(duplicate));
        const auto &error = errorFor(result);
        CHECK(error.at("code") == "INVALID_ARGUMENT");
        CHECK(error.at("details").at("path") == "/requirements");
        CHECK(error.at("message") == "A mode named 'a' already exists.");
        unchanged();
    }
    SECTION("an object without a format") {
        const auto result =
            edit({{"selected_mode", 0}, {"modes", nlohmann::ordered_json::array()}});
        const auto &error = errorFor(result);
        CHECK(error.at("code") == "INVALID_ARGUMENT");
        CHECK(error.at("details").at("path") == "/requirements/format");
        unchanged();
    }
    SECTION("a mode whose input level is out of range") {
        ReceiverRequirementsSet invalid;
        ReceiverRequirementsConfig config;
        config.band_start_Hz = 1.0e9;
        config.band_stop_Hz = 2.0e9;
        config.measurement_conditions.input_level_dBm = 20.0;
        invalid.modes.push_back({"A", config});
        const auto result = edit(receiverRequirementsToJson(invalid));
        const auto &error = errorFor(result);
        CHECK(error.at("code") == "INVALID_ARGUMENT");
        CHECK(error.at("details").at("path") == "/requirements");
        // The decoder prefixes structural config errors with the 1-based mode number, as the plan
        // specifies ("Mode <n>: <reason>"); the set-level validator uses the name instead.
        CHECK(error.at("message") == "Mode 1: Input level must be between -174 and 10 dBm.");
        unchanged();
    }
}

TEST_CASE("requirements_edit checks the epoch first", "[agent_requirements]") {
    ApiFixture fixture;
    fixture.host.requirements = twoModeSet();
    const auto result = fixture.call("requirements_edit",
                                     {{"epoch", fixture.api.epoch() + 1}, {"requirements", 5}});
    CHECK(errorFor(result).at("code") == "STALE_EPOCH");
    CHECK(fixture.host.checkpoint_begins == 0);
}

TEST_CASE("requirements_edit reports a host that refuses the write", "[agent_requirements]") {
    ApiFixture fixture;
    fixture.host.requirements = twoModeSet();
    fixture.host.requirements_write_succeeds = false;
    const auto before = fixture.commands.revision();
    ReceiverRequirementsSet next;
    next.modes.push_back({"Default", std::nullopt});

    const auto result =
        fixture.call("requirements_edit", {{"epoch", fixture.api.epoch()},
                                           {"requirements", receiverRequirementsToJson(next)}});
    const auto &error = errorFor(result);
    CHECK(error.at("code") == "INTERNAL");
    CHECK(error.at("message") == "the host rejected the receiver requirements");
    // The write changed nothing, so the project stays as it was and no checkpoint survives.
    CHECK(*fixture.host.requirements == twoModeSet());
    CHECK(fixture.commands.revision() == before);
    CHECK(fixture.host.checkpoint_begins == 1);
    CHECK(fixture.host.checkpoint_commits == 0);
    CHECK(fixture.host.last_checkpoint_summary != "interrupted: receiver requirements");
    CHECK(fixture.host.checkpoint_discards == 1);
}

TEST_CASE("requirements tools report INTERNAL without host support", "[agent_requirements]") {
    ApiFixture fixture;
    fixture.host.requirements = std::nullopt;
    const auto get = fixture.call("requirements_get");
    CHECK(errorFor(get).at("code") == "INTERNAL");
    const auto edit = fixture.call("requirements_edit",
                                   {{"epoch", fixture.api.epoch()},
                                    {"requirements", receiverRequirementsToJson(twoModeSet())}});
    CHECK(errorFor(edit).at("code") == "INTERNAL");
    CHECK(fixture.host.checkpoint_begins == 0);
}

TEST_CASE("requirements_edit refuses misspelled nested keys and names the full path",
          "[agent_requirements]") {
    ApiFixture fixture;
    fixture.host.requirements = twoModeSet();
    const auto before = fixture.commands.revision();
    const auto refused = [&](nlohmann::json requirements) {
        const auto result =
            fixture.call("requirements_edit", {{"epoch", fixture.api.epoch()},
                                               {"requirements", std::move(requirements)}});
        const auto &error = errorFor(result);
        CHECK(error.at("code") == "INVALID_ARGUMENT");
        CHECK(*fixture.host.requirements == twoModeSet());
        CHECK(fixture.commands.revision() == before);
        CHECK(fixture.host.checkpoint_begins == 0);
        return error.at("details").at("path");
    };

    SECTION("a misspelled gain key in a mode config") {
        nlohmann::json requirements = receiverRequirementsToJson(twoModeSet());
        requirements["modes"][0]["config"]["gain_min_dB"] = 10.0;
        CHECK(refused(requirements) == "/requirements/modes/0/config/gain_min_dB");
    }
    SECTION("a misspelled input level under measurement conditions") {
        nlohmann::json requirements = receiverRequirementsToJson(twoModeSet());
        requirements["modes"][0]["config"]["measurement_conditions"]["input_level_dBm"] = -30.0;
        CHECK(refused(requirements) ==
              "/requirements/modes/0/config/measurement_conditions/input_level_dBm");
    }
    SECTION("an unknown key in the requirements object") {
        nlohmann::json requirements = receiverRequirementsToJson(twoModeSet());
        requirements["extra"] = 1;
        CHECK(refused(requirements) == "/requirements/extra");
    }
}
