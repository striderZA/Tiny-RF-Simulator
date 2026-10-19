#include "agent_api_test_support.h"
#include "attenuator_engine.h"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

using Catch::Approx;
using OrderedJson = nlohmann::ordered_json;

TEST_CASE("library_create persists a definition for search and circuit placement",
          "[agent_library_create]") {
    ApiFixture fixture;
    const std::uint64_t initial_revision = fixture.commands.revision();
    const std::uint64_t initial_epoch = fixture.api.epoch();

    const AgentToolResult created = fixture.call(
        "library_create",
        {{"type", "attenuator"},
         {"part_number", "MCP-ATT-10"},
         {"manufacturer", "MCP Test"},
         {"description", "MCP-created attenuator"},
         {"parameters", {{"attenuation_dB", 10.0}}}});

    REQUIRE_FALSE(created.is_error);
    CHECK(created.structured.at("epoch") == initial_epoch);
    CHECK(fixture.api.epoch() == initial_epoch);
    CHECK(created.structured.at("scope") == "project");
    CHECK(created.structured.at("part_number") == "MCP-ATT-10");
    CHECK(created.structured.at("type") == "attenuator");
    CHECK(created.structured.at("library_path") ==
          "attenuator/.mcp/part-mcp-att-10.json");
    const fs::path project_root(fixture.host.project_library_root);
    const fs::path persisted =
        project_root / created.structured.at("library_path").get<std::string>();
    REQUIRE(fs::is_regular_file(persisted));
    std::ifstream saved_file(persisted, std::ios::binary);
    REQUIRE(saved_file.good());
    nlohmann::json saved_definition;
    saved_file >> saved_definition;
    CHECK(saved_definition.at("part_number") == "MCP-ATT-10");
    CHECK(saved_definition.at("parameters") == nlohmann::json{{"attenuation_dB", 10.0}});

    ComponentLibrary reloaded;
    reloaded.scan(project_root.string());
    REQUIRE(reloaded.all().size() == 1);
    CHECK(reloaded.all()[0]->part_number == "MCP-ATT-10");
    CHECK(reloaded.all()[0]->parameters.at("attenuation_dB") == 10.0);
    CHECK(fixture.commands.revision() == initial_revision);
    CHECK_FALSE(fixture.commands.isDirty());
    CHECK(fixture.host.checkpoint_begins == 0);

    const AgentToolResult search = fixture.call("library_search", {{"query", "MCP-ATT-10"}});
    REQUIRE_FALSE(search.is_error);
    REQUIRE(search.structured.at("total") == 1);
    REQUIRE(search.structured.at("parts").size() == 1);
    CHECK(search.structured.at("parts")[0].at("parameters") ==
          nlohmann::json{{"attenuation_dB", 10.0}});

    const AgentToolResult placed = editCall(
        fixture,
        OrderedJson::array({{{"op", "add"},
                             {"library_part", {{"part_number", "MCP-ATT-10"},
                                                {"type", "attenuator"}}}}}));
    REQUIRE_FALSE(placed.is_error);
    const auto attenuators = fixture.runtime.components().byType<AttenuatorEngine>();
    REQUIRE(attenuators.size() == 1);
    CHECK(attenuators[0]->attenuation() == Approx(10.0));
}

TEST_CASE("component_types exposes library authoring fields separately from runtime state",
          "[agent_library_create]") {
    ApiFixture fixture;
    const AgentToolResult result = fixture.call("component_types", {{"type", "attenuator"}});

    REQUIRE_FALSE(result.is_error);
    REQUIRE(result.structured.at("types").size() == 1);
    const auto &attenuator = result.structured.at("types")[0];
    REQUIRE(attenuator.contains("authorable"));
    REQUIRE(attenuator.contains("library_fields"));
    CHECK(attenuator.at("authorable") == true);
    REQUIRE(attenuator.at("library_fields").size() == 1);
    CHECK(attenuator.at("library_fields")[0].at("key") == "attenuation_dB");
    CHECK(attenuator.at("library_fields")[0].at("kind") == "number");
    CHECK(attenuator.at("library_fields")[0].at("required") == true);
    CHECK(attenuator.at("library_fields")[0].at("minimum") == 0.0);
    CHECK(attenuator.at("library_fields")[0].at("maximum") == 100.0);
    CHECK(attenuator.at("param_info")[0].at("path") == "atten_dB");
}

