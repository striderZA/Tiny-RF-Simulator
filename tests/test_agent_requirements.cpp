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
// the unconfigured one. Both carry the ids the stored set assigned.
ReceiverRequirementsSet twoModeSet() {
    ReceiverRequirementsSet set;
    ReceiverRequirementsConfig high;
    high.band_start_Hz = 1.0e9;
    high.band_stop_Hz = 2.0e9;
    high.gain = ReceiverGainLimits{10.0, 20.0};
    high.nf_max_dB = 5.0;
    set.modes.push_back({"HG", high, 1});
    set.modes.push_back({"LG", std::nullopt, 2});
    set.selected = 1;
    set.next_mode_id = 3;
    return set;
}

// The edit payload: the encoded set without next_mode_id, which requirements_edit refuses.
nlohmann::json editPayload(const ReceiverRequirementsSet &set) {
    nlohmann::json payload = receiverRequirementsToJson(set);
    payload.erase("next_mode_id");
    return payload;
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
    // The empty host has no modes to match, so the sent modes carry no ids and get new ones.
    auto payload = editPayload(twoModeSet());
    for (auto &mode : payload["modes"])
        mode.erase("id");
    const auto result = fixture.call("requirements_edit",
                                     {{"epoch", fixture.api.epoch()}, {"requirements", payload}});
    REQUIRE_FALSE(result.is_error);
    auto expected = twoModeSet();
    for (auto &mode : expected.modes)
        mode.id = 0;
    expected.next_mode_id = 1;
    assignReceiverRequirementsModeIds(expected);
    CHECK(*fixture.host.requirements == expected);
    CHECK(fixture.commands.revision() == before + 1);
    CHECK(fixture.host.checkpoint_begins == 1);
    CHECK(fixture.host.checkpoint_commits == 1);
    CHECK(fixture.host.last_checkpoint_summary == "receiver requirements");
}

TEST_CASE("requirements_edit with identical requirements changes nothing", "[agent_requirements]") {
    ApiFixture fixture;
    fixture.host.requirements = twoModeSet();
    const auto before = fixture.commands.revision();
    const auto result =
        fixture.call("requirements_edit",
                     {{"epoch", fixture.api.epoch()}, {"requirements", editPayload(twoModeSet())}});
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
        const auto result = edit(editPayload(duplicate));
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
        const auto result = edit(editPayload(invalid));
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

    const auto result = fixture.call(
        "requirements_edit", {{"epoch", fixture.api.epoch()}, {"requirements", editPayload(next)}});
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
    const auto edit =
        fixture.call("requirements_edit",
                     {{"epoch", fixture.api.epoch()}, {"requirements", editPayload(twoModeSet())}});
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
        nlohmann::json requirements = editPayload(twoModeSet());
        requirements["modes"][0]["config"]["gain_min_dB"] = 10.0;
        CHECK(refused(requirements) == "/requirements/modes/0/config/gain_min_dB");
    }
    SECTION("a misspelled input level under measurement conditions") {
        nlohmann::json requirements = editPayload(twoModeSet());
        requirements["modes"][0]["config"]["measurement_conditions"]["input_level_dBm"] = -30.0;
        CHECK(refused(requirements) ==
              "/requirements/modes/0/config/measurement_conditions/input_level_dBm");
    }
    SECTION("an unknown key in the requirements object") {
        nlohmann::json requirements = editPayload(twoModeSet());
        requirements["extra"] = 1;
        CHECK(refused(requirements) == "/requirements/extra");
    }
}

TEST_CASE("requirements_edit refuses an edit made against an older read", "[agent_requirements]") {
    ApiFixture fixture;
    fixture.host.requirements = twoModeSet();
    const std::uint64_t revision = fixture.call("requirements_get").structured.at("revision");
    // A GUI Apply or circuit edit after the read advances the project revision.
    fixture.commands.markModified();
    const auto before = fixture.commands.revision();

    auto next = twoModeSet();
    next.modes[0].config->nf_max_dB = 9.0;
    const auto result = fixture.call("requirements_edit", {{"epoch", fixture.api.epoch()},
                                                           {"revision", revision},
                                                           {"requirements", editPayload(next)}});
    CHECK(errorFor(result).at("code") == "BUSY");
    CHECK(*fixture.host.requirements == twoModeSet());
    CHECK(fixture.commands.revision() == before);
    CHECK(fixture.host.checkpoint_begins == 0);
}

TEST_CASE("requirements_edit applies when the revision is current", "[agent_requirements]") {
    ApiFixture fixture;
    fixture.host.requirements = twoModeSet();
    const std::uint64_t revision = fixture.call("requirements_get").structured.at("revision");

    auto next = twoModeSet();
    next.modes[0].config->nf_max_dB = 9.0;
    const auto result = fixture.call("requirements_edit", {{"epoch", fixture.api.epoch()},
                                                           {"revision", revision},
                                                           {"requirements", editPayload(next)}});
    REQUIRE_FALSE(result.is_error);
    CHECK(*fixture.host.requirements == next);
}

TEST_CASE("requirements_edit rejects a malformed revision", "[agent_requirements]") {
    ApiFixture fixture;
    fixture.host.requirements = twoModeSet();
    const auto result =
        fixture.call("requirements_edit", {{"epoch", fixture.api.epoch()},
                                           {"revision", -1},
                                           {"requirements", editPayload(twoModeSet())}});
    CHECK(errorFor(result).at("code") == "INVALID_ARGUMENT");
    CHECK(errorFor(result).at("details").at("path") == "/revision");
    CHECK(fixture.host.checkpoint_begins == 0);
}

