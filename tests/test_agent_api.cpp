#if __has_include("imgui.h") || __has_include("imnodes.h")
#error "agent_api tests must remain independent of the UI"
#endif

#include "agent_api.h"
#include "agent_errors.h"
#include "amplifier_engine.h"
#include "component_engine_base.h"
#include "component_library.h"
#include "component_type_registry.h"
#include "editor_commands.h"
#include "graph_editor_actions.h"
#include "logging_core.h"
#include "network_analyzer_engine.h"
#include "rf_switch_engine.h"
#include "signal_generator_engine.h"
#include "spectrum_analyzer_engine.h"
#include "splitter_engine.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

class TestChainScratch final : public IMeasurementChainScratch {
  public:
    IComponentEngine *createClone(std::string_view type, int id) override {
        const auto *descriptor = ComponentTypeRegistry::instance().find(type);
        return descriptor ? descriptor->create(m_components, m_graph, id) : nullptr;
    }

  private:
    NodeGraphEngine m_graph;
    ViewManager m_view;
    ComponentRegistry m_components{m_graph, m_view};
};

class TestChainHost final : public IMeasurementChainHost {
  public:
    explicit TestChainHost(const CircuitRuntime &runtime) : m_runtime(runtime) {}

    IComponentEngine *componentForNode(int graph_node_id) const override {
        return const_cast<IComponentEngine *>(m_runtime.components().find(graph_node_id));
    }

    std::unique_ptr<IMeasurementChainScratch> beginScratchPass() const override {
        return std::make_unique<TestChainScratch>();
    }

  private:
    const CircuitRuntime &m_runtime;
};

class ThrowingHoverEngine final : public ComponentEngineBase {
  public:
    ThrowingHoverEngine(int id, NodeGraphEngine &graph)
        : ComponentEngineBase(id, graph, "Throwing", 1, 1) {}
    std::string_view type_name() const override { return "agent_api_thrower"; }
    std::string hoverSummary() const override { throw std::runtime_error("hover failed"); }
    void update(double) override {}
};

class FakeAgentHost final : public IAgentHost {
  public:
    void beginCheckpoint(const AgentCall &) override { ++checkpoint_begins; }
    void commitCheckpoint(const std::string &) override { ++checkpoint_commits; }
    void discardCheckpoint() override { ++checkpoint_discards; }
    void placeComponents(const std::vector<AgentPlacement> &placements) override {
        last_placements = placements;
    }
    bool appModalOpen() const override { return modal_open; }
    std::optional<std::string> projectName() const override { return name; }
    void recordActivity(const AgentActivity &entry) override {
        if (throw_on_activity)
            throw std::runtime_error("activity sink failed");
        activities.push_back(entry);
    }

    bool modal_open = false;
    bool throw_on_activity = false;
    std::optional<std::string> name;
    int checkpoint_begins = 0;
    int checkpoint_commits = 0;
    int checkpoint_discards = 0;
    std::vector<AgentPlacement> last_placements;
    std::vector<AgentActivity> activities;
};

struct ApiFixture {
    CircuitRuntime runtime;
    GraphEditorActions actions{runtime};
    EditorCommands commands{runtime, actions};
    ComponentLibrary library;
    SpectrumAnalyzerEngine spectrum_analyzer;
    TestChainHost chain_host{runtime};
    NetworkAnalyzerEngine network_analyzer{runtime.graph(), chain_host};
    FakeAgentHost host;
    AgentApi api{
        {commands, runtime, library, network_analyzer, chain_host, spectrum_analyzer, host}};

    ApiFixture() { library.scan(std::string(PROJECT_SOURCE_DIR) + "/component_data/library"); }

    template <typename Engine> Engine *add(std::string_view type) {
        const auto *descriptor = ComponentTypeRegistry::instance().find(type);
        REQUIRE(descriptor != nullptr);
        return static_cast<Engine *>(commands.createComponent(descriptor->create));
    }

    ThrowingHoverEngine *addThrower() {
        return static_cast<ThrowingHoverEngine *>(
            commands.createComponent([](ComponentRegistry &components, NodeGraphEngine &graph,
                                        int id) -> IComponentEngine * {
                return &components.add<ThrowingHoverEngine>(id, graph);
            }));
    }