TEST_CASE("library_create rejects invalid definitions without publishing an entry",
          "[agent_library_create]") {
    ApiFixture fixture;
    const fs::path library_root(fixture.host.project_library_root);
    const auto reject = [&](OrderedJson arguments, std::string_view expected_path) {
        const AgentToolResult result = fixture.call("library_create", std::move(arguments));
        REQUIRE(result.is_error);
        CHECK(errorFor(result).at("code") == "INVALID_ARGUMENT");
        CHECK(errorFor(result).at("details").at("path") == expected_path);
        CHECK_FALSE(fs::exists(library_root));
    };

    reject({{"type", "attenuator"},
            {"part_number", "BAD-RANGE"},
            {"parameters", {{"attenuation_dB", 101.0}}}},
           "/parameters/attenuation_dB");
    reject({{"type", "attenuator"},
            {"part_number", "BAD-STATE-KEY"},
            {"parameters", {{"atten_dB", 10.0}}}},
           "/parameters/atten_dB");
    reject({{"type", "attenuator"},
            {"part_number", "BAD-FILE-PATH"},
            {"parameters", {{"attenuation_dB", 10.0}, {"sparam_filepath", "/tmp/input.s2p"}}}},
           "/parameters/sparam_filepath");
    reject({{"type", "generator"}, {"part_number", "NOT-AUTHORABLE"}, {"parameters", {}}},
           "/type");
    reject({{"type", "attenuator"}, {"part_number", "BAD-SHAPE"}, {"parameters", "10"}},
           "/parameters");
    reject({{"type", "attenuator"},
            {"part_number", "BAD-FILE-LIST"},
            {"parameters", {{"attenuation_dB", 10.0}}},
            {"data_files", nlohmann::ordered_json::array()}},
           "/data_files");
}

TEST_CASE("library_create rejects duplicates without replacing the original entry",
          "[agent_library_create]") {
    ApiFixture fixture;
    const auto first = fixture.call(
        "library_create",
        {{"type", "attenuator"},
         {"part_number", "MCP-DUP-ATT"},
         {"manufacturer", "First Vendor"},
         {"parameters", {{"attenuation_dB", 8.0}}}});
    REQUIRE_FALSE(first.is_error);

    const fs::path first_file =
        fs::path(fixture.host.project_library_root) /
        first.structured.at("library_path").get<std::string>();
    std::ifstream original_input(first_file, std::ios::binary);
    REQUIRE(original_input.good());
    const std::string original((std::istreambuf_iterator<char>(original_input)),
                               std::istreambuf_iterator<char>());

    const auto duplicate = fixture.call(
        "library_create",
        {{"type", "attenuator"},
         {"part_number", "mcp-dup-att"},
         {"manufacturer", "Second Vendor"},
         {"parameters", {{"attenuation_dB", 18.0}}}});
    REQUIRE(duplicate.is_error);
    CHECK(errorFor(duplicate).at("code") == "INVALID_ARGUMENT");
    CHECK(errorFor(duplicate).at("details").at("path") == "/part_number");

    std::ifstream after_input(first_file, std::ios::binary);
    REQUIRE(after_input.good());
    const std::string after((std::istreambuf_iterator<char>(after_input)),
                            std::istreambuf_iterator<char>());
    CHECK(after == original);
    const auto search = fixture.call("library_search", {{"query", "MCP-DUP-ATT"}});
    REQUIRE_FALSE(search.is_error);
    CHECK(search.structured.at("total") == 1);
}

TEST_CASE("library_create rejects cross-view duplicates but preserves distinct plus-suffixed parts",
          "[agent_library_create]") {
    ApiFixture fixture;
    ComponentLibrary first_view;
    ComponentLibrary second_view;
    const fs::path root(fixture.host.project_library_root);

    ComponentDefinition first;
    first.type = "attenuator";
    first.part_number = "MCP-CROSS-VIEW";
    first.manufacturer = "First Vendor";
    first.parameters = {{"attenuation_dB", 8.0}};
    const auto created = first_view.createUserEntry(std::move(first), root);
    REQUIRE(created.status == ComponentLibraryCreateStatus::Created);

    ComponentDefinition second;
    second.type = "attenuator";
    second.part_number = "mcp-cross-view";
    second.manufacturer = "Second Vendor";
    second.parameters = {{"attenuation_dB", 18.0}};
    const auto duplicate = second_view.createUserEntry(std::move(second), root);
    CHECK(duplicate.status == ComponentLibraryCreateStatus::AlreadyExists);

    ComponentDefinition distinct;
    distinct.type = "attenuator";
    distinct.part_number = "mcp-cross-view+";
    distinct.manufacturer = "Second Vendor";
    distinct.parameters = {{"attenuation_dB", 28.0}};
    const auto other = second_view.createUserEntry(std::move(distinct), root);
    REQUIRE(other.status == ComponentLibraryCreateStatus::Created);

    ComponentLibrary reloaded;
    reloaded.scan(root.string());
    const auto parts = reloaded.all();
    REQUIRE(parts.size() == 2);
    CHECK(std::any_of(parts.begin(), parts.end(), [](const auto *definition) {
        return definition->part_number == "MCP-CROSS-VIEW";
    }));
    CHECK(std::any_of(parts.begin(), parts.end(), [](const auto *definition) {
        return definition->part_number == "mcp-cross-view+";
    }));
}

