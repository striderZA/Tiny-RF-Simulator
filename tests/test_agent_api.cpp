#if __has_include("imgui.h") || __has_include("imnodes.h")
#error "agent_api tests must remain independent of the UI"
#endif

#include "agent_api.h"
#include "agent_catalog.h"
#include "agent_errors.h"
#include "agent_links.h"
#include "agent_tools.h"
#include "amplifier_engine.h"
#include "component_engine_base.h"
#include "component_library.h"
#include "component_type_registry.h"
#include "editor_commands.h"
#include "graph_editor_actions.h"
#include "ideal_filter_engine.h"
#include "logging_core.h"
#include "network_analyzer_engine.h"
#include "pfb_channelizer_engine.h"
#include "rf_switch_engine.h"
#include "signal_generator_engine.h"
#include "spectrum_analyzer_engine.h"
#include "splitter_engine.h"

#include "adc_engine.h"
#include "component_params.h"
#include "flow_metrics.h"
#include "output_snr.h"
#include "power_meter_engine.h"
#include "test_temp_paths.h"
#include "touchstone_parser.h"
#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <filesystem>
#include <fstream>
#include <limits>

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
        ++activity_attempts;
        if (throw_on_activity)
            throw std::runtime_error("activity sink failed");
        activities.push_back(entry);
    }

    bool modal_open = false;
    int activity_attempts = 0;
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

TEST_CASE("measure_port includes replacement details in stale-epoch errors",
          "[agent_api][measure_port]") {
    ApiFixture fixture;
    const auto stale_epoch = fixture.api.epoch();

    fixture.runtime.clearComponentsAndResetIds();
    fixture.api.noteProjectReplaced(AgentReplacementCause::Reverted, {"add generator"});
    const auto current_epoch = fixture.api.epoch();
    const auto result = fixture.call("measure_port", {{"epoch", stale_epoch}});

    REQUIRE(result.is_error);
    const auto &error = errorFor(result);
    CHECK(error.at("code") == "STALE_EPOCH");
    CHECK(error.at("details").at("epoch") == current_epoch);
    CHECK(error.at("details").at("cause") == "reverted");
    CHECK(error.at("details").at("undone") == nlohmann::json::array({"add generator"}));
}

TEST_CASE("network_analyzer_sweep includes replacement details in stale-epoch errors",
          "[agent_api][network_analyzer_sweep]") {
    ApiFixture fixture;
    const auto stale_epoch = fixture.api.epoch();

    fixture.runtime.clearComponentsAndResetIds();
    fixture.api.noteProjectReplaced(AgentReplacementCause::Reverted, {"add generator"});
    const auto current_epoch = fixture.api.epoch();
    const auto result = fixture.call("network_analyzer_sweep", {{"epoch", stale_epoch}});

    REQUIRE(result.is_error);
    const auto &error = errorFor(result);
    CHECK(error.at("code") == "STALE_EPOCH");
    CHECK(error.at("details").at("epoch") == current_epoch);
    CHECK(error.at("details").at("cause") == "reverted");
    CHECK(error.at("details").at("undone") == nlohmann::json::array({"add generator"}));
}