TEST_CASE("requirements_get reports format 3 ids without next_mode_id", "[agent_requirements]") {
    ApiFixture fixture;
    fixture.host.requirements = twoModeSet();
    const auto result = fixture.call("requirements_get");
    REQUIRE_FALSE(result.is_error);
    const auto &requirements = result.structured.at("requirements");
    CHECK(requirements.at("format") == 3);
    CHECK(requirements.at("modes")[0].at("id") == 1);
    CHECK(requirements.at("modes")[1].at("id") == 2);
    CHECK_FALSE(requirements.contains("next_mode_id"));
}

TEST_CASE("requirements_edit renames a mode and keeps its id", "[agent_requirements]") {
    ApiFixture fixture;
    fixture.host.requirements = twoModeSet();
    const auto before = fixture.commands.revision();
    auto payload = editPayload(twoModeSet());
    payload["modes"][0]["name"] = "Alpha";
    const auto result = fixture.call("requirements_edit",
                                     {{"epoch", fixture.api.epoch()}, {"requirements", payload}});
    REQUIRE_FALSE(result.is_error);
    REQUIRE(fixture.host.requirements->modes.size() == 2);
    CHECK(fixture.host.requirements->modes[0].id == 1);
    CHECK(fixture.host.requirements->modes[0].name == "Alpha");
    CHECK(fixture.commands.revision() == before + 1);
}

TEST_CASE("requirements_edit refuses an unknown mode id without a checkpoint",
          "[agent_requirements]") {
    ApiFixture fixture;
    fixture.host.requirements = twoModeSet();
    const auto before = fixture.commands.revision();
    auto payload = editPayload(twoModeSet());
    payload["modes"][0]["id"] = 9;
    const auto result = fixture.call("requirements_edit",
                                     {{"epoch", fixture.api.epoch()}, {"requirements", payload}});
    const auto &error = errorFor(result);
    CHECK(error.at("code") == "INVALID_ARGUMENT");
    CHECK(error.at("details").at("path") == "/requirements/modes/0/id");
    CHECK(error.at("message") == "no mode has id 9; omit id to add a mode");
    CHECK(*fixture.host.requirements == twoModeSet());
    CHECK(fixture.commands.revision() == before);
    CHECK(fixture.host.checkpoint_begins == 0);
}

TEST_CASE("requirements_edit stores out-of-band suppression points", "[agent_requirements]") {
    ApiFixture fixture;
    fixture.host.requirements = twoModeSet();
    auto next = twoModeSet();
    next.modes[0].config->oob_suppression = {
        {ReceiverOOBSuppressionAnchor::BandStart, -5.0e6, 60.0},
        {ReceiverOOBSuppressionAnchor::BandStop, 2.5e6, 40.0}};

    const auto result = fixture.call(
        "requirements_edit", {{"epoch", fixture.api.epoch()}, {"requirements", editPayload(next)}});
    REQUIRE_FALSE(result.is_error);
    REQUIRE(fixture.host.requirements->modes[0].config.has_value());
    CHECK(fixture.host.requirements->modes[0].config->oob_suppression ==
          next.modes[0].config->oob_suppression);

    const auto read = fixture.call("requirements_get");
    REQUIRE_FALSE(read.is_error);
    const auto &points =
        read.structured.at("requirements").at("modes").at(0).at("config").at("oob_suppression");
    REQUIRE(points.size() == 2);
    CHECK(points[0].at("anchor") == "band_start");
    CHECK(points[1].at("anchor") == "band_stop");
}

TEST_CASE("requirements_edit refuses a misspelled out-of-band point key", "[agent_requirements]") {
    ApiFixture fixture;
    fixture.host.requirements = twoModeSet();
    const auto before = fixture.commands.revision();
    auto payload = editPayload(twoModeSet());
    payload["modes"][0]["config"]["oob_suppression"] = nlohmann::json::array(
        {{{"anchor", "band_start"}, {"offset_hz", -5.0e6}, {"min_db", 60.0}, {"spacing", 1.0}}});

    const auto result = fixture.call("requirements_edit",
                                     {{"epoch", fixture.api.epoch()}, {"requirements", payload}});
    const auto &error = errorFor(result);
    CHECK(error.at("code") == "INVALID_ARGUMENT");
    CHECK(error.at("details").at("path") ==
          "/requirements/modes/0/config/oob_suppression/0/spacing");
    CHECK(*fixture.host.requirements == twoModeSet());
    CHECK(fixture.commands.revision() == before);
    CHECK(fixture.host.checkpoint_begins == 0);
}

TEST_CASE("requirements_edit refuses an out-of-band point inside the band",
          "[agent_requirements]") {
    ApiFixture fixture;
    fixture.host.requirements = twoModeSet();
    auto payload = editPayload(twoModeSet());
    payload["modes"][0]["config"]["oob_suppression"] = nlohmann::json::array(
        {{{"anchor", "band_start"}, {"offset_hz", 10.0e6}, {"min_db", 60.0}}});

    const auto result = fixture.call("requirements_edit",
                                     {{"epoch", fixture.api.epoch()}, {"requirements", payload}});
    const auto &error = errorFor(result);
    CHECK(error.at("code") == "INVALID_ARGUMENT");
    CHECK(error.at("details").at("path") == "/requirements");
    CHECK(error.at("message") ==
          "Mode 1: Out-of-band suppression frequency must lie outside the requirement band.");
    CHECK(fixture.host.checkpoint_begins == 0);
}
