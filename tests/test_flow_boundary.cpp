// Standalone, UI-free coverage for the run boundary shared by the Test Flow panel and the
// agent tool: snapshot every engine, run the flow, restore each snapshot independently, then
// rewire. Each failure mode is its own status, and a restore failure is never silent.
#include "component_engine_base.h"
#include "component_registry.h"
#include "flow_boundary.h"
#include "flow_types.h"
#include "node_graph_engine.h"
#include "view_manager.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

// A component that accepts a swept value but throws when its own baseline is
// written back, so execution succeeds and only restoration fails.
class BaselineRejectingEngine final : public ComponentEngineBase {
  public:
    BaselineRejectingEngine(int id, NodeGraphEngine &graph, double baseline)
        : ComponentEngineBase(id, graph, "BaselineRejector", 0, 1), m_baseline(baseline),
          m_value(baseline) {}

    std::string_view type_name() const override { return "baseline_rejector"; }
    std::string hoverSummary() const override { return "BaselineRejector"; }
    void update(double) override {}

    nlohmann::json serialize() const override { return {{"value", m_value}}; }
    void deserialize(const nlohmann::json &snapshot) override {
        ++m_deserialize_calls;
        const double value = snapshot.at("value").get<double>();
        if (value == m_baseline)
            throw std::runtime_error("intentional baseline restore failure");
        m_value = value;
    }

    double value() const { return m_value; }
    int deserializeCalls() const { return m_deserialize_calls; }

  private:
    double m_baseline;
    double m_value;
    int m_deserialize_calls = 0;
};

// A well-behaved neighbour, declared after the rejecting engine so its successful
// restoration proves the restore loop continued past the failure.
class TrackingEngine final : public ComponentEngineBase {
  public:
    TrackingEngine(int id, NodeGraphEngine &graph, double initial)
        : ComponentEngineBase(id, graph, "Tracking", 0, 1), m_value(initial) {}

    std::string_view type_name() const override { return "tracking"; }
    std::string hoverSummary() const override { return "Tracking"; }
    void update(double) override {}

    nlohmann::json serialize() const override { return {{"value", m_value}}; }
    void deserialize(const nlohmann::json &snapshot) override {
        ++m_deserialize_calls;
        m_value = snapshot.at("value").get<double>();
    }

    double value() const { return m_value; }
    int deserializeCalls() const { return m_deserialize_calls; }

  private:
    double m_value;
    int m_deserialize_calls = 0;
};

class ThrowingUpdateEngine final : public ComponentEngineBase {
  public:
    ThrowingUpdateEngine(int id, NodeGraphEngine &graph)
        : ComponentEngineBase(id, graph, "ThrowingUpdate", 0, 1) {}
    std::string_view type_name() const override { return "throwing_update"; }
    std::string hoverSummary() const override { return "ThrowingUpdate"; }
    void update(double) override { throw std::runtime_error("update failed"); }
    nlohmann::json serialize() const override { return {{"value", 0.0}}; }
    void deserialize(const nlohmann::json &) override {}
};

class ThrowingSnapshotEngine final : public ComponentEngineBase {
  public:
    ThrowingSnapshotEngine(int id, NodeGraphEngine &graph)
        : ComponentEngineBase(id, graph, "ThrowingSnapshot", 0, 1) {}
    std::string_view type_name() const override { return "throwing_snapshot"; }
    std::string hoverSummary() const override { return "ThrowingSnapshot"; }
    void update(double) override {}
    nlohmann::json serialize() const override { throw std::runtime_error("snapshot failed"); }
    void deserialize(const nlohmann::json &) override {}
};

FlowSpec probeSpec(std::vector<Condition> conditions, int component) {
    FlowSpec spec;
    spec.name = "boundary";
    spec.conditions = std::move(conditions);
    spec.measure = {Measurement{component, 0, "power_dBm"}};
    return spec;
}

} // namespace

TEST_CASE("A flow without conditions measures one row and leaves the circuit as it was",
          "[test_flow][boundary]") {
    NodeGraphEngine graph;
    ViewManager view;
    ComponentRegistry components{graph, view};
    auto &tracking = components.add<TrackingEngine>(1, graph, 5.0);
    const auto before = tracking.serialize();

    const auto outcome = RunFlowWithinBoundary(probeSpec({}, 1), components.all(), graph);

    REQUIRE(outcome.status == BoundaryStatus::Completed);
    REQUIRE(outcome.result.ok);
    CHECK(outcome.result.rows.size() == 1);
    CHECK(outcome.message.empty());
    CHECK(tracking.serialize() == before);
    CHECK(tracking.deserializeCalls() == 1);
}

TEST_CASE("A restore failure is reported and later engines are still restored",
          "[test_flow][boundary]") {
    NodeGraphEngine graph;
    ViewManager view;
    ComponentRegistry components{graph, view};
    auto &rejecting = components.add<BaselineRejectingEngine>(1, graph, 5.0);
    auto &tracking = components.add<TrackingEngine>(2, graph, 5.0);

    const auto outcome = RunFlowWithinBoundary(probeSpec({Condition{1, "value", {2.0}}}, 2),
                                               components.all(), graph);

    REQUIRE(outcome.status == BoundaryStatus::RestoreFailed);
    CHECK(outcome.message.find("component 1:") != std::string::npos);
    CHECK(tracking.deserializeCalls() == 1);
    CHECK(rejecting.value() == 2.0);
}

TEST_CASE("An execution failure is reported and the circuit is still restored",
          "[test_flow][boundary]") {
    NodeGraphEngine graph;
    ViewManager view;
    ComponentRegistry components{graph, view};
    components.add<ThrowingUpdateEngine>(1, graph);
    auto &tracking = components.add<TrackingEngine>(2, graph, 5.0);

    const auto outcome = RunFlowWithinBoundary(probeSpec({}, 2), components.all(), graph);

    REQUIRE(outcome.status == BoundaryStatus::ExecutionFailed);
    CHECK(outcome.message == "update failed");
    CHECK(tracking.deserializeCalls() == 1);
}

TEST_CASE("A snapshot failure refuses the run before anything executes", "[test_flow][boundary]") {
    NodeGraphEngine graph;
    ViewManager view;
    ComponentRegistry components{graph, view};
    components.add<ThrowingSnapshotEngine>(1, graph);
    auto &tracking = components.add<TrackingEngine>(2, graph, 5.0);

    const auto outcome = RunFlowWithinBoundary(probeSpec({}, 2), components.all(), graph);

    REQUIRE(outcome.status == BoundaryStatus::SnapshotFailed);
    CHECK(outcome.message == "snapshot failed");
    CHECK(tracking.deserializeCalls() == 0);
}
