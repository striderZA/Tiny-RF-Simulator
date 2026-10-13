#if __has_include("imgui.h") || __has_include("imnodes.h")
#error "agent_api tests must remain independent of the UI"
#endif

// Circuit-edit move and editor-space position contracts, split out of test_agent_api.cpp.
// On the MinGW-w64 build the cases added to that file were compiled but never registered
// with Catch2 (exact-name runs reported no match), so they run from their own executable.
#include "agent_api_test_support.h"

TEST_CASE("circuit_get reports editor-space component positions", "[agent_api]") {
    ApiFixture fixture;
    auto *generator = fixture.add<SignalGeneratorEngine>("generator");
    REQUIRE(generator != nullptr);
    fixture.host.node_positions[generator->graphNodeId()] = {12.5f, -7.25f};

    const AgentToolResult result = fixture.call("circuit_get");
    REQUIRE_FALSE(result.is_error);
    const auto &component = result.structured.at("components").at(0);
    REQUIRE(component.contains("position"));
    CHECK(component.at("position") == nlohmann::json{{"x", 12.5}, {"y", -7.25}});
}

TEST_CASE("circuit_edit moves a component and marks the project modified",
          "[agent_api][circuit_edit]") {
    ApiFixture fixture;
    auto *generator = fixture.add<SignalGeneratorEngine>("generator");
    REQUIRE(generator != nullptr);
    fixture.host.node_positions[generator->graphNodeId()] = {10.0f, 20.0f};
    const std::uint64_t revision = fixture.commands.revision();

    const AgentToolResult result = editCall(
        fixture, nlohmann::ordered_json::array({{{"op", "move"},
                                                 {"component", generator->id()},
                                                 {"position", {{"x", 30.0}, {"y", 40.0}}}}}));

    REQUIRE_FALSE(result.is_error);
    CHECK(result.structured.at("revision") == revision + 1);
    CHECK(fixture.host.checkpoint_begins == 1);
    CHECK(fixture.host.checkpoint_commits == 1);
    CHECK(fixture.host.last_checkpoint_summary == "1 component moved");
    const AgentToolResult read = fixture.call("circuit_get");
    REQUIRE_FALSE(read.is_error);
    CHECK(read.structured.at("components").at(0).at("position") ==
          nlohmann::json{{"x", 30.0}, {"y", 40.0}});
}

TEST_CASE("circuit_edit does not create a checkpoint for an unchanged move",
          "[agent_api][circuit_edit]") {
    ApiFixture fixture;
    auto *generator = fixture.add<SignalGeneratorEngine>("generator");
    REQUIRE(generator != nullptr);
    fixture.host.node_positions[generator->graphNodeId()] = {10.0f, 20.0f};
    const std::uint64_t revision = fixture.commands.revision();

    const AgentToolResult result = editCall(
        fixture, nlohmann::ordered_json::array({{{"op", "move"},
                                                 {"component", generator->id()},
                                                 {"position", {{"x", 10.0}, {"y", 20.0}}}}}));

    REQUIRE_FALSE(result.is_error);
    CHECK(result.structured.at("revision") == revision);
    REQUIRE(result.structured.at("applied").size() == 1);
    CHECK(result.structured.at("applied").at(0).at("unchanged") == true);
    CHECK(fixture.host.checkpoint_begins == 1);
    CHECK(fixture.host.checkpoint_commits == 0);
    CHECK(fixture.host.checkpoint_discards == 1);
}

TEST_CASE("circuit_edit discards a checkpoint for unchanged set_params",
          "[agent_api][circuit_edit]") {
    ApiFixture fixture;
    auto *generator = fixture.add<SignalGeneratorEngine>("generator");
    REQUIRE(generator != nullptr);
    const auto params = nlohmann::ordered_json::parse(generator->serialize().dump());
    const std::uint64_t revision = fixture.commands.revision();

    const AgentToolResult result = editCall(
        fixture, nlohmann::ordered_json::array(
                     {{{"op", "set_params"}, {"component", generator->id()}, {"params", params}}}));

    REQUIRE_FALSE(result.is_error);
    REQUIRE(result.structured.at("applied").size() == 1);
    CHECK(result.structured.at("applied").at(0).at("unchanged") == true);
    CHECK(fixture.commands.revision() == revision);
    CHECK(fixture.host.checkpoint_begins == 1);
    CHECK(fixture.host.checkpoint_commits == 0);
    CHECK(fixture.host.checkpoint_discards == 1);
}