    bool errorWasLogged(std::string_view tool) const {
        return std::any_of(LoggerCore::instance().entries().begin(),
                           LoggerCore::instance().entries().end(), [&](const LogEntry &entry) {
                               return entry.level == Level::Error &&
                                      entry.message.find("Agent tool " + std::string(tool) +
                                                         " failed:") != std::string::npos;
                           });
    }

    AgentToolResult call(std::string tool,
                         nlohmann::ordered_json arguments = nlohmann::ordered_json::object()) {
        return api.execute({std::move(tool), std::move(arguments), "test-client"});
    }
};

const nlohmann::json &errorFor(const AgentToolResult &result) {
    REQUIRE(result.is_error);
    REQUIRE(result.structured.contains("error"));
    return result.structured.at("error");
}

} // namespace

TEST_CASE("circuit_get reports components, ports, links, probes and analyzer points",
          "[agent_api]") {
    ApiFixture fixture;
    auto *generator = fixture.add<SignalGeneratorEngine>("generator");
    auto *splitter = fixture.add<SplitterEngine>("splitter");
    auto *sw = fixture.add<RFSwitchEngine>("rf_switch_spdt");
    REQUIRE(generator != nullptr);
    REQUIRE(splitter != nullptr);
    REQUIRE(sw != nullptr);

    REQUIRE(fixture.commands.connect(generator->outputPinId(), splitter->inputPinId()).has_value());
    REQUIRE(fixture.commands.connect(splitter->outputPinId(1), sw->inputPinId()).has_value());
    REQUIRE(fixture.commands.addProbePin(generator->outputPinId()));
    REQUIRE(fixture.commands.addProbePin(splitter->outputPinId(1)));
    fixture.network_analyzer.setPointA(generator->outputPinId());
    fixture.network_analyzer.setPointB(sw->outputPinId(1));
    fixture.network_analyzer.setStartFrequency(100e6);
    fixture.network_analyzer.setStopFrequency(200e6);
    fixture.network_analyzer.setPoints(11);
    fixture.network_analyzer.setStimulusPower(-18.0);

    const AgentToolResult result = fixture.call("circuit_get", {{"include_params", true}});
    REQUIRE_FALSE(result.is_error);
    const auto &data = result.structured;
    CHECK(data.at("epoch") == fixture.runtime.epoch());
    CHECK(data.at("revision") == fixture.commands.revision());
    CHECK(data.at("dirty") == fixture.commands.isDirty());
    CHECK(data.at("project_name").is_null());
    REQUIRE(data.at("components").size() == 3);
    CHECK(data.at("components")[0].at("label") == "Generator 100");
    CHECK(data.at("components")[0].contains("params"));

    const auto &splitter_json = data.at("components")[1];
    REQUIRE(splitter_json.at("outputs").size() == 2);
    CHECK(splitter_json.at("outputs")[0].at("label") == "OUT");
    CHECK(splitter_json.at("outputs")[1].at("label") == "OUT2");
    REQUIRE(splitter_json.at("inputs").size() == 1);
    CHECK(splitter_json.at("inputs")[0].at("label") == "IN");
    CHECK(splitter_json.at("inputs")[0].at("source") ==
          nlohmann::json{{"component", generator->id()}, {"port", 0}});
    CHECK(splitter_json.at("outputs")[1].at("destinations")[0] ==
          nlohmann::json{{"component", sw->id()}, {"port", 0}});
    const auto &switch_json = data.at("components")[2];
    REQUIRE(switch_json.at("outputs").size() == 2);
    CHECK(switch_json.at("inputs")[0].at("label") == "COM");
    CHECK(switch_json.at("outputs")[0].at("label") == "T1");
    CHECK(switch_json.at("outputs")[1].at("label") == "T2");
    REQUIRE(data.at("probes").size() == 2);
    CHECK(data.at("probes")[0].at("component") == generator->id());
    CHECK(data.at("probes")[0].at("port") == 0);
    CHECK(data.at("probes")[1].at("component") == splitter->id());
    CHECK(data.at("probes")[1].at("port") == 1);
    CHECK(data.at("network_analyzer").at("point_a") ==
          nlohmann::json{{"component", generator->id()}, {"port", 0}});
    CHECK(data.at("network_analyzer").at("point_b") ==
          nlohmann::json{{"component", sw->id()}, {"port", 1}});
    CHECK(data.at("network_analyzer").at("start_Hz") == 100e6);
    CHECK(data.at("network_analyzer").at("stop_Hz") == 200e6);
    CHECK(data.at("network_analyzer").at("points") == 11);
    CHECK(data.at("network_analyzer").at("stimulus_dBm") == -18.0);
    const AgentToolResult component = fixture.call(
        "component_get", {{"epoch", fixture.api.epoch()}, {"component", generator->id()}});
    REQUIRE_FALSE(component.is_error);
    CHECK(component.structured.at("epoch") == fixture.runtime.epoch());
    CHECK(component.structured.at("id") == generator->id());
    CHECK(component.structured.at("type") == "generator");
    CHECK(component.structured.at("params") == generator->serialize());
    const auto &param_info = component.structured.at("param_info");
    const auto frequency_info =
        std::find_if(param_info.begin(), param_info.end(),
                     [](const auto &info) { return info.at("path") == "tones[].freq_Hz"; });
    REQUIRE(frequency_info != param_info.end());
    CHECK(frequency_info->at("unit") == "Hz");
    CHECK(component.structured.at("summary") ==
          fixture.runtime.components().hoverSummary(generator->graphNodeId()));

    const AgentToolResult no_params = fixture.call("circuit_get");
    REQUIRE_FALSE(no_params.is_error);
    CHECK_FALSE(no_params.structured.at("components")[0].contains("params"));
}