TEST_CASE("library_create persists entries in the selected Global user library",
          "[agent_library_create]") {
    ApiFixture fixture;
    const auto created = fixture.call(
        "library_create",
        {{"type", "attenuator"},
         {"part_number", "GLOBAL-ATT"},
         {"manufacturer", "Global Vendor"},
         {"parameters", {{"attenuation_dB", 4.0}}},
         {"scope", "global"}});

    REQUIRE_FALSE(created.is_error);
    CHECK(created.structured.at("scope") == "global");
    const fs::path global_root(fixture.host.global_library_root);
    const fs::path persisted =
        global_root / created.structured.at("library_path").get<std::string>();
    REQUIRE(fs::is_regular_file(persisted));

    ComponentLibrary reloaded;
    reloaded.scan(global_root.string());
    REQUIRE(reloaded.all().size() == 1);
    CHECK(reloaded.all()[0]->part_number == "GLOBAL-ATT");
}

TEST_CASE("library_create leaves an existing non-directory destination unchanged",
          "[agent_library_create]") {
    ApiFixture fixture;
    const fs::path blocked_root =
        fs::temp_directory_path() / ("agent_library_blocked_" + test_temp_paths::processTag());
    const std::string original = "not a directory";
    {
        std::ofstream blocked_file(blocked_root, std::ios::binary | std::ios::trunc);
        REQUIRE(blocked_file.good());
        blocked_file << original;
    }
    fixture.host.project_library_root = blocked_root.string();

    const auto result = fixture.call(
        "library_create",
        {{"type", "attenuator"},
         {"part_number", "IO-FAIL-ATT"},
         {"parameters", {{"attenuation_dB", 4.0}}}});
    REQUIRE(result.is_error);
    CHECK(errorFor(result).at("code") == "INTERNAL");

    std::ifstream blocked_file(blocked_root, std::ios::binary);
    REQUIRE(blocked_file.good());
    const std::string after((std::istreambuf_iterator<char>(blocked_file)),
                            std::istreambuf_iterator<char>());
    CHECK(after == original);
    const auto search = fixture.call("library_search", {{"query", "IO-FAIL-ATT"}});
    REQUIRE_FALSE(search.is_error);
    CHECK(search.structured.at("total") == 0);
}

TEST_CASE("library_create checks symlink ancestors before creating library directories",
          "[agent_library_create]") {
    ApiFixture fixture;
    const fs::path base =
        fs::path(fixture.host.project_library_root).parent_path().parent_path();
    const fs::path target = base / "outside";
    const fs::path link = base / "project";
    fs::create_directories(target / "existing");
    std::error_code link_error;
    fs::create_directory_symlink(target, link, link_error);
    if (link_error)
        SKIP("directory symlinks are unavailable: " + link_error.message());
    fixture.host.project_library_root =
        (link / "existing" / "nested" / "rf-sim-libraries").string();

    const auto result = fixture.call(
        "library_create",
        {{"type", "attenuator"},
         {"part_number", "SYMLINK-PARENT"},
         {"parameters", {{"attenuation_dB", 4.0}}}});
    REQUIRE(result.is_error);
    CHECK(errorFor(result).at("code") == "INTERNAL");
    CHECK_FALSE(fs::exists(target / "existing" / "nested"));
}

TEST_CASE("library_create rejects parent traversal before normalizing its library root",
          "[agent_library_create]") {
    ApiFixture fixture;
    const fs::path base =
        fs::path(fixture.host.project_library_root).parent_path().parent_path();
    fs::create_directories(base / "existing");

    ComponentDefinition definition;
    definition.type = "attenuator";
    definition.part_number = "PARENT-TRAVERSAL";
    definition.parameters = {{"attenuation_dB", 4.0}};
    ComponentLibrary library;
    const fs::path traversal_root = base / "existing" / ".." / "rf-sim-libraries";
    const auto result = library.createUserEntry(std::move(definition), traversal_root);

    CHECK(result.status == ComponentLibraryCreateStatus::IoError);
    CHECK_FALSE(fs::exists(base / "rf-sim-libraries"));
}
