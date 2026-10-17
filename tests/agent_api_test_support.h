#pragma once

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
#include "flow_boundary.h"
#include "flow_metrics.h"
#include "output_snr.h"
#include "power_meter_engine.h"
#include "receiver_performance_measurement.h"
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
#include <span>

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
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
    std::array<float, 2> nodeEditorSpacePosition(int graph_node_id) const override {
        const auto found = node_positions.find(graph_node_id);
        return found == node_positions.end() ? std::array<float, 2>{0.0f, 0.0f} : found->second;
    }
    bool setNodeEditorSpacePosition(int graph_node_id, std::array<float, 2> position) override {
        if (nodeEditorSpacePosition(graph_node_id) == position)
            return false;
        node_positions[graph_node_id] = position;
        return true;
    }
    bool appModalOpen() const override { return modal_open; }
    std::optional<std::string> projectName() const override { return name; }
    std::optional<std::string> projectPath() const override { return open_path; }
    bool saveProjectFile(const std::string &path) override {
        saved_paths.push_back(path);
        if (!save_succeeds)
            return false;
        open_path = path;
        return true;
    }
    AgentProjectLoadResult loadProjectFile(const std::string &path) override {
        loaded_paths.push_back(path);
        return load_outcome(path);
    }
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
    std::optional<std::string> open_path;
    bool save_succeeds = true;
    std::vector<std::string> saved_paths;
    std::vector<std::string> loaded_paths;
    std::function<AgentProjectLoadResult(const std::string &)> load_outcome =
        [](const std::string &) { return AgentProjectLoadResult{true, false}; };
    int checkpoint_begins = 0;
    int checkpoint_commits = 0;
    int checkpoint_discards = 0;
    std::vector<AgentPlacement> last_placements;
    std::vector<AgentActivity> activities;
    std::string last_checkpoint_summary;
    std::unordered_map<int, std::array<float, 2>> node_positions;
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
