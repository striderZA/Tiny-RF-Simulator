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
#include "pfb_channelizer_engine.h"
#include "rf_switch_engine.h"
#include "signal_generator_engine.h"
#include "spectrum_analyzer_engine.h"
#include "splitter_engine.h"

#include "component_params.h"
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
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
    void commitCheckpoint(const std::string &summary) override {
        ++checkpoint_commits;
        last_checkpoint_summary = summary;
    }
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
    std::string last_checkpoint_summary;
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

AgentToolResult editCall(ApiFixture &fixture, nlohmann::ordered_json ops) {
    return fixture.call("circuit_edit", {{"epoch", fixture.api.epoch()}, {"ops", std::move(ops)}});
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
TEST_CASE("component_types lists every registered type with its ports", "[agent_api]") {
    ApiFixture fixture;

    const auto result = fixture.call("component_types");
    REQUIRE_FALSE(result.is_error);
    CHECK(result.structured.at("epoch") == fixture.api.epoch());
    const auto &types = result.structured.at("types");
    REQUIRE(types.size() == 13);
    const std::vector<std::string> expected{"amplifier", "attenuator",     "splitter",
                                            "filter",    "mixer",          "equalizer",
                                            "combiner",  "rf_switch_spdt", "rf_switch_spdt_2to1",
                                            "adc",       "generator",      "coax",
                                            "pfb"};
    for (std::size_t i = 0; i < expected.size(); ++i)
        CHECK(types[i].at("type") == expected[i]);

    const auto pfb = std::find_if(types.begin(), types.end(),
                                  [](const auto &type) { return type.at("type") == "pfb"; });
    REQUIRE(pfb != types.end());
    REQUIRE(pfb->at("outputs").size() == 2);
    CHECK(pfb->at("outputs")[0].at("label") == "OUT");
    CHECK(pfb->at("outputs")[1].at("label") == "OUT2");

    const auto sw = std::find_if(types.begin(), types.end(), [](const auto &type) {
        return type.at("type") == "rf_switch_spdt";
    });
    REQUIRE(sw != types.end());
    REQUIRE(sw->at("inputs").size() == 1);
    REQUIRE(sw->at("outputs").size() == 2);
    CHECK(sw->at("inputs")[0].at("label") == "COM");
    CHECK(sw->at("outputs")[0].at("label") == "T1");
    CHECK(sw->at("outputs")[1].at("label") == "T2");
}

TEST_CASE("component_types with a type returns defaults and parameter info", "[agent_api]") {
    ApiFixture fixture;

    const auto amp = fixture.call("component_types", {{"type", "amplifier"}});
    REQUIRE_FALSE(amp.is_error);
    CHECK(amp.structured.at("epoch") == fixture.api.epoch());
    const auto &amplifier = amp.structured.at("types")[0];
    CHECK(amplifier.at("type") == "amplifier");
    CHECK(amplifier.at("default_params").at("gain_dB") == 0.0);
    const auto &amplifier_info = amplifier.at("param_info");
    const auto sparam =
        std::find_if(amplifier_info.begin(), amplifier_info.end(),
                     [](const auto &info) { return info.at("path") == "sparam_filepath"; });
    REQUIRE(sparam != amplifier_info.end());
    CHECK(sparam->at("kind") == "file_path");
    CHECK(sparam->at("read_only") == true);

    const auto filter = fixture.call("component_types", {{"type", "filter"}});
    REQUIRE_FALSE(filter.is_error);
    CHECK(filter.structured.at("epoch") == fixture.api.epoch());
    const auto &filter_info = filter.structured.at("types")[0].at("param_info");
    const auto filter_type =
        std::find_if(filter_info.begin(), filter_info.end(),
                     [](const auto &info) { return info.at("path") == "filter_type"; });
    REQUIRE(filter_type != filter_info.end());
    CHECK(filter_type->at("kind") == "enum");
    CHECK(filter_type->at("enum_labels") == nlohmann::json::array({"LPF", "HPF", "BPF", "BSF"}));

    const auto bool_type = fixture.call("component_types", {{"type", "amplifier"}});
    REQUIRE_FALSE(bool_type.is_error);
    CHECK(bool_type.structured.at("epoch") == fixture.api.epoch());
    const auto &bool_info = bool_type.structured.at("types")[0].at("param_info");
    const auto nonlinear = std::find_if(bool_info.begin(), bool_info.end(), [](const auto &info) {
        return info.at("path") == "enable_nonlinear";
    });
    REQUIRE(nonlinear != bool_info.end());
    CHECK(nonlinear->at("kind") == "boolean");
}

TEST_CASE("component_types rejects an unknown type", "[agent_api]") {
    ApiFixture fixture;

    const auto result = fixture.call("component_types", {{"type", "missing_type"}});
    CHECK(errorFor(result).at("code") == "UNKNOWN_TYPE");
    CHECK(result.structured.at("epoch") == fixture.api.epoch());
    const auto empty_type = fixture.call("component_types", {{"type", ""}});
    CHECK(empty_type.is_error);
    if (empty_type.is_error)
        CHECK(errorFor(empty_type).at("code") == "UNKNOWN_TYPE");
    CHECK(empty_type.structured.at("epoch") == fixture.api.epoch());
}

TEST_CASE("library_search matches case-insensitively and limits results", "[agent_api]") {
    ApiFixture fixture;

    const auto empty_query = fixture.call("library_search");
    REQUIRE_FALSE(empty_query.is_error);
    CHECK(empty_query.structured.at("epoch") == fixture.api.epoch());
    REQUIRE(empty_query.structured.at("parts").size() > 0);
    CHECK(empty_query.structured.at("total") >= empty_query.structured.at("parts").size());
    const auto part_number = fixture.call("library_search", {{"query", "zx60"}});
    REQUIRE_FALSE(part_number.is_error);
    CHECK(part_number.structured.at("epoch") == fixture.api.epoch());
    REQUIRE(part_number.structured.at("parts").size() == 1);
    CHECK(part_number.structured.at("parts")[0].at("part_number") == "ZX60-33LN+");

    const auto manufacturer = fixture.call("library_search", {{"query", "mini-circuits"}});
    REQUIRE_FALSE(manufacturer.is_error);
    CHECK(manufacturer.structured.at("epoch") == fixture.api.epoch());
    const auto mini_circuits = std::find_if(
        manufacturer.structured.at("parts").begin(), manufacturer.structured.at("parts").end(),
        [](const auto &part) { return part.at("manufacturer") == "Mini-Circuits"; });
    CHECK(mini_circuits != manufacturer.structured.at("parts").end());

    const auto description = fixture.call("library_search", {{"query", "low noise"}});
    REQUIRE_FALSE(description.is_error);
    CHECK(description.structured.at("epoch") == fixture.api.epoch());
    const auto low_noise = std::find_if(
        description.structured.at("parts").begin(), description.structured.at("parts").end(),
        [](const auto &part) { return part.at("part_number") == "ZX60-33LN+"; });
    CHECK(low_noise != description.structured.at("parts").end());

    const auto limited = fixture.call("library_search", {{"query", "mini-circuits"}, {"limit", 1}});
    REQUIRE_FALSE(limited.is_error);
    CHECK(limited.structured.at("epoch") == fixture.api.epoch());
    CHECK(limited.structured.at("parts").size() == 1);
    CHECK(limited.structured.at("total") >= 2);
    const auto amplifier_filter =
        fixture.call("library_search", {{"query", "mini-circuits"}, {"type", "amplifier"}});
    REQUIRE_FALSE(amplifier_filter.is_error);
    CHECK(amplifier_filter.structured.at("epoch") == fixture.api.epoch());
    REQUIRE(amplifier_filter.structured.at("parts").size() == 1);
    CHECK(amplifier_filter.structured.at("parts")[0].at("part_number") == "ZX60-33LN+");
    for (const auto &part : amplifier_filter.structured.at("parts"))
        CHECK(part.at("type") == "amplifier");

    const auto ordered = fixture.call("library_search", {{"query", "mini-circuits"}});
    CHECK(ordered.structured.at("epoch") == fixture.api.epoch());
    const auto &ordered_parts = ordered.structured.at("parts");
    REQUIRE(ordered_parts.size() >= 2);
    for (std::size_t i = 1; i < ordered_parts.size(); ++i) {
        const auto previous = std::pair{ordered_parts[i - 1].at("type").get<std::string>(),
                                        ordered_parts[i - 1].at("part_number").get<std::string>()};
        const auto current = std::pair{ordered_parts[i].at("type").get<std::string>(),
                                       ordered_parts[i].at("part_number").get<std::string>()};
        CHECK(previous <= current);
    }

    const auto invalid_limit = fixture.call("library_search", {{"query", "zx60"}, {"limit", 0}});
    CHECK(errorFor(invalid_limit).at("code") == "INVALID_ARGUMENT");
    CHECK(invalid_limit.structured.at("epoch") == fixture.api.epoch());
    CHECK(errorFor(invalid_limit).at("details").at("path") == "/limit");

    const auto data_files = fixture.call("library_search", {{"query", "AM1143"}});
    REQUIRE_FALSE(data_files.is_error);
    CHECK(data_files.structured.at("epoch") == fixture.api.epoch());
    REQUIRE(data_files.structured.at("parts").size() == 1);
    CHECK(data_files.structured.at("parts")[0].at("has_data_files") == true);
}

TEST_CASE("circuit_edit applies a generator-library-filter chain as one checkpoint",
          "[agent_api][circuit_edit]") {
    ApiFixture fixture;
    const auto epoch = fixture.api.epoch();
    const auto result = editCall(
        fixture, nlohmann::ordered_json::array(
                     {{{"op", "add"}, {"ref", "source"}, {"type", "generator"}},
                      {{"op", "add"},
                       {"ref", "gain"},
                       {"library_part", {{"part_number", "ZX60-33LN+"}}},
                       {"position", {{"x", 400.0}, {"y", 25.0}}}},
                      {{"op", "add"},
                       {"ref", "channelizer"},
                       {"type", "filter"},
                       {"params", {{"filter_type", "BPF"}}}},
                      {{"op", "connect"},
                       {"from", {{"ref", "source"}, {"port", 0}}},
                       {"to", {{"ref", "gain"}, {"port", 0}}}},
                      {{"op", "connect"},
                       {"from", {{"ref", "gain"}, {"port", 0}}},
                       {"to", {{"ref", "channelizer"}, {"port", 0}}}},
                      {{"op", "probe_add"}, {"at", {{"ref", "channelizer"}, {"port", 0}}}}}));

    REQUIRE_FALSE(result.is_error);
    CHECK(result.structured.at("epoch") == epoch);
    CHECK(result.structured.at("revision") == fixture.commands.revision());
    REQUIRE(result.structured.at("applied").size() == 6);
    CHECK(result.structured.at("refs").size() == 3);
    CHECK(result.structured.at("refs").contains("source"));
    CHECK(result.structured.at("refs").contains("gain"));
    CHECK(result.structured.at("refs").contains("channelizer"));
    CHECK(result.structured.at("applied")[2].at("params").at("filter_type") == 2);
    CHECK(fixture.host.checkpoint_begins == 1);
    CHECK(fixture.host.checkpoint_commits == 1);
    CHECK(fixture.host.checkpoint_discards == 0);
    CHECK(fixture.host.last_checkpoint_summary == "+3 components, +2 links, +1 probe");
    REQUIRE(fixture.host.last_placements.size() == 3);
    CHECK(fixture.host.last_placements[0].column == 0);
    CHECK(fixture.host.last_placements[1].column == 1);
    CHECK(fixture.host.last_placements[2].column == 2);
    REQUIRE(fixture.host.last_placements[1].position.has_value());
    CHECK((*fixture.host.last_placements[1].position)[0] == 400.0F);
    CHECK((*fixture.host.last_placements[1].position)[1] == 25.0F);
    REQUIRE(fixture.host.activities.size() == 1);
    CHECK(fixture.host.activities[0].tool == "circuit_edit");
    CHECK(fixture.host.activities[0].ok);
}

TEST_CASE("circuit_edit stops at the first missing component and keeps its prefix",
          "[agent_api][circuit_edit]") {
    ApiFixture fixture;
    const auto result = editCall(
        fixture,
        nlohmann::ordered_json::array({{{"op", "add"}, {"ref", "source"}, {"type", "generator"}},
                                       {{"op", "remove"}, {"component", 987654}}}));
    REQUIRE(result.is_error);
    CHECK(result.structured.at("epoch") == fixture.api.epoch());
    REQUIRE(errorFor(result).at("code") == "NOT_FOUND");
    REQUIRE(errorFor(result).contains("op_index"));
    REQUIRE(result.structured.contains("applied"));
    CHECK(errorFor(result).at("op_index") == 1);
    REQUIRE(result.structured.at("applied").size() == 1);
    CHECK(result.structured.at("applied")[0].at("op") == "add");
    CHECK(fixture.host.checkpoint_begins == 1);
    CHECK(fixture.host.checkpoint_commits == 1);
    CHECK(fixture.host.checkpoint_discards == 0);
}

TEST_CASE("circuit_edit rejects malformed operations at their ordered index",
          "[agent_api][circuit_edit]") {
    ApiFixture fixture;
    const auto result =
        editCall(fixture, nlohmann::ordered_json::array(
                              {{{"op", "add"}, {"type", "generator"}},
                               {{"op", "connect"}, {"from", {{"component", 1}, {"port", 0}}}}}));
    REQUIRE(result.is_error);
    CHECK(result.structured.at("epoch") == fixture.api.epoch());
    REQUIRE(errorFor(result).at("code") == "INVALID_ARGUMENT");
    REQUIRE(errorFor(result).contains("op_index"));
    REQUIRE(result.structured.contains("applied"));
    CHECK(result.structured.at("applied").size() == 1);
    CHECK(fixture.host.checkpoint_commits == 1);
}

TEST_CASE("circuit_edit discards a checkpoint when its first operation fails",
          "[agent_api][circuit_edit]") {
    ApiFixture fixture;
    const auto revision = fixture.commands.revision();
    const auto result = editCall(
        fixture, nlohmann::ordered_json::array({{{"op", "remove"}, {"component", 987654}}}));
    REQUIRE(result.is_error);
    CHECK(result.structured.at("epoch") == fixture.api.epoch());
    REQUIRE(errorFor(result).at("code") == "NOT_FOUND");
    REQUIRE(errorFor(result).contains("op_index"));
    REQUIRE(result.structured.contains("applied"));
    CHECK(errorFor(result).at("op_index") == 0);
    CHECK(result.structured.at("applied").empty());
    CHECK(fixture.commands.revision() == revision);
    CHECK(fixture.host.checkpoint_begins == 1);
    CHECK(fixture.host.checkpoint_commits == 0);
    CHECK(fixture.host.checkpoint_discards == 1);
}

TEST_CASE("circuit_edit classifies occupied inputs, cycles, and ADC-to-PFB policy errors",
          "[agent_api][circuit_edit]") {
    ApiFixture fixture;
    const auto occupied = editCall(
        fixture,
        nlohmann::ordered_json::array({{{"op", "add"}, {"ref", "g1"}, {"type", "generator"}},
                                       {{"op", "add"}, {"ref", "g2"}, {"type", "generator"}},
                                       {{"op", "add"}, {"ref", "amp"}, {"type", "amplifier"}},
                                       {{"op", "connect"},
                                        {"from", {{"ref", "g1"}, {"port", 0}}},
                                        {"to", {{"ref", "amp"}, {"port", 0}}}},
                                       {{"op", "connect"},
                                        {"from", {{"ref", "g2"}, {"port", 0}}},
                                        {"to", {{"ref", "amp"}, {"port", 0}}}}}));
    REQUIRE(occupied.is_error);
    CHECK(occupied.structured.at("epoch") == fixture.api.epoch());
    REQUIRE(errorFor(occupied).at("code") == "LINK_REJECTED");
    REQUIRE(errorFor(occupied).contains("details"));
    REQUIRE(errorFor(occupied).contains("op_index"));
    REQUIRE(occupied.structured.contains("applied"));
    CHECK(errorFor(occupied).at("details").at("reason") == "INPUT_OCCUPIED");
    CHECK(errorFor(occupied).at("op_index") == 4);
    CHECK(errorFor(occupied).at("details").at("existing_source") ==
          nlohmann::json{{"component", occupied.structured.at("applied")[0].at("component")},
                         {"port", 0}});
    CHECK(occupied.structured.at("applied").size() == 4);

    ApiFixture cycle_fixture;
    const auto cycle = editCall(
        cycle_fixture,
        nlohmann::ordered_json::array({{{"op", "add"}, {"ref", "left"}, {"type", "amplifier"}},
                                       {{"op", "add"}, {"ref", "right"}, {"type", "amplifier"}},
                                       {{"op", "connect"},
                                        {"from", {{"ref", "left"}, {"port", 0}}},
                                        {"to", {{"ref", "right"}, {"port", 0}}}},
                                       {{"op", "connect"},
                                        {"from", {{"ref", "right"}, {"port", 0}}},
                                        {"to", {{"ref", "left"}, {"port", 0}}}}}));
    REQUIRE(cycle.is_error);
    CHECK(cycle.structured.at("epoch") == cycle_fixture.api.epoch());
    REQUIRE(errorFor(cycle).at("code") == "LINK_REJECTED");
    REQUIRE(errorFor(cycle).contains("details"));
    REQUIRE(errorFor(cycle).contains("op_index"));
    CHECK(errorFor(cycle).at("details").at("reason") == "CYCLE");
    CHECK(errorFor(cycle).at("op_index") == 3);

    ApiFixture policy_fixture;
    const auto policy = editCall(
        policy_fixture,
        nlohmann::ordered_json::array({{{"op", "add"}, {"ref", "rf"}, {"type", "generator"}},
                                       {{"op", "add"}, {"ref", "channelizer"}, {"type", "pfb"}},
                                       {{"op", "connect"},
                                        {"from", {{"ref", "rf"}, {"port", 0}}},
                                        {"to", {{"ref", "channelizer"}, {"port", 0}}}}}));
    REQUIRE(policy.is_error);
    CHECK(policy.structured.at("epoch") == policy_fixture.api.epoch());
    REQUIRE(errorFor(policy).at("code") == "LINK_REJECTED");
    REQUIRE(errorFor(policy).contains("details"));
    REQUIRE(errorFor(policy).contains("op_index"));
    CHECK(errorFor(policy).at("details").at("reason") == "ADC_TO_PFB_ONLY");
    CHECK(errorFor(policy).at("op_index") == 2);
}

TEST_CASE("circuit_edit probes enforce capacity and preserve probe revision semantics",
          "[agent_api][circuit_edit]") {
    ApiFixture fixture;
    const auto added = editCall(
        fixture,
        nlohmann::ordered_json::array({{{"op", "add"}, {"ref", "a"}, {"type", "generator"}},
                                       {{"op", "add"}, {"ref", "b"}, {"type", "generator"}},
                                       {{"op", "add"}, {"ref", "c"}, {"type", "generator"}},
                                       {{"op", "add"}, {"ref", "d"}, {"type", "generator"}},
                                       {{"op", "add"}, {"ref", "e"}, {"type", "generator"}}}));
    REQUIRE_FALSE(added.is_error);
    CHECK(added.structured.at("epoch") == fixture.api.epoch());
    const auto epoch = fixture.api.epoch();
    const auto ids = added.structured.at("refs");
    const auto revision = fixture.commands.revision();
    const auto fill =
        editCall(fixture, nlohmann::ordered_json::array(
                              {{{"op", "probe_add"}, {"at", {{"ref", "a"}, {"port", 0}}}},
                               {{"op", "probe_add"}, {"at", {{"ref", "b"}, {"port", 0}}}},
                               {{"op", "probe_add"}, {"at", {{"ref", "c"}, {"port", 0}}}},
                               {{"op", "probe_add"}, {"at", {{"ref", "d"}, {"port", 0}}}}}));
    REQUIRE_FALSE(fill.is_error);
    CHECK(fill.structured.at("epoch") == epoch);
    CHECK(fixture.commands.revision() == revision);
    CHECK(fixture.host.checkpoint_commits == 2);
    const auto duplicate = editCall(
        fixture, nlohmann::ordered_json::array(
                     {{{"op", "probe_add"}, {"at", {{"component", ids.at("a")}, {"port", 0}}}}}));
    REQUIRE_FALSE(duplicate.is_error);
    CHECK(duplicate.structured.at("epoch") == epoch);
    CHECK(duplicate.structured.at("applied")[0].at("unchanged") == true);
    CHECK(fixture.commands.revision() == revision);
    const auto fifth = editCall(
        fixture, nlohmann::ordered_json::array(
                     {{{"op", "probe_add"}, {"at", {{"component", ids.at("e")}, {"port", 0}}}}}));
    REQUIRE(fifth.is_error);
    CHECK(fifth.structured.at("epoch") == epoch);
    REQUIRE(errorFor(fifth).at("code") == "INVALID_ARGUMENT");
    REQUIRE(errorFor(fifth).contains("details"));
    CHECK(errorFor(fifth).at("details").at("limit") == 4);
    const auto missing = editCall(
        fixture,
        nlohmann::ordered_json::array(
            {{{"op", "probe_remove"}, {"at", {{"component", ids.at("e")}, {"port", 0}}}}}));
    REQUIRE(missing.is_error);
    CHECK(missing.structured.at("epoch") == epoch);
    REQUIRE(errorFor(missing).at("code") == "NOT_FOUND");
}

TEST_CASE("circuit_edit reports component port counts for a missing port",
          "[agent_api][circuit_edit]") {
    ApiFixture fixture;
    const auto result =
        editCall(fixture, nlohmann::ordered_json::array(
                              {{{"op", "add"}, {"ref", "source"}, {"type", "generator"}},
                               {{"op", "probe_add"}, {"at", {{"ref", "source"}, {"port", 1}}}}}));
    REQUIRE(result.is_error);
    CHECK(result.structured.at("epoch") == fixture.api.epoch());
    REQUIRE(errorFor(result).at("code") == "NOT_FOUND");
    REQUIRE(errorFor(result).contains("op_index"));
    REQUIRE(errorFor(result).contains("message"));
    REQUIRE(result.structured.contains("applied"));
    CHECK(errorFor(result).at("op_index") == 1);
    CHECK(errorFor(result).at("message").get<std::string>().find("0 inputs and 1 output") !=
          std::string::npos);
    CHECK(result.structured.at("applied").size() == 1);
}

TEST_CASE("circuit_edit maps parameter failures", "[agent_api][circuit_edit]") {
    ApiFixture fixture;
    const auto added = editCall(
        fixture, nlohmann::ordered_json::array({{{"op", "add"}, {"ref", "pfb"}, {"type", "pfb"}}}));
    REQUIRE_FALSE(added.is_error);
    CHECK(added.structured.at("epoch") == fixture.api.epoch());
    const int pfb = added.structured.at("refs").at("pfb");
    const auto unknown = editCall(
        fixture,
        nlohmann::ordered_json::array(
            {{{"op", "set_params"}, {"component", pfb}, {"params", {{"channel_cout", 12}}}}}));
    REQUIRE(unknown.is_error);
    CHECK(unknown.structured.at("epoch") == fixture.api.epoch());
    REQUIRE(errorFor(unknown).at("code") == "PARAM_REJECTED");
    REQUIRE(errorFor(unknown).contains("details"));
    CHECK(errorFor(unknown).at("details").at("reason") == "UNKNOWN_KEY");
    CHECK(errorFor(unknown).at("details").at("suggestions").size() > 0);
    const auto adjusted = editCall(
        fixture,
        nlohmann::ordered_json::array(
            {{{"op", "set_params"}, {"component", pfb}, {"params", {{"channel_count", 4096}}}}}));
    REQUIRE(adjusted.is_error);
    CHECK(adjusted.structured.at("epoch") == fixture.api.epoch());
    REQUIRE(errorFor(adjusted).at("code") == "PARAM_REJECTED");
    REQUIRE(errorFor(adjusted).contains("details"));
    CHECK(errorFor(adjusted).at("details").at("reason") == "ENGINE_ADJUSTED");
    CHECK(errorFor(adjusted).at("details").at("requested") == 4096);
    CHECK(errorFor(adjusted).at("details").at("stored") == 2048);
    const auto path = editCall(fixture, nlohmann::ordered_json::array(
                                            {{{"op", "set_params"},
                                              {"component", pfb},
                                              {"params", {{"sparam_filepath", "blocked.s2p"}}}}}));
    REQUIRE(path.is_error);
    CHECK(path.structured.at("epoch") == fixture.api.epoch());
    REQUIRE(errorFor(path).at("code") == "PATH_PARAMS_UNSUPPORTED");
}

TEST_CASE("circuit_edit reports the first invalid parameter in insertion order",
          "[agent_api][circuit_edit]") {
    ApiFixture fixture;
    auto *amplifier = fixture.add<AmplifierEngine>("amplifier");
    REQUIRE(amplifier);
    const nlohmann::ordered_json params{{"z_unknown_first", 1}, {"a_unknown_second", 2}};
    const auto result = editCall(
        fixture, nlohmann::ordered_json::array(
                     {{{"op", "set_params"}, {"component", amplifier->id()}, {"params", params}}}));
    REQUIRE(result.is_error);
    CHECK(result.structured.at("epoch") == fixture.api.epoch());
    REQUIRE(errorFor(result).at("code") == "PARAM_REJECTED");
    REQUIRE(errorFor(result).contains("details"));
    REQUIRE(errorFor(result).contains("op_index"));
    CHECK(errorFor(result).at("details").at("path") == "z_unknown_first");
    CHECK(errorFor(result).at("op_index") == 0);
}

TEST_CASE("circuit_edit revisions advance only for accepted component changes",
          "[agent_api][circuit_edit]") {
    ApiFixture fixture;
    const auto before = fixture.commands.revision();
    const auto add =
        editCall(fixture, nlohmann::ordered_json::array({{{"op", "add"}, {"type", "amplifier"}}}));
    REQUIRE_FALSE(add.is_error);
    CHECK(add.structured.at("epoch") == fixture.api.epoch());
    CHECK(add.structured.at("revision") == before + 1);
    const int id = add.structured.at("applied")[0].at("component");
    const auto accepted_revision = fixture.commands.revision();
    const auto rejected = editCall(
        fixture, nlohmann::ordered_json::array(
                     {{{"op", "set_params"}, {"component", id}, {"params", {{"missing", 1}}}}}));
    REQUIRE(rejected.is_error);
    CHECK(rejected.structured.at("epoch") == fixture.api.epoch());
    CHECK(fixture.commands.revision() == accepted_revision);
    const auto change = editCall(
        fixture, nlohmann::ordered_json::array(
                     {{{"op", "set_params"}, {"component", id}, {"params", {{"gain_dB", 2.5}}}}}));
    REQUIRE_FALSE(change.is_error);
    CHECK(change.structured.at("epoch") == fixture.api.epoch());
    CHECK(change.structured.at("revision") == accepted_revision + 1);
    CHECK(change.structured.at("applied")[0].at("params").at("gain_dB") == 2.5);
}

TEST_CASE("circuit_edit resolves library parts case-insensitively and reports ambiguity",
          "[agent_api][circuit_edit]") {
    ApiFixture fixture;
    auto all = fixture.library.all();
    const auto original = std::find_if(
        all.begin(), all.end(), [](const auto *part) { return part->part_number == "ZX60-33LN+"; });
    REQUIRE(original != all.end());
    const auto attenuator = std::find_if(
        all.begin(), all.end(), [](const auto *part) { return part->type == "attenuator"; });
    REQUIRE(attenuator != all.end());
    ComponentDefinition duplicate = **attenuator;
    duplicate.part_number = "ZX60-33LN+";
    duplicate.source_path += ".duplicate";
    duplicate.manufacturer = "Duplicate vendor";
    fixture.library.upsert(duplicate);
    const auto lower = editCall(
        fixture, nlohmann::ordered_json::array(
                     {{{"op", "add"},
                       {"library_part", {{"part_number", "zx60-33ln+"}, {"type", "amplifier"}}}}}));
    REQUIRE_FALSE(lower.is_error);
    CHECK(lower.structured.at("epoch") == fixture.api.epoch());
    CHECK(lower.structured.at("applied")[0].at("component") > 0);
    const auto ambiguous = editCall(
        fixture, nlohmann::ordered_json::array(
                     {{{"op", "add"}, {"library_part", {{"part_number", "ZX60-33LN+"}}}}}));
    REQUIRE(ambiguous.is_error);
    CHECK(ambiguous.structured.at("epoch") == fixture.api.epoch());
    REQUIRE(errorFor(ambiguous).at("code") == "AMBIGUOUS_PART");
    REQUIRE(errorFor(ambiguous).contains("details"));
    REQUIRE(errorFor(ambiguous).at("details").contains("candidates"));
    REQUIRE(errorFor(ambiguous).at("details").at("candidates").size() == 2);
    CHECK(errorFor(ambiguous).at("details").at("candidates")[0].contains("part_number"));
    CHECK(errorFor(ambiguous).at("details").at("candidates")[0].contains("type"));
    CHECK(errorFor(ambiguous).at("details").at("candidates")[0].contains("manufacturer"));
    const auto unknown =
        editCall(fixture, nlohmann::ordered_json::array(
                              {{{"op", "add"}, {"library_part", {{"part_number", "ZX60-33L"}}}}}));
    REQUIRE(unknown.is_error);
    CHECK(unknown.structured.at("epoch") == fixture.api.epoch());
    REQUIRE(errorFor(unknown).at("code") == "UNKNOWN_PART");
    REQUIRE(errorFor(unknown).contains("details"));
    CHECK(errorFor(unknown).at("details").contains("candidates"));
}

TEST_CASE("circuit_edit reports consequential PFB changes with path and stored values",
          "[agent_api][circuit_edit]") {
    ApiFixture fixture;
    auto *pfb = fixture.add<PFBChannelizerEngine>("pfb");
    REQUIRE(pfb);
    const auto defaults = pfb->serialize();
    REQUIRE(
        fixture.commands
            .setComponentParams(pfb->graphNodeId(),
                                {{"active_channel", defaults.at("channel_count").get<int>() - 1}})
            .ok());
    const auto before = pfb->serialize();
    REQUIRE(before.at("active_channel") == before.at("channel_count").get<int>() - 1);
    const auto result =
        editCall(fixture, nlohmann::ordered_json::array({{{"op", "set_params"},
                                                          {"component", pfb->id()},
                                                          {"params", {{"channel_count", 4}}}}}));
    REQUIRE_FALSE(result.is_error);
    CHECK(result.structured.at("epoch") == fixture.api.epoch());
    const auto &applied = result.structured.at("applied")[0];
    CHECK(applied.at("params").at("channel_count") == 4);
    CHECK(applied.at("params").at("active_channel") == 3);
    REQUIRE(applied.at("also_changed").size() == 1);
    CHECK(applied.at("also_changed")[0].at("path") == "active_channel");
    CHECK(applied.at("also_changed")[0].at("old_value") == before.at("active_channel"));
    CHECK(applied.at("also_changed")[0].at("new_value") == 3);
}

TEST_CASE("placement follows added-node topology and preserves explicit coordinates",
          "[agent_api][circuit_edit]") {
    ApiFixture fixture;
    const auto result = editCall(
        fixture,
        nlohmann::ordered_json::array({{{"op", "add"},
                                        {"ref", "root"},
                                        {"type", "splitter"},
                                        {"position", {{"x", 11.0}, {"y", 12.0}}}},
                                       {{"op", "add"}, {"ref", "branch_a"}, {"type", "amplifier"}},
                                       {{"op", "add"}, {"ref", "branch_b"}, {"type", "amplifier"}},
                                       {{"op", "connect"},
                                        {"from", {{"ref", "root"}, {"port", 0}}},
                                        {"to", {{"ref", "branch_a"}, {"port", 0}}}},
                                       {{"op", "connect"},
                                        {"from", {{"ref", "root"}, {"port", 1}}},
                                        {"to", {{"ref", "branch_b"}, {"port", 0}}}}}));
    REQUIRE_FALSE(result.is_error);
    CHECK(result.structured.at("epoch") == fixture.api.epoch());
    REQUIRE(fixture.host.last_placements.size() == 3);
    CHECK(fixture.host.last_placements[0].column == 0);
    CHECK(fixture.host.last_placements[0].position.has_value());
    CHECK((*fixture.host.last_placements[0].position)[0] == 11.0F);
    CHECK((*fixture.host.last_placements[0].position)[1] == 12.0F);
    CHECK(fixture.host.last_placements[1].column == 1);
    CHECK(fixture.host.last_placements[1].row == 0);
    CHECK(fixture.host.last_placements[2].column == 1);
    CHECK(fixture.host.last_placements[2].row == 1);
}

TEST_CASE("circuit_edit validates duplicate and undefined refs in operation order",
          "[agent_api][circuit_edit]") {
    ApiFixture duplicate_fixture;
    const auto duplicate = editCall(
        duplicate_fixture,
        nlohmann::ordered_json::array({{{"op", "add"}, {"ref", "source"}, {"type", "generator"}},
                                       {{"op", "add"}, {"ref", "source"}, {"type", "amplifier"}}}));
    REQUIRE(duplicate.is_error);
    CHECK(duplicate.structured.at("epoch") == duplicate_fixture.api.epoch());
    REQUIRE(errorFor(duplicate).at("code") == "INVALID_ARGUMENT");
    REQUIRE(errorFor(duplicate).contains("details"));
    REQUIRE(errorFor(duplicate).contains("op_index"));
    REQUIRE(duplicate.structured.contains("applied"));
    CHECK(errorFor(duplicate).at("op_index") == 1);
    REQUIRE(duplicate.structured.at("applied").size() == 1);
    CHECK(duplicate_fixture.host.checkpoint_commits == 1);

    ApiFixture undefined_fixture;
    const auto undefined =
        editCall(undefined_fixture,
                 nlohmann::ordered_json::array(
                     {{{"op", "add"}, {"ref", "source"}, {"type", "generator"}},
                      {{"op", "set_params"}, {"ref", "later"}, {"params", {{"gain_dB", 1.0}}}}}));
    REQUIRE(undefined.is_error);
    CHECK(undefined.structured.at("epoch") == undefined_fixture.api.epoch());
    REQUIRE(errorFor(undefined).at("code") == "NOT_FOUND");
    REQUIRE(errorFor(undefined).contains("op_index"));
    REQUIRE(undefined.structured.contains("applied"));
    CHECK(errorFor(undefined).at("op_index") == 1);
    REQUIRE(undefined.structured.at("applied").size() == 1);
    CHECK(undefined_fixture.host.checkpoint_commits == 1);
}

TEST_CASE("circuit_edit rejects empty and oversized operation lists before checkpointing",
          "[agent_api][circuit_edit]") {
    ApiFixture empty_fixture;
    const auto empty = editCall(empty_fixture, nlohmann::ordered_json::array());
    REQUIRE(empty.is_error);
    CHECK(empty.structured.at("epoch") == empty_fixture.api.epoch());
    REQUIRE(errorFor(empty).at("code") == "INVALID_ARGUMENT");
    REQUIRE(errorFor(empty).contains("details"));
    CHECK(errorFor(empty).at("details").at("path") == "/ops");
    CHECK(empty_fixture.host.checkpoint_begins == 0);
    CHECK(empty_fixture.host.checkpoint_commits == 0);
    CHECK(empty_fixture.host.checkpoint_discards == 0);

    ApiFixture oversized_fixture;
    nlohmann::ordered_json ops = nlohmann::ordered_json::array();
    for (int i = 0; i < 65; ++i)
        ops.push_back({{"op", "add"}, {"type", "generator"}});
    const auto oversized = editCall(oversized_fixture, std::move(ops));
    REQUIRE(oversized.is_error);
    CHECK(oversized.structured.at("epoch") == oversized_fixture.api.epoch());
    REQUIRE(errorFor(oversized).at("code") == "INVALID_ARGUMENT");
    REQUIRE(errorFor(oversized).contains("details"));
    CHECK(errorFor(oversized).at("details").at("path") == "/ops");
    CHECK(oversized_fixture.host.checkpoint_begins == 0);
    CHECK(oversized_fixture.host.checkpoint_commits == 0);
    CHECK(oversized_fixture.host.checkpoint_discards == 0);
}