TEST_CASE("Calls that take ids require the current epoch", "[agent_api]") {
    ApiFixture fixture;
    auto *generator = fixture.add<SignalGeneratorEngine>("generator");
    REQUIRE(generator != nullptr);
    auto stale = fixture.call("component_get", {{"epoch", 99}, {"component", generator->id()}});
    const auto &stale_error = errorFor(stale);
    CHECK(stale_error.at("code") == "STALE_EPOCH");
    CHECK(stale_error.at("details").at("epoch") == fixture.runtime.epoch());
    CHECK(stale_error.at("message") == "epoch 99 is stale; call circuit_get");

    fixture.runtime.clearComponentsAndResetIds();
    fixture.api.noteProjectReplaced(AgentReplacementCause::OpenedProject);
    auto opened = fixture.call("component_get", {{"epoch", 0}, {"component", 100}});
    CHECK(errorFor(opened).at("details").at("cause") == "opened_project");
    CHECK(errorFor(opened).at("message") == "epoch 0 is stale; opened_project; call circuit_get");

    fixture.runtime.clearComponentsAndResetIds();
    fixture.api.noteProjectReplaced(AgentReplacementCause::Reverted, {"add generator", "wire"});
    auto reverted = fixture.call("component_get", {{"epoch", 1}, {"component", 100}});
    CHECK(errorFor(reverted).at("details").at("cause") == "reverted");
    CHECK(errorFor(reverted).at("details").at("undone") ==
          nlohmann::json::array({"add generator", "wire"}));
}

TEST_CASE("A removed component is NOT_FOUND, never another part", "[agent_api]") {
    ApiFixture fixture;
    auto *first = fixture.add<SignalGeneratorEngine>("generator");
    auto *second = fixture.add<AmplifierEngine>("amplifier");
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    const int removed_id = first->id();
    const auto epoch = fixture.api.epoch();
    REQUIRE(fixture.commands.removeComponent(first->graphNodeId()));

    const auto result =
        fixture.call("component_get", {{"epoch", epoch}, {"component", removed_id}});
    const auto &error = errorFor(result);
    CHECK(error.at("code") == "NOT_FOUND");
    CHECK(error.at("message") == "component " + std::to_string(removed_id) + " is not in epoch " +
                                     std::to_string(epoch) + "; call circuit_get");
    CHECK(fixture.runtime.components().find(second->graphNodeId()) == second);
}