TEST_CASE("circuit_edit rejects a move to an unknown component without changing state",
          "[agent_api][circuit_edit]") {
    ApiFixture fixture;
    const std::uint64_t revision = fixture.commands.revision();

    const AgentToolResult result = editCall(
        fixture,
        nlohmann::ordered_json::array(
            {{{"op", "move"}, {"component", 999}, {"position", {{"x", 30.0}, {"y", 40.0}}}}}));

    CHECK(errorFor(result).at("code") == "NOT_FOUND");
    CHECK(fixture.commands.revision() == revision);
    CHECK(fixture.host.checkpoint_discards == 1);
}

TEST_CASE("circuit_edit rejects moving a component added earlier in the same batch",
          "[agent_api][circuit_edit]") {
    ApiFixture fixture;
    const int new_component_id = fixture.runtime.nextComponentId();

    const AgentToolResult result = editCall(
        fixture, nlohmann::ordered_json::array({{{"op", "add"}, {"type", "generator"}},
                                                {{"op", "move"},
                                                 {"component", new_component_id},
                                                 {"position", {{"x", 30.0}, {"y", 40.0}}}}}));

    REQUIRE(result.is_error);
    CHECK(errorFor(result).at("code") == "INVALID_ARGUMENT");
    CHECK(errorFor(result).at("details").at("path") == "/ops/1/component");
    REQUIRE(result.structured.at("applied").size() == 1);
    CHECK(result.structured.at("applied").at(0).at("component") == new_component_id);
    CHECK(fixture.host.checkpoint_commits == 1);
    CHECK(fixture.host.last_placements.size() == 1);
}

TEST_CASE("circuit_edit validates move operation and coordinate shapes",
          "[agent_api][circuit_edit]") {
    ApiFixture fixture;
    auto *generator = fixture.add<SignalGeneratorEngine>("generator");
    REQUIRE(generator != nullptr);
    fixture.host.node_positions[generator->graphNodeId()] = {10.0f, 20.0f};
    const std::uint64_t revision = fixture.commands.revision();
    const std::vector<nlohmann::ordered_json> invalid_moves{
        {{"op", "move"}, {"component", generator->id()}},
        {{"op", "move"},
         {"component", generator->id()},
         {"position", {{"x", 30.0}, {"y", 40.0}, {"z", 50.0}}}},
        {{"op", "move"}, {"component", generator->id()}, {"position", {{"x", "30"}, {"y", 40.0}}}},
        {{"op", "move"},
         {"component", generator->id()},
         {"position", {{"x", std::numeric_limits<double>::max()}, {"y", 40.0}}}},
        {{"op", "move"},
         {"component", generator->id()},
         {"position", {{"x", 30.0}, {"y", 40.0}}},
         {"unexpected", true}}};

    for (const auto &operation : invalid_moves) {
        const AgentToolResult invalid_result =
            editCall(fixture, nlohmann::ordered_json::array({operation}));
        REQUIRE(invalid_result.is_error);
        CHECK(errorFor(invalid_result).at("code") == "INVALID_ARGUMENT");
        CHECK(fixture.commands.revision() == revision);
        CHECK(fixture.host.node_positions.at(generator->graphNodeId()) ==
              std::array<float, 2>{10.0f, 20.0f});
    }
    CHECK(fixture.host.checkpoint_discards == static_cast<int>(invalid_moves.size()));
    CHECK(fixture.host.checkpoint_commits == 0);
}