TEST_CASE("AMBIGUOUS_PART candidates are capped at the first five sorted definitions",
          "[agent_api][circuit_edit]") {
    ApiFixture fixture;
    const auto library_definitions = fixture.library.all();
    const std::array<std::string_view, 6> types{"filter",     "equalizer", "combiner",
                                                "attenuator", "amplifier", "adc"};
    std::vector<ComponentDefinition> ambiguous_definitions;
    for (const std::string_view type : types) {
        const std::string type_name{type};
        const auto source = std::find_if(library_definitions.begin(), library_definitions.end(),
                                         [&type_name](const ComponentDefinition *definition) {
                                             return definition->type == type_name;
                                         });
        REQUIRE(source != library_definitions.end());
        ComponentDefinition candidate = **source;
        candidate.part_number = "AMBIGUOUS-CAP";
        candidate.manufacturer = "Vendor " + type_name;
        candidate.source_path = "test:ambiguous-cap:" + type_name;
        ambiguous_definitions.push_back(std::move(candidate));
    }
    for (const auto &definition : ambiguous_definitions)
        fixture.library.upsert(definition);

    const auto inserted_definitions = fixture.library.all();
    const auto matching_definition_count =
        std::count_if(inserted_definitions.begin(), inserted_definitions.end(),
                      [](const ComponentDefinition *definition) {
                          return definition->part_number == "AMBIGUOUS-CAP";
                      });
    REQUIRE(matching_definition_count == 6);

    const auto result = editCall(
        fixture, nlohmann::ordered_json::array(
                     {{{"op", "add"}, {"library_part", {{"part_number", "AMBIGUOUS-CAP"}}}}}));
    REQUIRE(result.is_error);
    const auto &error = errorFor(result);
    REQUIRE(error.at("code") == "AMBIGUOUS_PART");
    REQUIRE(error.contains("details"));
    REQUIRE(error.at("details").contains("candidates"));
    const auto &candidates = error.at("details").at("candidates");
    REQUIRE(candidates.size() == 5);
    const nlohmann::ordered_json expected_candidates = nlohmann::ordered_json::array(
        {{{"part_number", "AMBIGUOUS-CAP"}, {"type", "adc"}, {"manufacturer", "Vendor adc"}},
         {{"part_number", "AMBIGUOUS-CAP"},
          {"type", "amplifier"},
          {"manufacturer", "Vendor amplifier"}},
         {{"part_number", "AMBIGUOUS-CAP"},
          {"type", "attenuator"},
          {"manufacturer", "Vendor attenuator"}},
         {{"part_number", "AMBIGUOUS-CAP"},
          {"type", "combiner"},
          {"manufacturer", "Vendor combiner"}},
         {{"part_number", "AMBIGUOUS-CAP"},
          {"type", "equalizer"},
          {"manufacturer", "Vendor equalizer"}}});
    CHECK(candidates == expected_candidates);
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

TEST_CASE("activity sink failures do not hide committed circuit edits", "[agent_api]") {
    ApiFixture fixture;
    LoggerCore::instance().clear();
    fixture.host.throw_on_activity = true;

    const auto result = editCall(
        fixture,
        nlohmann::ordered_json::array({{{"op", "add"}, {"ref", "gen"}, {"type", "generator"}}}));

    REQUIRE_FALSE(result.is_error);
    CHECK(result.structured.at("epoch") == fixture.api.epoch());
    REQUIRE(result.structured.contains("applied"));
    REQUIRE(result.structured.at("applied").size() == 1);
    REQUIRE(result.structured.contains("refs"));
    CHECK(result.structured.at("refs").at("gen") ==
          result.structured.at("applied")[0].at("component"));
    CHECK(result.structured.at("revision") == 1);
    CHECK(fixture.commands.revision() == 1);
    CHECK(fixture.host.checkpoint_begins == 1);
    CHECK(fixture.host.checkpoint_commits == 1);
    CHECK(fixture.host.checkpoint_discards == 0);
    CHECK(fixture.host.activity_attempts == 1);
    CHECK(fixture.host.activities.empty());
    CHECK(fixture.errorWasLogged("circuit_edit"));
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
    const auto fill = editCall(
        fixture, nlohmann::ordered_json::array(
                     {{{"op", "probe_add"}, {"at", {{"component", ids.at("a")}, {"port", 0}}}},
                      {{"op", "probe_add"}, {"at", {{"component", ids.at("b")}, {"port", 0}}}},
                      {{"op", "probe_add"}, {"at", {{"component", ids.at("c")}, {"port", 0}}}},
                      {{"op", "probe_add"}, {"at", {{"component", ids.at("d")}, {"port", 0}}}}}));
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
    const auto empty_type = editCall(
        fixture,
        nlohmann::ordered_json::array(
            {{{"op", "add"}, {"library_part", {{"part_number", "ZX60-33LN+"}, {"type", ""}}}}}));
    REQUIRE(empty_type.is_error);
    CHECK(empty_type.structured.at("epoch") == fixture.api.epoch());
    REQUIRE(errorFor(empty_type).at("code") == "UNKNOWN_PART");
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
    const auto longer_query = editCall(
        fixture, nlohmann::ordered_json::array(
                     {{{"op", "add"}, {"library_part", {{"part_number", "ZX60-33LN+EXTRA"}}}}}));
    REQUIRE(longer_query.is_error);
    CHECK(longer_query.structured.at("epoch") == fixture.api.epoch());
    REQUIRE(errorFor(longer_query).at("code") == "UNKNOWN_PART");
    REQUIRE(errorFor(longer_query).contains("details"));
    REQUIRE(errorFor(longer_query).at("details").contains("candidates"));
    const auto &longer_candidates = errorFor(longer_query).at("details").at("candidates");
    CHECK(
        std::none_of(longer_candidates.begin(), longer_candidates.end(), [](const auto &candidate) {
            return candidate.at("part_number") == "ZX60-33LN+";
        }));
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
    ApiFixture malformed_fixture;
    const auto malformed =
        editCall(malformed_fixture,
                 nlohmann::ordered_json::array(
                     {{{"op", "set_params"}, {"ref", "bad-ref"}, {"params", {{"gain_dB", 1.0}}}}}));
    REQUIRE(malformed.is_error);
    CHECK(malformed.structured.at("epoch") == malformed_fixture.api.epoch());
    REQUIRE(errorFor(malformed).at("code") == "INVALID_ARGUMENT");
    REQUIRE(errorFor(malformed).contains("details"));
    CHECK(errorFor(malformed).at("details").at("path") == "/ops/0/ref");
    REQUIRE(errorFor(malformed).contains("op_index"));
    CHECK(errorFor(malformed).at("op_index") == 0);
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

TEST_CASE("link rejection classifier maps otherwise-allowed links to POLICY",
          "[agent_api][circuit_edit]") {
    ApiFixture fixture;
    auto *generator = fixture.add<SignalGeneratorEngine>("generator");
    auto *amplifier = fixture.add<AmplifierEngine>("amplifier");
    REQUIRE(generator);
    REQUIRE(amplifier);
    CHECK(classifyLinkRejection(fixture.runtime.graph(), *generator, *amplifier,
                                generator->outputPinId(), amplifier->inputPinId()) == "POLICY");
}

TEST_CASE("agent parameter errors map deserialize and restore failures",
          "[agent_api][circuit_edit]") {
    ParamWriteResult deserialize_failed;
    deserialize_failed.status = ParamWriteStatus::DeserializeFailed;
    const auto deserialize_error = agentParamError(deserialize_failed, 7);
    CHECK(agentErrorCodeName(deserialize_error.code) == "PARAM_REJECTED");
    CHECK(deserialize_error.op_index == 7);
    CHECK(deserialize_error.details.at("reason") == "DESERIALIZE_FAILED");

    ParamWriteResult restore_failed;
    restore_failed.status = ParamWriteStatus::RestoreFailed;
    const std::string diagnostic = "private rollback diagnostic sentinel";
    restore_failed.error = diagnostic;
    LoggerCore::instance().clear();
    const auto restore_error = agentParamError(restore_failed, 8);
    CHECK(agentErrorCodeName(restore_error.code) == "INTERNAL");
    CHECK(restore_error.op_index == 8);
    CHECK(restore_error.message.find(diagnostic) == std::string::npos);
    CHECK(restore_error.details.dump().find(diagnostic) == std::string::npos);
    const auto entries = LoggerCore::instance().entries();
    CHECK(std::any_of(entries.begin(), entries.end(), [&](const LogEntry &entry) {
        return entry.level == Level::Error && entry.message.find(diagnostic) != std::string::npos;
    }));
}

TEST_CASE("placement depth ignores an existing component between added nodes",
          "[agent_api][circuit_edit]") {
    ApiFixture fixture;
    auto *existing = fixture.add<AmplifierEngine>("amplifier");
    REQUIRE(existing);
    const auto result = editCall(
        fixture,
        nlohmann::ordered_json::array({{{"op", "add"}, {"ref", "left"}, {"type", "amplifier"}},
                                       {{"op", "add"}, {"ref", "right"}, {"type", "amplifier"}},
                                       {{"op", "connect"},
                                        {"from", {{"ref", "left"}, {"port", 0}}},
                                        {"to", {{"component", existing->id()}, {"port", 0}}}},
                                       {{"op", "connect"},
                                        {"from", {{"component", existing->id()}, {"port", 0}}},
                                        {"to", {{"ref", "right"}, {"port", 0}}}}}));
    REQUIRE_FALSE(result.is_error);
    CHECK(result.structured.at("epoch") == fixture.api.epoch());
    REQUIRE(fixture.host.last_placements.size() == 2);
    CHECK(fixture.host.last_placements[0].column == 0);
    CHECK(fixture.host.last_placements[0].row == 0);
    CHECK(fixture.host.last_placements[1].column == 0);
    CHECK(fixture.host.last_placements[1].row == 1);
}
TEST_CASE("measure_port matches registry and Power Meter totals and sorts stored tones",
          "[agent_api][measure_port]") {
    ApiFixture fixture;
    auto *generator = fixture.add<SignalGeneratorEngine>("generator");
    REQUIRE(generator);
    generator->addTone(1.1e9, -14.0);
    generator->addTone(0.9e9, -4.0);
    generator->addTone(1.2e9, -9.0);
    const auto result =
        fixture.call("measure_port", {{"epoch", fixture.api.epoch()},
                                      {"at", {{"component", generator->id()}, {"port", 0}}},
                                      {"max_tones", 2}});
    REQUIRE_FALSE(result.is_error);
    CHECK(result.structured.at("epoch") == fixture.api.epoch());
    const auto &spectrum = generator->node().outputs[0];
    const auto *power_metric = MetricRegistry::instance().find("power_dBm");
    const auto *peak_power_metric = MetricRegistry::instance().find("peak_power_dBm");
    const auto *peak_frequency_metric = MetricRegistry::instance().find("peak_freq_Hz");
    const auto *noise_metric = MetricRegistry::instance().find("noise_floor_dBm_per_Hz");
    REQUIRE(power_metric);
    REQUIRE(peak_power_metric);
    REQUIRE(peak_frequency_metric);
    REQUIRE(noise_metric);
    const double metric_total = power_metric->compute(spectrum);
    const PowerMeasurement meter = PowerMeterEngine{}.measure(&spectrum);
    REQUIRE(meter.valid);
    CHECK(result.structured.at("total_power_dBm").get<double>() ==
          Catch::Approx(metric_total).margin(1e-9));
    CHECK(result.structured.at("total_power_dBm").get<double>() ==
          Catch::Approx(meter.power_dBm).margin(1e-9));
    const auto strongest = std::max_element(
        spectrum.tones.begin(), spectrum.tones.end(),
        [](const auto &left, const auto &right) { return left.power_dBm < right.power_dBm; });
    REQUIRE(strongest != spectrum.tones.end());
    CHECK(result.structured.at("peak").at("freq_Hz") == peak_frequency_metric->compute(spectrum));
    CHECK(result.structured.at("peak").at("power_dBm") == peak_power_metric->compute(spectrum));
    REQUIRE(result.structured.at("tones").size() == 2);
    CHECK(result.structured.at("tone_count") == spectrum.tones.size());
    CHECK(result.structured.at("tones")[0].at("power_dBm") >=
          result.structured.at("tones")[1].at("power_dBm"));
    CHECK(result.structured.at("tones")[0].at("freq_Hz") == 0.9e9);
    CHECK(result.structured.at("tones")[0].at("power_dBm") == -4.0);
    CHECK(result.structured.at("tones")[0].at("phase_deg") == 0.0);
    CHECK(result.structured.at("tones")[1].at("freq_Hz") == 1.2e9);
    CHECK(result.structured.at("tones")[1].at("power_dBm") == -9.0);
    CHECK(result.structured.at("tones")[1].at("phase_deg") == 0.0);
    CHECK(result.structured.at("noise_floor_dBm_per_Hz") == noise_metric->compute(spectrum));
    CHECK_FALSE(result.structured.at("is_complex_baseband").get<bool>());
}

TEST_CASE("measure_port uses shared output SNR basis for amplifier and PFB channel",
          "[agent_api][measure_port]") {
    ApiFixture fixture;
    auto *generator = fixture.add<SignalGeneratorEngine>("generator");
    auto *amplifier = fixture.add<AmplifierEngine>("amplifier");
    REQUIRE(generator);
    REQUIRE(amplifier);
    generator->addTone(250e6, -20.0);
    REQUIRE(fixture.commands.connect(generator->outputPinId(), amplifier->inputPinId()));
    fixture.runtime.update(0.0);
    const auto amplifier_snr = computeOutputSnr(*amplifier, 0, fixture.spectrum_analyzer);
    REQUIRE(amplifier_snr.snr_dB.has_value());
    const auto amplifier_result =
        fixture.call("measure_port", {{"epoch", fixture.api.epoch()},
                                      {"at", {{"component", amplifier->id()}, {"port", 0}}}});
    REQUIRE_FALSE(amplifier_result.is_error);
    CHECK(amplifier_result.structured.at("snr_basis").at("kind") == "rbw");
    CHECK(amplifier_result.structured.at("snr_basis").at("rbw_Hz") == amplifier_snr.rbw_Hz);
    CHECK(amplifier_result.structured.at("snr_dB").get<double>() ==
          Catch::Approx(*amplifier_snr.snr_dB).margin(1e-9));

    auto *adc = fixture.add<AdcEngine>("adc");
    auto *pfb = fixture.add<PFBChannelizerEngine>("pfb");
    REQUIRE(adc);
    REQUIRE(pfb);
    REQUIRE(fixture.commands.connect(generator->outputPinId(), adc->inputPinId()));
    REQUIRE(fixture.commands.connect(adc->outputPinId(), pfb->inputPinId()));
    pfb->setActiveChannel(16);
    fixture.runtime.update(0.0);
    const auto pfb_snr = computeOutputSnr(*pfb, 0, fixture.spectrum_analyzer);
    REQUIRE(pfb_snr.snr_dB.has_value());
    REQUIRE(pfb_snr.channel_noise_dBm.has_value());
    const auto pfb_result =
        fixture.call("measure_port", {{"epoch", fixture.api.epoch()},
                                      {"at", {{"component", pfb->id()}, {"port", 0}}}});
    REQUIRE_FALSE(pfb_result.is_error);
    CHECK(pfb_result.structured.at("snr_basis").at("kind") == "pfb_channel");
    CHECK(pfb_result.structured.at("snr_basis").at("enbw_Hz") == pfb_snr.enbw_Hz);
    CHECK(pfb_result.structured.at("snr_dB").get<double>() ==
          Catch::Approx(*pfb_snr.snr_dB).margin(1e-9));
    CHECK(pfb_result.structured.at("snr_basis").at("channel_noise_dBm").get<double>() ==
          Catch::Approx(*pfb_snr.channel_noise_dBm).margin(1e-9));
    ApiFixture no_noise_fixture;
    auto *no_noise_pfb = no_noise_fixture.add<PFBChannelizerEngine>("pfb");
    REQUIRE(no_noise_pfb);
    no_noise_fixture.runtime.update(0.0);
    const auto no_noise = no_noise_fixture.call(
        "measure_port", {{"epoch", no_noise_fixture.api.epoch()},
                         {"at", {{"component", no_noise_pfb->id()}, {"port", 0}}}});
    REQUIRE_FALSE(no_noise.is_error);
    CHECK(no_noise.structured.at("snr_basis").at("kind") == "pfb_channel");
    CHECK(no_noise.structured.at("snr_basis").at("channel_noise_dBm").is_null());
}

TEST_CASE("measure_port traces use interval centers and linear noise means",
          "[agent_api][measure_port]") {
    ApiFixture fixture;
    auto *amplifier = fixture.add<AmplifierEngine>("amplifier");
    REQUIRE(amplifier);
    fixture.runtime.update(0.0);
    auto &spectrum = amplifier->node().outputs[0];
    spectrum.frequencies = {0.0, 5.0, 12.0, 40.0};
    spectrum.noise_total_W = {1.0, 3.0, 5.0, 7.0};
    const auto result = fixture.call(
        "measure_port", {{"epoch", fixture.api.epoch()},
                         {"at", {{"component", amplifier->id()}, {"port", 0}}},
                         {"trace", {{"start_Hz", 0.0}, {"stop_Hz", 40.0}, {"points", 4}}}});
    REQUIRE_FALSE(result.is_error);
    const auto &trace = result.structured.at("trace");
    REQUIRE(trace.is_object());
    const auto &frequencies = trace.at("frequencies_Hz");
    const auto &noise = trace.at("noise_dBm_per_Hz");
    REQUIRE(frequencies.size() == 4);
    REQUIRE(noise.size() == 4);
    CHECK(frequencies[0] == 5.0);
    CHECK(frequencies[1] == 15.0);
    CHECK(frequencies[2] == 25.0);
    CHECK(frequencies[3] == 35.0);
    CHECK(noise[0] == Catch::Approx(10.0 * std::log10(2.0) + 30.0));
    CHECK(noise[1] == Catch::Approx(10.0 * std::log10(5.0) + 30.0));
    CHECK(noise[2] == Catch::Approx(10.0 * std::log10(5.0) + 30.0));
    CHECK(noise[3] == Catch::Approx(10.0 * std::log10(7.0) + 30.0));
    const double largest = std::numeric_limits<double>::max();
    const auto wide = fixture.call(
        "measure_port", {{"epoch", fixture.api.epoch()},
                         {"at", {{"component", amplifier->id()}, {"port", 0}}},
                         {"trace", {{"start_Hz", -largest}, {"stop_Hz", largest}, {"points", 2}}}});
    REQUIRE_FALSE(wide.is_error);
    const auto &wide_frequencies = wide.structured.at("trace").at("frequencies_Hz");
    REQUIRE(wide_frequencies.size() == 2);
    REQUIRE(wide_frequencies[0].is_number());
    REQUIRE(wide_frequencies[1].is_number());
    CHECK(wide_frequencies[0].get<double>() == Catch::Approx(-largest / 2.0));
    CHECK(wide_frequencies[1].get<double>() == Catch::Approx(largest / 2.0));
    const auto invalid =
        fixture.call("measure_port", {{"epoch", fixture.api.epoch()},
                                      {"at", {{"component", amplifier->id()}, {"port", 0}}},
                                      {"trace", {{"start_Hz", 40.0}, {"stop_Hz", 0.0}}}});
    CHECK(errorFor(invalid).at("code") == "INVALID_ARGUMENT");
    CHECK(invalid.structured.at("epoch") == fixture.api.epoch());
}

TEST_CASE("measure_port sees component edits before a DSP frame", "[agent_api][measure_port]") {
    ApiFixture fixture;
    auto *generator = fixture.add<SignalGeneratorEngine>("generator");
    REQUIRE(generator);
    const int id = generator->id();
    const auto before = fixture.call(
        "measure_port", {{"epoch", fixture.api.epoch()}, {"at", {{"component", id}, {"port", 0}}}});
    REQUIRE_FALSE(before.is_error);
    generator->addTone(1.25e9, -6.0);
    const auto after = fixture.call(
        "measure_port", {{"epoch", fixture.api.epoch()}, {"at", {{"component", id}, {"port", 0}}}});
    REQUIRE_FALSE(after.is_error);
    CHECK(after.structured.at("tones").size() == before.structured.at("tones").size() + 1);
    CHECK(std::any_of(after.structured.at("tones").begin(), after.structured.at("tones").end(),
                      [](const auto &tone) { return tone.at("freq_Hz") == 1.25e9; }));
}

TEST_CASE("measure_port encodes unavailable numeric values and peak fields as null",
          "[agent_api][measure_port]") {
    ApiFixture fixture;
    auto *amplifier = fixture.add<AmplifierEngine>("amplifier");
    REQUIRE(amplifier);
    const auto result =
        fixture.call("measure_port", {{"epoch", fixture.api.epoch()},
                                      {"at", {{"component", amplifier->id()}, {"port", 0}}}});
    REQUIRE_FALSE(result.is_error);
    CHECK(result.structured.at("total_power_dBm").is_null());
    CHECK(result.structured.at("noise_floor_dBm_per_Hz").is_null());
    CHECK(result.structured.at("snr_dB").is_null());
    CHECK(result.structured.at("peak").at("freq_Hz").is_null());
    CHECK(result.structured.at("peak").at("power_dBm").is_null());
}
TEST_CASE("measure_port sorts non-finite tone powers safely and stably",
          "[agent_api][measure_port]") {
    ApiFixture fixture;
    auto *generator = fixture.add<SignalGeneratorEngine>("generator");
    REQUIRE(generator);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    generator->addTone(100e6, nan);
    generator->addTone(200e6, -infinity);
    generator->addTone(300e6, infinity);
    generator->addTone(400e6, -20.0);
    const auto result =
        fixture.call("measure_port", {{"epoch", fixture.api.epoch()},
                                      {"at", {{"component", generator->id()}, {"port", 0}}}});
    REQUIRE_FALSE(result.is_error);
    const auto &tones = result.structured.at("tones");
    REQUIRE(tones.size() == 4);
    CHECK(tones[0].at("freq_Hz") == 400e6);
    CHECK(tones[1].at("freq_Hz") == 100e6);
    CHECK(tones[2].at("freq_Hz") == 200e6);
    CHECK(tones[3].at("freq_Hz") == 300e6);
    for (std::size_t index = 1; index < tones.size(); ++index)
        CHECK(tones[index].at("power_dBm").is_null());
}

TEST_CASE("network_analyzer_sweep matches direct engine summary and sampled arrays",
          "[agent_api][network_analyzer_sweep]") {
    ApiFixture fixture;
    auto *generator = fixture.add<SignalGeneratorEngine>("generator");
    auto *amplifier = fixture.add<AmplifierEngine>("amplifier");
    REQUIRE(generator);
    REQUIRE(amplifier);
    REQUIRE(fixture.commands.connect(generator->outputPinId(), amplifier->inputPinId()));
    const auto result = fixture.call("network_analyzer_sweep",
                                     {{"epoch", fixture.api.epoch()},
                                      {"point_a", {{"component", generator->id()}, {"port", 0}}},
                                      {"point_b", {{"component", amplifier->id()}, {"port", 0}}},
                                      {"start_Hz", 900e6},
                                      {"stop_Hz", 1.1e9},
                                      {"points", 9},
                                      {"stimulus_dBm", -25.0},
                                      {"arrays", {{"max_points", 3}}}});
    REQUIRE_FALSE(result.is_error);
    NetworkAnalyzerEngine direct(fixture.runtime.graph(), fixture.chain_host);
    direct.setPointA(generator->outputPinId());
    direct.setPointB(amplifier->outputPinId());
    direct.setStartFrequency(900e6);
    direct.setStopFrequency(1.1e9);
    direct.setPoints(9);
    direct.setStimulusPower(-25.0);
    direct.update();
    CHECK(result.structured.at("epoch") == fixture.api.epoch());
    CHECK(result.structured.at("settings").at("points") == direct.points());
    const auto &arrays = result.structured.at("arrays");
    REQUIRE(arrays.at("frequencies_Hz").size() == 3);
    REQUIRE(arrays.at("gain_dB").size() == 3);
    REQUIRE(arrays.at("nf_dB").size() == 3);
    for (std::size_t sample = 0; sample < 3; ++sample) {
        const std::size_t index = sample * 4;
        CHECK(arrays.at("frequencies_Hz")[sample] == direct.sweepFrequencies()[index]);
        CHECK(arrays.at("gain_dB")[sample] == direct.gainDb()[index]);
        CHECK(arrays.at("nf_dB")[sample] == direct.noiseFigureDb()[index]);
    }
    CHECK(result.structured.at("summary").at("valid_points") == 9);
    CHECK(result.structured.at("summary").at("gain_dB").at("at_center") ==
          Catch::Approx(direct.gainDb()[4]).margin(1e-9));
    CHECK(result.structured.at("summary").at("nf_dB").at("at_center") ==
          Catch::Approx(direct.noiseFigureDb()[4]).margin(1e-9));
}
TEST_CASE("network analyzer sweep safely handles finite frequencies beyond bucket cells",
          "[agent_api][network_analyzer_sweep]") {
    ApiFixture fixture;
    auto *generator = fixture.add<SignalGeneratorEngine>("generator");
    auto *amplifier = fixture.add<AmplifierEngine>("amplifier");
    REQUIRE(generator);
    REQUIRE(amplifier);
    REQUIRE(fixture.commands.connect(generator->outputPinId(), amplifier->inputPinId()));
    const auto result = fixture.call("network_analyzer_sweep",
                                     {{"epoch", fixture.api.epoch()},
                                      {"point_a", {{"component", generator->id()}, {"port", 0}}},
                                      {"point_b", {{"component", amplifier->id()}, {"port", 0}}},
                                      {"start_Hz", 1e20},
                                      {"stop_Hz", 1.1e20},
                                      {"points", 5}});
    REQUIRE(errorFor(result).at("code") == "NO_MEASUREMENT");
    CHECK(result.structured.at("epoch") == fixture.api.epoch());
    REQUIRE(errorFor(result).contains("details"));
    CHECK(errorFor(result).at("details").at("reason") == "NO_VALID_POINTS");
    const auto &frequencies = fixture.network_analyzer.sweepFrequencies();
    REQUIRE(frequencies.size() == 5);
    CHECK(std::all_of(frequencies.begin(), frequencies.end(),
                      [](double frequency) { return std::isfinite(frequency); }));
}

TEST_CASE("network analyzer settings checkpoint only changes and return engine-clamped points",
          "[agent_api][network_analyzer_sweep]") {
    ApiFixture fixture;
    auto *generator = fixture.add<SignalGeneratorEngine>("generator");
    auto *amplifier = fixture.add<AmplifierEngine>("amplifier");
    REQUIRE(generator);
    REQUIRE(amplifier);
    REQUIRE(fixture.commands.connect(generator->outputPinId(), amplifier->inputPinId()));
    const auto arguments = [&](int points) {
        return nlohmann::ordered_json{{"epoch", fixture.api.epoch()},
                                      {"point_a", {{"component", generator->id()}, {"port", 0}}},
                                      {"point_b", {{"component", amplifier->id()}, {"port", 0}}},
                                      {"points", points}};
    };
    const auto before_revision = fixture.commands.revision();
    const auto changed = fixture.call("network_analyzer_sweep", arguments(13));
    REQUIRE_FALSE(changed.is_error);
    CHECK(changed.structured.at("settings").at("points") == 13);
    CHECK(fixture.commands.revision() == before_revision + 1);
    CHECK(fixture.host.checkpoint_begins == 1);
    CHECK(fixture.host.checkpoint_commits == 1);
    const auto unchanged = fixture.call("network_analyzer_sweep", arguments(13));
    REQUIRE_FALSE(unchanged.is_error);
    CHECK(fixture.commands.revision() == before_revision + 1);
    CHECK(fixture.host.checkpoint_begins == 1);
    CHECK(fixture.host.checkpoint_commits == 1);
    for (const auto &[requested, stored] :
         std::array<std::pair<int, int>, 3>{{{-3, 2}, {1, 2}, {5000, 2001}}}) {
        const auto clamped = fixture.call("network_analyzer_sweep", arguments(requested));
        REQUIRE_FALSE(clamped.is_error);
        CHECK(clamped.structured.at("settings").at("points") == stored);
        CHECK(fixture.network_analyzer.points() == stored);
    }
}

TEST_CASE("network_analyzer_sweep distinguishes missing path from invalid sweep points",
          "[agent_api][network_analyzer_sweep]") {
    ApiFixture fixture;
    auto *first = fixture.add<SignalGeneratorEngine>("generator");
    auto *second = fixture.add<SignalGeneratorEngine>("generator");
    REQUIRE(first);
    REQUIRE(second);
    const auto args = [&](int a, int b) {
        return nlohmann::ordered_json{{"epoch", fixture.api.epoch()},
                                      {"point_a", {{"component", a}, {"port", 0}}},
                                      {"point_b", {{"component", b}, {"port", 0}}}};
    };
    const auto no_path = fixture.call("network_analyzer_sweep", args(first->id(), second->id()));
    REQUIRE(errorFor(no_path).at("code") == "NO_MEASUREMENT");
    CHECK(no_path.structured.at("epoch") == fixture.api.epoch());
    REQUIRE(errorFor(no_path).contains("details"));
    CHECK(errorFor(no_path).at("details").at("reason") == "NO_PATH");
}
TEST_CASE("network_analyzer_sweep reports a connected path with no valid points",
          "[agent_api][network_analyzer_sweep]") {
    ApiFixture no_valid_fixture;
    auto *source = no_valid_fixture.add<SignalGeneratorEngine>("generator");
    auto *filter = no_valid_fixture.add<IdealFilterEngine>("filter");
    REQUIRE(source);
    REQUIRE(filter);
    filter->setFilterType(FilterType::HPF);
    filter->setCutoff_Hz(100e6);
    REQUIRE(no_valid_fixture.commands.connect(source->outputPinId(), filter->inputPinId()));
    nlohmann::ordered_json no_valid_args{{"epoch", no_valid_fixture.api.epoch()},
                                         {"point_a", {{"component", source->id()}, {"port", 0}}},
                                         {"point_b", {{"component", filter->id()}, {"port", 0}}},
                                         {"start_Hz", 1e6},
                                         {"stop_Hz", 10e6},
                                         {"points", 5}};
    const auto no_valid = no_valid_fixture.call("network_analyzer_sweep", std::move(no_valid_args));
    REQUIRE(errorFor(no_valid).at("code") == "NO_MEASUREMENT");
    CHECK(no_valid.structured.at("epoch") == no_valid_fixture.api.epoch());
    REQUIRE(errorFor(no_valid).contains("details"));
    CHECK(errorFor(no_valid).at("details").at("reason") == "NO_VALID_POINTS");
}

TEST_CASE("Every catalog tool input example passes API argument validation", "[agent_api]") {
    ApiFixture fixture;
    for (const auto &tool : agentToolCatalog()) {
        CAPTURE(tool.name);
        REQUIRE_FALSE(tool.input_schema.at("examples").empty());
        const auto result = fixture.call(tool.name, tool.input_schema.at("examples").front());
        if (result.is_error)
            CHECK(errorFor(result).at("code") != "INVALID_ARGUMENT");
        CHECK(result.structured.contains("epoch"));
        CHECK(result.structured.at("epoch") == fixture.api.epoch());
    }
}

TEST_CASE("Measurement results carry epochs and missing endpoints return NOT_FOUND",
          "[agent_api][measure]") {
    ApiFixture fixture;
    auto *generator = fixture.add<SignalGeneratorEngine>("generator");
    REQUIRE(generator);
    const auto success =
        fixture.call("measure_port", {{"epoch", fixture.api.epoch()},
                                      {"at", {{"component", generator->id()}, {"port", 0}}}});
    CHECK_FALSE(success.is_error);
    CHECK(success.structured.at("epoch") == fixture.api.epoch());
    const auto missing_component =
        fixture.call("measure_port",
                     {{"epoch", fixture.api.epoch()}, {"at", {{"component", 99999}, {"port", 0}}}});
    CHECK(missing_component.is_error);
    CHECK(errorFor(missing_component).at("code") == "NOT_FOUND");
    CHECK(missing_component.structured.at("epoch") == fixture.api.epoch());
    const auto missing_port =
        fixture.call("measure_port", {{"epoch", fixture.api.epoch()},
                                      {"at", {{"component", generator->id()}, {"port", 1}}}});
    CHECK(missing_port.is_error);
    CHECK(errorFor(missing_port).at("code") == "NOT_FOUND");
    CHECK(missing_port.structured.at("epoch") == fixture.api.epoch());
}

TEST_CASE("set_params rejects a partial tone element and leaves the generator unchanged",
          "[agent_api][circuit_edit]") {
    ApiFixture fixture;
    auto *generator = fixture.add<SignalGeneratorEngine>("generator");
    REQUIRE(generator != nullptr);
    generator->addTone(1.1e9, -14.0);
    const auto before = generator->serialize();
    const auto revision = fixture.commands.revision();

    const auto result = editCall(
        fixture,
        nlohmann::ordered_json::array(
            {{{"op", "set_params"},
              {"component", generator->id()},
              {"params", {{"tones", nlohmann::ordered_json::array({{{"freq_Hz", 3e9}}})}}}}}));
    REQUIRE(result.is_error);
    CHECK(result.structured.at("epoch") == fixture.api.epoch());
    CHECK(errorFor(result).at("code") == "PARAM_REJECTED");
    CHECK(errorFor(result).at("details").at("reason") == "TYPE_MISMATCH");
    CHECK(errorFor(result).at("details").at("path") == "tones[0]");
    CHECK(fixture.commands.revision() == revision);
    CHECK(generator->serialize() == before);
}

namespace {

std::filesystem::path dataFileScratch(const std::string &name) {
    const auto dir = std::filesystem::temp_directory_path() /
                     ("data_file_read_" + name + "_" + test_temp_paths::processTag());
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

void writeDataFileText(const std::filesystem::path &path, const std::string &text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

std::string admS2pPath() {
    return std::string(PROJECT_SOURCE_DIR) +
           "/component_data/amplifiers/adm-3844psm/ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
}

std::string am1143S2pPath() {
    return std::string(PROJECT_SOURCE_DIR) +
           "/component_data/library/amplifiers/anatech/AM1143.s2p";
}

} // namespace

TEST_CASE("data_file_read summarizes a component's S-parameter file",
          "[agent_api][data_file_read]") {
    ApiFixture fixture;
    auto *amplifier = fixture.add<AmplifierEngine>("amplifier");
    REQUIRE(amplifier != nullptr);
    amplifier->deserialize({{"sparam_mode", true}, {"sparam_filepath", admS2pPath()}});
    const auto parsed = TouchstoneParser::parse(admS2pPath());
    REQUIRE(parsed.has_value());

    const auto result = fixture.call(
        "data_file_read", {{"epoch", fixture.api.epoch()}, {"component", amplifier->id()}});
    REQUIRE_FALSE(result.is_error);
    const auto &data = result.structured;
    CHECK(data.at("epoch") == fixture.api.epoch());
    CHECK(data.at("source") == "component");
    CHECK(data.at("file") == "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p");
    CHECK(data.at("ports") == parsed->num_ports);
    CHECK(data.at("points") == parsed->frequencies.size());
    CHECK(data.at("reference_impedance_ohm") == parsed->reference_impedance);
    CHECK(data.at("frequency_range_Hz").at("min") == parsed->frequencies.front());
    CHECK(data.at("frequency_range_Hz").at("max") == parsed->frequencies.back());
    CHECK(data.at("s_parameter").at("row") == 1);
    CHECK(data.at("s_parameter").at("col") == 0);
    const auto &samples = data.at("samples");
    REQUIRE(samples.size() == std::min<std::size_t>(201, parsed->frequencies.size()));
    CHECK(samples[0].at("freq_Hz") == parsed->frequencies.front());
    const double s21_dB = 20.0 * std::log10(std::abs(parsed->parameters.front()[2]));
    CHECK(samples[0].at("magnitude_dB").get<double>() == Catch::Approx(s21_dB));
}

TEST_CASE("data_file_read library route matches the component route on the same file",
          "[agent_api][data_file_read]") {
    ApiFixture fixture;
    const auto library = fixture.call("data_file_read", {{"part_number", "AM1143"}});
    REQUIRE_FALSE(library.is_error);
    CHECK(library.structured.at("epoch") == fixture.api.epoch());
    CHECK(library.structured.at("source") == "library");
    CHECK(library.structured.at("part_number") == "AM1143");
    CHECK(library.structured.at("file") == "AM1143.s2p");

    auto *amplifier = fixture.add<AmplifierEngine>("amplifier");
    REQUIRE(amplifier != nullptr);
    amplifier->deserialize({{"sparam_mode", true}, {"sparam_filepath", am1143S2pPath()}});
    const auto component = fixture.call(
        "data_file_read", {{"epoch", fixture.api.epoch()}, {"component", amplifier->id()}});
    REQUIRE_FALSE(component.is_error);
    CHECK(component.structured.at("samples") == library.structured.at("samples"));
}

TEST_CASE("data_file_read requires exactly one source, and an epoch with a component",
          "[agent_api][data_file_read]") {
    ApiFixture fixture;
    const auto neither = fixture.call("data_file_read", {{"epoch", fixture.api.epoch()}});
    REQUIRE(errorFor(neither).at("code") == "INVALID_ARGUMENT");
    CHECK(errorFor(neither).at("details").at("path") == "/");

    const auto both =
        fixture.call("data_file_read",
                     {{"epoch", fixture.api.epoch()}, {"component", 1}, {"part_number", "AM1143"}});
    REQUIRE(errorFor(both).at("code") == "INVALID_ARGUMENT");
    CHECK(errorFor(both).at("details").at("path") == "/");

    const auto no_epoch = fixture.call("data_file_read", {{"component", 1}});
    REQUIRE(errorFor(no_epoch).at("code") == "INVALID_ARGUMENT");
    CHECK(errorFor(no_epoch).at("details").at("path") == "/epoch");
}

TEST_CASE("data_file_read rejects s_row and s_col outside the file's port count",
          "[agent_api][data_file_read]") {
    ApiFixture fixture;
    auto *amplifier = fixture.add<AmplifierEngine>("amplifier");
    REQUIRE(amplifier != nullptr);
    amplifier->deserialize({{"sparam_mode", true}, {"sparam_filepath", admS2pPath()}});
    const auto row = fixture.call(
        "data_file_read",
        {{"epoch", fixture.api.epoch()}, {"component", amplifier->id()}, {"s_row", 2}});
    REQUIRE(errorFor(row).at("code") == "INVALID_ARGUMENT");
    CHECK(errorFor(row).at("details").at("path") == "/s_row");
    CHECK(errorFor(row).at("hint") == "file has 2 ports");
    const auto column = fixture.call(
        "data_file_read",
        {{"epoch", fixture.api.epoch()}, {"component", amplifier->id()}, {"s_col", 2}});
    REQUIRE(errorFor(column).at("code") == "INVALID_ARGUMENT");
    CHECK(errorFor(column).at("details").at("path") == "/s_col");
}

TEST_CASE("data_file_read refuses a component without a usable S-parameter file",
          "[agent_api][data_file_read]") {
    ApiFixture fixture;
    auto *amplifier = fixture.add<AmplifierEngine>("amplifier");
    REQUIRE(amplifier != nullptr);
    const auto read_component = [&]() {
        return fixture.call("data_file_read",
                            {{"epoch", fixture.api.epoch()}, {"component", amplifier->id()}});
    };
    REQUIRE(errorFor(read_component()).at("code") == "NOT_FOUND");

    amplifier->deserialize({{"sparam_filepath", "relative.s2p"}});
    REQUIRE(errorFor(read_component()).at("code") == "NOT_FOUND");

    const auto dir = dataFileScratch("missing");
    amplifier->deserialize(
        {{"sparam_mode", true}, {"sparam_filepath", (dir / "missing.s2p").string()}});
    REQUIRE(errorFor(read_component()).at("code") == "NOT_FOUND");
}

TEST_CASE("data_file_read refuses non-S-parameter and unparseable component files",
          "[agent_api][data_file_read]") {
    ApiFixture fixture;
    auto *amplifier = fixture.add<AmplifierEngine>("amplifier");
    REQUIRE(amplifier != nullptr);
    const auto read_component = [&]() {
        return fixture.call("data_file_read",
                            {{"epoch", fixture.api.epoch()}, {"component", amplifier->id()}});
    };
    const auto dir = dataFileScratch("refusals");

    const auto admittance = dir / "admittance.s2p";
    writeDataFileText(admittance, "# HZ Y RI R 50\n1000000000 0.5 0 0 0 0.5 0 0.5 0\n");
    amplifier->deserialize({{"sparam_mode", true}, {"sparam_filepath", admittance.string()}});
    REQUIRE(errorFor(read_component()).at("code") == "NOT_FOUND");

    const auto garbage = dir / "garbage.s2p";
    writeDataFileText(garbage, "# HZ S RI R 50\n1000000000 abc def\n");
    REQUIRE_FALSE(TouchstoneParser::parse(garbage.string()).has_value());
    amplifier->deserialize({{"sparam_mode", true}, {"sparam_filepath", garbage.string()}});
    REQUIRE(errorFor(read_component()).at("code") == "INTERNAL");
}

TEST_CASE("data_file_read encodes a zero-magnitude sample as null", "[agent_api][data_file_read]") {
    ApiFixture fixture;
    const auto path = dataFileScratch("zero") / "zero_s21.s2p";
    // Touchstone 2-port order is S11, S21, S12, S22, so the second pair is S21 and is zero.
    writeDataFileText(path, "# HZ S RI R 50\n1000000000 0.5 0 0 0 0.5 0 0.5 0\n");
    auto *amplifier = fixture.add<AmplifierEngine>("amplifier");
    REQUIRE(amplifier != nullptr);
    amplifier->deserialize({{"sparam_mode", true}, {"sparam_filepath", path.string()}});

    const auto result = fixture.call(
        "data_file_read", {{"epoch", fixture.api.epoch()}, {"component", amplifier->id()}});
    REQUIRE_FALSE(result.is_error);
    const auto &samples = result.structured.at("samples");
    REQUIRE(samples.size() == 1);
    CHECK(samples[0].at("magnitude_dB").is_null());
    CHECK(samples[0].at("phase_deg") == 0.0);
}

TEST_CASE("data_file_read is read-only", "[agent_api][data_file_read]") {
    ApiFixture fixture;
    auto *amplifier = fixture.add<AmplifierEngine>("amplifier");
    REQUIRE(amplifier != nullptr);
    amplifier->deserialize({{"sparam_mode", true}, {"sparam_filepath", admS2pPath()}});
    const auto before = amplifier->serialize();
    const auto revision = fixture.commands.revision();
    const auto dirty = fixture.commands.isDirty();

    const auto result = fixture.call(
        "data_file_read", {{"epoch", fixture.api.epoch()}, {"component", amplifier->id()}});
    REQUIRE_FALSE(result.is_error);
    CHECK(amplifier->serialize() == before);
    CHECK(fixture.commands.revision() == revision);
    CHECK(fixture.commands.isDirty() == dirty);
}

TEST_CASE("data_file_read reports stale epochs with replacement details",
          "[agent_api][data_file_read]") {
    ApiFixture fixture;
    auto *amplifier = fixture.add<AmplifierEngine>("amplifier");
    REQUIRE(amplifier != nullptr);
    amplifier->deserialize({{"sparam_mode", true}, {"sparam_filepath", admS2pPath()}});

    const auto stale = fixture.call(
        "data_file_read", {{"epoch", fixture.api.epoch() + 1}, {"component", amplifier->id()}});
    REQUIRE(errorFor(stale).at("code") == "STALE_EPOCH");
    CHECK(errorFor(stale).at("details").at("epoch") == fixture.api.epoch());
}

TEST_CASE("data_file_read refuses a library path loaded from disk that escapes its directory",
          "[agent_api][data_file_read]") {
    ApiFixture fixture;
    const auto root = dataFileScratch("escape_file");
    const auto lib_dir = root / "library";
    std::filesystem::create_directories(lib_dir);
    writeDataFileText(root / "decoy.s2p", "# HZ S RI R 50\n1000000000 0.5 0 0 0 0.5 0 0.5 0\n");
    nlohmann::json library_json;
    library_json["schema_version"] = 2;
    library_json["type"] = "amplifier";
    library_json["part_number"] = "DFR-ESCAPE";
    library_json["parameters"]["gain_dB"] = 20.0;
    library_json["parameters"]["nf_dB"] = 1.0;
    library_json["data_files"] = nlohmann::json::array();
    library_json["data_files"].push_back({{"type", "s_parameters"}, {"path", "../decoy.s2p"}});
    writeDataFileText(lib_dir / "escape.json", library_json.dump(2));
    fixture.library.loadFile((lib_dir / "escape.json").string());

    const auto result = fixture.call(
        "data_file_read", {{"epoch", fixture.api.epoch()}, {"part_number", "DFR-ESCAPE"}});
    REQUIRE(errorFor(result).at("code") == "NOT_FOUND");
}

TEST_CASE("data_file_read refuses an escaping library path resolved in memory",
          "[agent_api][data_file_read]") {
    ApiFixture fixture;
    const auto root = dataFileScratch("escape_memory");
    const auto lib_dir = root / "library";
    std::filesystem::create_directories(lib_dir);
    writeDataFileText(root / "decoy.s2p", "# HZ S RI R 50\n1000000000 0.5 0 0 0 0.5 0 0.5 0\n");
    auto all = fixture.library.all();
    const auto am1143 = std::find_if(
        all.begin(), all.end(), [](const auto *part) { return part->part_number == "AM1143"; });
    REQUIRE(am1143 != all.end());
    ComponentDefinition escaping = **am1143;
    escaping.part_number = "DFR-ESCAPE-MEMORY";
    escaping.source_path = (lib_dir / "escape-memory.json").string();
    escaping.data_files = {{"s_parameters", "../decoy.s2p"}};
    fixture.library.upsert(escaping);

    const auto result = fixture.call(
        "data_file_read", {{"epoch", fixture.api.epoch()}, {"part_number", "DFR-ESCAPE-MEMORY"}});
    REQUIRE(errorFor(result).at("code") == "NOT_FOUND");
}

TEST_CASE("data_file_read reports unknown library parts", "[agent_api][data_file_read]") {
    ApiFixture fixture;
    const auto unknown = fixture.call(
        "data_file_read", {{"epoch", fixture.api.epoch()}, {"part_number", "NO-SUCH-PART"}});
    REQUIRE(errorFor(unknown).at("code") == "UNKNOWN_PART");
}

TEST_CASE("data_file_read reports ambiguous library parts", "[agent_api][data_file_read]") {
    ApiFixture fixture;
    auto all = fixture.library.all();
    const auto am1143 = std::find_if(
        all.begin(), all.end(), [](const auto *part) { return part->part_number == "AM1143"; });
    REQUIRE(am1143 != all.end());
    ComponentDefinition duplicate = **am1143;
    duplicate.source_path += ".duplicate";
    duplicate.manufacturer = "Duplicate vendor";
    fixture.library.upsert(duplicate);

    const auto ambiguous =
        fixture.call("data_file_read", {{"epoch", fixture.api.epoch()}, {"part_number", "AM1143"}});
    REQUIRE(errorFor(ambiguous).at("code") == "AMBIGUOUS_PART");
}

TEST_CASE("data_file_read caps samples at max_points and keeps the first and last",
          "[agent_api][data_file_read]") {
    ApiFixture fixture;
    auto *amplifier = fixture.add<AmplifierEngine>("amplifier");
    REQUIRE(amplifier != nullptr);
    amplifier->deserialize({{"sparam_mode", true}, {"sparam_filepath", admS2pPath()}});
    const auto parsed = TouchstoneParser::parse(admS2pPath());
    REQUIRE(parsed.has_value());

    const auto result = fixture.call(
        "data_file_read",
        {{"epoch", fixture.api.epoch()}, {"component", amplifier->id()}, {"max_points", 2}});
    REQUIRE_FALSE(result.is_error);
    const auto &samples = result.structured.at("samples");
    REQUIRE(samples.size() == 2);
    CHECK(samples[0].at("freq_Hz") == parsed->frequencies.front());
    CHECK(samples[1].at("freq_Hz") == parsed->frequencies.back());
}

TEST_CASE("data_file_read rejects a negative component id", "[agent_api][data_file_read]") {
    ApiFixture fixture;
    const auto result =
        fixture.call("data_file_read", {{"epoch", fixture.api.epoch()}, {"component", -1}});
    REQUIRE(errorFor(result).at("code") == "INVALID_ARGUMENT");
    CHECK(errorFor(result).at("details").at("path") == "/component");
}