TEST_CASE("Arguments are validated at the boundary", "[agent_api]") {
    ApiFixture fixture;
    const auto wrong_type = fixture.call("component_get", {{"epoch", 0}, {"component", "1"}});
    CHECK(errorFor(wrong_type).at("code") == "INVALID_ARGUMENT");
    CHECK(errorFor(wrong_type).at("details").at("path") == "/component");

    const auto unknown =
        fixture.call("component_get", {{"epoch", 0}, {"component", 1}, {"bogus", true}});
    CHECK(errorFor(unknown).at("code") == "INVALID_ARGUMENT");
    CHECK(errorFor(unknown).at("details").at("path") == "/bogus");

    const auto missing = fixture.call("component_get", {{"component", 1}});
    CHECK(errorFor(missing).at("code") == "INVALID_ARGUMENT");
    CHECK(errorFor(missing).at("details").at("path") == "/epoch");
}

TEST_CASE("Unexpected exceptions become INTERNAL and are logged", "[agent_api]") {
    ApiFixture fixture;
    LoggerCore::instance().clear();
    auto *throwing = fixture.addThrower();
    REQUIRE(throwing != nullptr);
    const auto failed = fixture.call(
        "component_get", {{"epoch", fixture.api.epoch()}, {"component", throwing->id()}});
    CHECK(errorFor(failed).at("code") == "INTERNAL");
    CHECK(errorFor(failed).at("message") ==
          "internal error in component_get; see the RF Simulator log");
    CHECK(fixture.errorWasLogged("component_get"));

    const auto next = fixture.call("circuit_get");
    CHECK_FALSE(next.is_error);
}

TEST_CASE("Activity callback exceptions become INTERNAL and are logged", "[agent_api]") {
    ApiFixture fixture;
    LoggerCore::instance().clear();
    fixture.host.throw_on_activity = true;

    std::optional<AgentToolResult> result;
    bool threw = false;
    try {
        result = fixture.call("circuit_get");
    } catch (...) {
        threw = true;
    }
    CHECK_FALSE(threw);
    if (result) {
        CHECK(errorFor(*result).at("code") == "INTERNAL");
        CHECK(errorFor(*result).at("message") ==
              "internal error in circuit_get; see the RF Simulator log");
        CHECK(fixture.errorWasLogged("circuit_get"));
    }
}

TEST_CASE("Every result carries the epoch and records activity", "[agent_api]") {
    ApiFixture fixture;
    const auto epoch = fixture.api.epoch();
    const auto result = fixture.call("circuit_get");
    REQUIRE_FALSE(result.is_error);
    CHECK(result.structured.at("epoch") == epoch);
    REQUIRE(fixture.host.activities.size() == 1);
    CHECK(fixture.host.activities[0].tool == "circuit_get");
    CHECK(fixture.host.activities[0].client == "test-client");
    CHECK(fixture.host.activities[0].ok);
    CHECK(fixture.host.activities[0].error_code.empty());
    CHECK(fixture.host.activities[0].duration_ms >= 0.0);

    const auto bad = fixture.call("component_get", {{"epoch", epoch}, {"component", 999}});
    CHECK(errorFor(bad).at("code") == "NOT_FOUND");
    CHECK(bad.structured.at("epoch") == epoch);
    REQUIRE(fixture.host.activities.size() == 2);
    CHECK_FALSE(fixture.host.activities[1].ok);
    CHECK(fixture.host.activities[1].error_code == "NOT_FOUND");
}

TEST_CASE("Unknown tools are rejected", "[agent_api]") {
    ApiFixture fixture;
    const auto result = fixture.call("future_tool");
    const auto &error = errorFor(result);
    CHECK(error.at("code") == "INVALID_ARGUMENT");
    CHECK(error.at("message") == "unknown tool future_tool");
    REQUIRE(fixture.host.activities.size() == 1);
    CHECK_FALSE(fixture.host.activities[0].ok);
    CHECK(fixture.host.activities[0].error_code == "INVALID_ARGUMENT");
}