TEST_CASE("circuit_edit commits a changed move in the applied prefix before a later error",
          "[agent_api][circuit_edit]") {
    ApiFixture fixture;
    auto *generator = fixture.add<SignalGeneratorEngine>("generator");
    REQUIRE(generator != nullptr);
    fixture.host.node_positions[generator->graphNodeId()] = {10.0f, 20.0f};
    const std::uint64_t revision = fixture.commands.revision();

    const AgentToolResult result =
        editCall(fixture, nlohmann::ordered_json::array({{{"op", "move"},
                                                          {"component", generator->id()},
                                                          {"position", {{"x", 30.0}, {"y", 40.0}}}},
                                                         {{"op", "invalid"}}}));

    REQUIRE(result.is_error);
    CHECK(errorFor(result).at("code") == "INVALID_ARGUMENT");
    REQUIRE(result.structured.at("applied").size() == 1);
    CHECK(result.structured.at("applied").at(0).at("op") == "move");
    CHECK(fixture.host.node_positions.at(generator->graphNodeId()) ==
          std::array<float, 2>{30.0f, 40.0f});
    CHECK(fixture.commands.revision() == revision + 1);
    CHECK(fixture.host.checkpoint_commits == 1);
    CHECK(fixture.host.last_checkpoint_summary == "1 component moved");
}

TEST_CASE("circuit_edit rejects non-finite move coordinates without changing state",
          "[agent_api][circuit_edit]") {
    ApiFixture fixture;
    auto *generator = fixture.add<SignalGeneratorEngine>("generator");
    REQUIRE(generator != nullptr);
    fixture.host.node_positions[generator->graphNodeId()] = {10.0f, 20.0f};
    const std::uint64_t revision = fixture.commands.revision();

    const AgentToolResult result = editCall(
        fixture,
        nlohmann::ordered_json::array(
            {{{"op", "move"},
              {"component", generator->id()},
              {"position", {{"x", std::numeric_limits<double>::infinity()}, {"y", 40.0}}}}}));

    CHECK(errorFor(result).at("code") == "INVALID_ARGUMENT");
    CHECK(errorFor(result).at("details").at("path") == "/ops/0/position");
    CHECK(fixture.commands.revision() == revision);
    CHECK(fixture.host.node_positions.at(generator->graphNodeId()) ==
          std::array<float, 2>{10.0f, 20.0f});
    CHECK(fixture.host.checkpoint_discards == 1);
}

TEST_CASE("circuit_edit rejects coordinates outside the float range without converting them",
          "[agent_api][circuit_edit]") {
    ApiFixture fixture;
    auto *generator = fixture.add<SignalGeneratorEngine>("generator");
    REQUIRE(generator != nullptr);
    fixture.host.node_positions[generator->graphNodeId()] = {10.0f, 20.0f};
    const std::uint64_t revision = fixture.commands.revision();
    // 1e300 is finite as a double but has no float value. 0x1p128 - 0x1p103 is the midpoint above
    // FLT_MAX and rounds to infinity, so it is rejected too.
    const std::array<double, 4> out_of_range{1e300, -1e300, 0x1p128, 0x1p128 - 0x1p103};
    for (const double x : out_of_range) {
        const AgentToolResult result = editCall(
            fixture, nlohmann::ordered_json::array({{{"op", "move"},
                                                     {"component", generator->id()},
                                                     {"position", {{"x", x}, {"y", 40.0}}}}}));
        REQUIRE(result.is_error);
        CHECK(errorFor(result).at("code") == "INVALID_ARGUMENT");
        CHECK(errorFor(result).at("details").at("path") == "/ops/0/position");
        CHECK(fixture.commands.revision() == revision);
        CHECK(fixture.host.node_positions.at(generator->graphNodeId()) ==
              std::array<float, 2>{10.0f, 20.0f});
    }
    CHECK(fixture.host.checkpoint_discards == static_cast<int>(out_of_range.size()));
    CHECK(fixture.host.checkpoint_commits == 0);

    // The largest finite float is still a valid coordinate.
    const double largest = static_cast<double>(std::numeric_limits<float>::max());
    const AgentToolResult accepted = editCall(
        fixture, nlohmann::ordered_json::array({{{"op", "move"},
                                                 {"component", generator->id()},
                                                 {"position", {{"x", largest}, {"y", 40.0}}}}}));
    REQUIRE_FALSE(accepted.is_error);
    CHECK(fixture.host.node_positions.at(generator->graphNodeId())[0] ==
          std::numeric_limits<float>::max());
}
