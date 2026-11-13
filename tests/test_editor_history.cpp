#include "circuit_runtime.h"
#include "component_type_registry.h"
#include "editor_history.h"
#include "graph_editor_actions.h"
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

const ComponentTypeDescriptor &descriptor(std::string_view type) {
    const auto *desc = ComponentTypeRegistry::instance().find(type);
    REQUIRE(desc != nullptr);
    return *desc;
}

const GraphNode *findNode(const CircuitRuntime &runtime, int node_id) {
    for (const GraphNode &node : runtime.graph().nodes()) {
        if (node.node_id == node_id)
            return &node;
    }
    return nullptr;
}

// LayoutPoint is compared by exact value here and in the delta implementation.
bool operator==(const LayoutPoint &lhs, const LayoutPoint &rhs) {
    return lhs.x == rhs.x && lhs.y == rhs.y;
}

ComponentState componentState(int component_id, std::string type_name, std::vector<int> input_pins,
                              std::vector<int> output_pins, nlohmann::json state,
                              std::string part_number) {
    return ComponentState{component_id,           std::move(type_name), std::move(input_pins),
                          std::move(output_pins), std::move(state),     std::move(part_number)};
}

// A fully populated snapshot, so a missing diff class cannot pass unnoticed.
EditorSnapshot sampleSnapshot() {
    EditorSnapshot snapshot;
    snapshot.components[10] =
        componentState(1, "amplifier", {100}, {101}, {{"gain_dB", 0.0}}, "AMP-1");
    snapshot.components[11] =
        componentState(2, "generator", {}, {102}, nlohmann::json::object(), "GEN-1");
    snapshot.links.insert(LinkEnds{101, 103});
    snapshot.links.insert(LinkEnds{103, 104});
    snapshot.probes = {101, 102};
    snapshot.groups[3] = GroupState{"Chain", {10, 11}, false};
    snapshot.positions[10] = LayoutPoint{240.0f, 96.0f};
    snapshot.positions[11] = LayoutPoint{0.0f, 0.0f};
    snapshot.app_state["receiver_requirements"] = nlohmann::json{{"nf_dB", 2.5}};
    snapshot.app_state["rf_budget"] = nlohmann::json{{"stages", nlohmann::json::array()}};
    return snapshot;
}

// Two snapshots that differ in every key class: a component added, removed, and changed; a link
// removed and added; a probe reorder; groups, positions, and app state changed, added, and
// removed.
std::pair<EditorSnapshot, EditorSnapshot> changedSnapshotPair() {
    EditorSnapshot before = sampleSnapshot();
    EditorSnapshot after = sampleSnapshot();

    after.components.erase(11);
    after.components[12] =
        componentState(3, "mixer", {105, 106}, {107}, nlohmann::json::object(), "MIX-1");
    after.components[10].state = {{"gain_dB", 12.0}};

    after.links.erase(LinkEnds{101, 103});
    after.links.insert(LinkEnds{101, 105});

    after.probes = {102, 101};

    after.groups[3].collapsed = true;
    after.groups[4] = GroupState{"Other", {12}, false};

    after.positions[10] = LayoutPoint{256.0f, 112.0f};
    after.positions.erase(11);
    after.positions[12] = LayoutPoint{32.0f, 8.0f};

    after.app_state["receiver_requirements"] = nlohmann::json{{"nf_dB", 3.5}};
    after.app_state.erase("rf_budget");
    after.app_state["na_points"] = nlohmann::json::array({1, 2});
    return {before, after};
}

template <class V> bool sameChange(const Change<V> &lhs, const Change<V> &rhs) {
    if (lhs.before.has_value() != rhs.before.has_value() ||
        lhs.after.has_value() != rhs.after.has_value())
        return false;
    if (lhs.before.has_value() && !(*lhs.before == *rhs.before))
        return false;
    return !lhs.after.has_value() || *lhs.after == *rhs.after;
}

template <class Key, class V>
bool sameChanges(const std::map<Key, Change<V>> &lhs, const std::map<Key, Change<V>> &rhs) {
    if (lhs.size() != rhs.size())
        return false;
    for (auto left = lhs.begin(), right = rhs.begin(); left != lhs.end(); ++left, ++right) {
        if (!(left->first == right->first) || !sameChange(left->second, right->second))
            return false;
    }
    return true;
}

bool sameDelta(const EditorDelta &lhs, const EditorDelta &rhs) {
    if (!sameChanges(lhs.components, rhs.components))
        return false;
    if (lhs.links_removed != rhs.links_removed || lhs.links_added != rhs.links_added)
        return false;
    if (lhs.probes.has_value() != rhs.probes.has_value())
        return false;
    if (lhs.probes.has_value() && !sameChange(*lhs.probes, *rhs.probes))
        return false;
    return sameChanges(lhs.groups, rhs.groups) && sameChanges(lhs.positions, rhs.positions) &&
           sameChanges(lhs.app_state, rhs.app_state);
}

} // namespace

TEST_CASE("restoreComponent rebuilds a removed component under its original ids",
          "[editor_history][restore]") {
    CircuitRuntime runtime;

    auto *generator = runtime.createComponent(descriptor("generator").create);
    auto *amplifier = runtime.createComponent(descriptor("amplifier").create);
    REQUIRE(generator != nullptr);
    REQUIRE(amplifier != nullptr);

    const int component_id = amplifier->id();
    const int graph_node_id = amplifier->graphNodeId();
    const GraphNode *before = findNode(runtime, graph_node_id);
    REQUIRE(before != nullptr);
    const std::vector<int> input_pins = before->input_pin_ids;
    const std::vector<int> output_pins = before->output_pin_ids;
    REQUIRE_FALSE(input_pins.empty());
    REQUIRE_FALSE(output_pins.empty());

    REQUIRE(runtime.removeComponent(graph_node_id));
    // Advance every counter past the removed component, so restoring is the
    // only way the original ids come back.
    REQUIRE(runtime.createComponent(descriptor("mixer").create) != nullptr);

    const int next_node_before = runtime.graph().nextNodeId();
    const int next_pin_before = runtime.graph().nextPinId();

    IComponentEngine *restored = runtime.restoreComponent(
        descriptor("amplifier").create, component_id, graph_node_id, input_pins, output_pins);

    REQUIRE(restored != nullptr);
    CHECK(restored->id() == component_id);
    CHECK(restored->graphNodeId() == graph_node_id);
    const GraphNode *after = findNode(runtime, graph_node_id);
    REQUIRE(after != nullptr);
    CHECK(after->input_pin_ids == input_pins);
    CHECK(after->output_pin_ids == output_pins);
    CHECK(runtime.graph().nextNodeId() == next_node_before);
    CHECK(runtime.graph().nextPinId() == next_pin_before);
}

TEST_CASE("restoreComponent refuses a live id", "[editor_history][restore]") {
    CircuitRuntime runtime;

    auto *generator = runtime.createComponent(descriptor("generator").create);
    REQUIRE(generator != nullptr);

    const std::size_t count_before = runtime.components().size();
    const int next_component_before = runtime.nextComponentId();
    const int next_node_before = runtime.graph().nextNodeId();
    const int next_pin_before = runtime.graph().nextPinId();

    IComponentEngine *restored =
        runtime.restoreComponent(descriptor("amplifier").create, runtime.nextComponentId(),
                                 generator->graphNodeId(), {}, {});

    CHECK(restored == nullptr);
    CHECK(runtime.components().size() == count_before);
    CHECK(runtime.nextComponentId() == next_component_before);
    CHECK(runtime.graph().nextNodeId() == next_node_before);
    CHECK(runtime.graph().nextPinId() == next_pin_before);
}

TEST_CASE("restoreGroup recreates a group under its id", "[editor_history][restore]") {
    CircuitRuntime runtime;
    GraphEditorActions actions(runtime);

    auto *first = runtime.createComponent(descriptor("generator").create);
    auto *second = runtime.createComponent(descriptor("amplifier").create);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);

    const int group_id =
        actions.createGroup("Chain", {first->graphNodeId(), second->graphNodeId()});
    REQUIRE(group_id >= 0);
    REQUIRE(actions.setGroupCollapsed(group_id, true));

    const Group *created = runtime.graph().groupById(group_id);
    REQUIRE(created != nullptr);
    const GroupState state{created->name, created->member_node_ids, created->collapsed};
    CHECK(state.collapsed);

    REQUIRE(actions.removeGroup(group_id));
    REQUIRE(runtime.graph().groupById(group_id) == nullptr);
    const int next_group_before = runtime.graph().nextGroupId();

    CHECK(actions.restoreGroup(group_id, state) == group_id);
    const Group *restored = runtime.graph().groupById(group_id);
    REQUIRE(restored != nullptr);
    CHECK(restored->name == state.name);
    CHECK(restored->member_node_ids == state.members);
    CHECK(restored->collapsed == state.collapsed);
    CHECK(runtime.graph().nextGroupId() == next_group_before);
}

TEST_CASE("setProbePins restores slot order", "[editor_history][restore]") {
    CircuitRuntime runtime;
    GraphEditorActions actions(runtime);

    auto *generator = runtime.createComponent(descriptor("generator").create);
    auto *amplifier = runtime.createComponent(descriptor("amplifier").create);
    REQUIRE(generator != nullptr);
    REQUIRE(amplifier != nullptr);

    const int a = generator->outputPinId();
    const int b = amplifier->inputPinId();
    const int c = amplifier->outputPinId();
    REQUIRE(actions.addProbePin(a));
    REQUIRE(actions.addProbePin(b));
    REQUIRE(actions.addProbePin(c));

    actions.setProbePins({c, a});

    CHECK(runtime.graph().probePins() == std::vector<int>{c, a});
}

TEST_CASE("restoreComponent refuses a live component id", "[editor_history][restore]") {
    CircuitRuntime runtime;

    auto *generator = runtime.createComponent(descriptor("generator").create);
    REQUIRE(generator != nullptr);

    const std::size_t count_before = runtime.components().size();
    const int next_node_before = runtime.graph().nextNodeId();
    const int next_pin_before = runtime.graph().nextPinId();
    // The graph node id is free: only the component id is live.
    const int free_node_id = runtime.graph().nextNodeId();

    IComponentEngine *restored = runtime.restoreComponent(descriptor("amplifier").create,
                                                          generator->id(), free_node_id, {}, {});

    CHECK(restored == nullptr);
    CHECK(runtime.components().size() == count_before);
    CHECK(findNode(runtime, free_node_id) == nullptr);
    CHECK(runtime.graph().nextNodeId() == next_node_before);
    CHECK(runtime.graph().nextPinId() == next_pin_before);
}

TEST_CASE("restoreComponent refuses rebuilt pin lists that differ", "[editor_history][restore]") {
    CircuitRuntime runtime;

    auto *generator = runtime.createComponent(descriptor("generator").create);
    auto *amplifier = runtime.createComponent(descriptor("amplifier").create);
    REQUIRE(generator != nullptr);
    REQUIRE(amplifier != nullptr);

    const int component_id = amplifier->id();
    const int graph_node_id = amplifier->graphNodeId();
    const GraphNode *before = findNode(runtime, graph_node_id);
    REQUIRE(before != nullptr);
    const std::vector<int> output_pins = before->output_pin_ids;
    REQUIRE_FALSE(output_pins.empty());

    REQUIRE(runtime.removeComponent(graph_node_id));
    REQUIRE(runtime.createComponent(descriptor("mixer").create) != nullptr);

    const std::size_t count_before = runtime.components().size();
    const int next_node_before = runtime.graph().nextNodeId();
    const int next_pin_before = runtime.graph().nextPinId();

    // The amplifier always builds one input pin, so an empty list cannot match.
    IComponentEngine *restored = runtime.restoreComponent(
        descriptor("amplifier").create, component_id, graph_node_id, {}, output_pins);

    CHECK(restored == nullptr);
    CHECK(findNode(runtime, graph_node_id) == nullptr);
    CHECK(runtime.components().size() == count_before);
    CHECK(runtime.graph().nextNodeId() == next_node_before);
    CHECK(runtime.graph().nextPinId() == next_pin_before);
}

TEST_CASE("restoreComponent restores the id counters when the factory throws",
          "[editor_history][restore]") {
    CircuitRuntime runtime;

    auto *generator = runtime.createComponent(descriptor("generator").create);
    auto *amplifier = runtime.createComponent(descriptor("amplifier").create);
    REQUIRE(generator != nullptr);
    REQUIRE(amplifier != nullptr);

    const int component_id = amplifier->id();
    const int graph_node_id = amplifier->graphNodeId();
    const GraphNode *before = findNode(runtime, graph_node_id);
    REQUIRE(before != nullptr);
    const std::vector<int> input_pins = before->input_pin_ids;
    const std::vector<int> output_pins = before->output_pin_ids;

    REQUIRE(runtime.removeComponent(graph_node_id));
    REQUIRE(runtime.createComponent(descriptor("mixer").create) != nullptr);

    const int next_node_before = runtime.graph().nextNodeId();
    const int next_pin_before = runtime.graph().nextPinId();
    const int next_link_before = runtime.graph().nextLinkId();

    const ComponentFactory throwing_factory = [](ComponentRegistry &, NodeGraphEngine &,
                                                 int) -> IComponentEngine * {
        throw std::runtime_error("factory failed");
    };

    CHECK_THROWS_AS(runtime.restoreComponent(throwing_factory, component_id, graph_node_id,
                                             input_pins, output_pins),
                    std::runtime_error);
    CHECK(runtime.graph().nextNodeId() == next_node_before);
    CHECK(runtime.graph().nextPinId() == next_pin_before);
    CHECK(runtime.graph().nextLinkId() == next_link_before);
}

TEST_CASE("restoreGroup recreates an expanded group as expanded", "[editor_history][restore]") {
    CircuitRuntime runtime;
    GraphEditorActions actions(runtime);

    auto *first = runtime.createComponent(descriptor("generator").create);
    auto *second = runtime.createComponent(descriptor("amplifier").create);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);

    const int group_id =
        actions.createGroup("Chain", {first->graphNodeId(), second->graphNodeId()});
    REQUIRE(group_id >= 0);
    // addGroup() creates groups collapsed, so expand before capture: the restore
    // step must apply collapsed == false, not just the creation default.
    REQUIRE(actions.setGroupCollapsed(group_id, false));

    const Group *created = runtime.graph().groupById(group_id);
    REQUIRE(created != nullptr);
    const GroupState state{created->name, created->member_node_ids, created->collapsed};
    CHECK_FALSE(state.collapsed);

    REQUIRE(actions.removeGroup(group_id));
    REQUIRE(runtime.graph().groupById(group_id) == nullptr);
    const int next_group_before = runtime.graph().nextGroupId();

    CHECK(actions.restoreGroup(group_id, state) == group_id);
    const Group *restored = runtime.graph().groupById(group_id);
    REQUIRE(restored != nullptr);
    CHECK(restored->name == state.name);
    CHECK(restored->member_node_ids == state.members);
    CHECK_FALSE(restored->collapsed);
    CHECK(runtime.graph().nextGroupId() == next_group_before);
}

TEST_CASE("Identical snapshots give an empty delta", "[editor_history][diff]") {
    const EditorSnapshot snapshot = sampleSnapshot();

    const EditorDelta delta = diffSnapshots(snapshot, snapshot);

    CHECK(delta.empty());
    CHECK(delta.components.empty());
    CHECK(delta.links_removed.empty());
    CHECK(delta.links_added.empty());
    CHECK_FALSE(delta.probes.has_value());
    CHECK(delta.groups.empty());
    CHECK(delta.positions.empty());
    CHECK(delta.app_state.empty());
}

TEST_CASE(
    "A component present only after is an add; only before is a removal; a state change keeps "
    "both sides",
    "[editor_history][diff]") {
    const ComponentState removed =
        componentState(1, "amplifier", {100}, {101}, {{"gain_dB", 0.0}}, "AMP-1");
    const ComponentState changed_before =
        componentState(2, "generator", {}, {102}, {{"level_dBm", 0.0}}, "GEN-1");
    const ComponentState changed_after =
        componentState(2, "generator", {}, {102}, {{"level_dBm", 3.0}}, "GEN-1");
    const ComponentState added =
        componentState(3, "mixer", {105, 106}, {107}, nlohmann::json::object(), "MIX-1");
    const ComponentState unchanged =
        componentState(4, "adder", {108}, {109}, nlohmann::json::object(), "ADD-1");

    EditorSnapshot before;
    before.components[1] = removed;
    before.components[2] = changed_before;
    before.components[4] = unchanged;

    EditorSnapshot after;
    after.components[2] = changed_after;
    after.components[3] = added;
    after.components[4] = unchanged;

    const EditorDelta delta = diffSnapshots(before, after);

    REQUIRE(delta.components.size() == 3);
    CHECK_FALSE(delta.empty());

    REQUIRE(delta.components.contains(1));
    CHECK(delta.components.at(1).before == removed);
    CHECK_FALSE(delta.components.at(1).after.has_value());

    REQUIRE(delta.components.contains(3));
    CHECK_FALSE(delta.components.at(3).before.has_value());
    CHECK(delta.components.at(3).after == added);

    REQUIRE(delta.components.contains(2));
    CHECK(delta.components.at(2).before == changed_before);
    CHECK(delta.components.at(2).after == changed_after);

    CHECK_FALSE(delta.components.contains(4));
}

TEST_CASE("Link set differences split into removed and added", "[editor_history][diff]") {
    EditorSnapshot before;
    before.links = {LinkEnds{101, 103}, LinkEnds{103, 104}};
    EditorSnapshot after;
    after.links = {LinkEnds{103, 104}, LinkEnds{104, 105}};

    const EditorDelta delta = diffSnapshots(before, after);

    CHECK(delta.links_removed == std::set<LinkEnds>{LinkEnds{101, 103}});
    CHECK(delta.links_added == std::set<LinkEnds>{LinkEnds{104, 105}});
    CHECK(delta.components.empty());
}

TEST_CASE("A probe reorder is a probe change", "[editor_history][diff]") {
    EditorSnapshot before;
    before.probes = {1, 2};
    EditorSnapshot after;
    after.probes = {2, 1};

    const EditorDelta delta = diffSnapshots(before, after);

    REQUIRE(delta.probes.has_value());
    CHECK(delta.probes->before == std::vector<int>{1, 2});
    CHECK(delta.probes->after == std::vector<int>{2, 1});
}

TEST_CASE("Group, position, and app_state keys diff independently", "[editor_history][diff]") {
    EditorSnapshot before;
    before.groups[7] = GroupState{"Chain", {1, 2}, false};
    before.positions[3] = LayoutPoint{0.0f, 0.0f};
    before.app_state["rf_budget"] = nlohmann::json{{"gain", 1}};

    EditorSnapshot after;
    after.groups[7] = GroupState{"Chain", {1, 2}, true};
    after.groups[8] = GroupState{"Other", {3}, false};
    after.positions[3] = LayoutPoint{32.0f, 16.0f};
    after.positions[4] = LayoutPoint{64.0f, 0.0f};
    after.app_state["rf_budget"] = nlohmann::json{{"gain", 2}};
    after.app_state["na_points"] = nlohmann::json::array({1, 2});

    const EditorDelta delta = diffSnapshots(before, after);

    REQUIRE(delta.groups.size() == 2);
    REQUIRE(delta.groups.contains(7));
    CHECK(delta.groups.at(7).before ==
          std::optional<GroupState>{GroupState{"Chain", {1, 2}, false}});
    CHECK(delta.groups.at(7).after == std::optional<GroupState>{GroupState{"Chain", {1, 2}, true}});
    REQUIRE(delta.groups.contains(8));
    CHECK_FALSE(delta.groups.at(8).before.has_value());
    CHECK(delta.groups.at(8).after == std::optional<GroupState>{GroupState{"Other", {3}, false}});

    REQUIRE(delta.positions.size() == 2);
    REQUIRE(delta.positions.contains(3));
    // std::optional's operator== cannot see the file-local LayoutPoint equality, so compare the
    // sides directly.
    CHECK(sameChange(delta.positions.at(3),
                     Change<LayoutPoint>{LayoutPoint{0.0f, 0.0f}, LayoutPoint{32.0f, 16.0f}}));
    REQUIRE(delta.positions.contains(4));
    CHECK_FALSE(delta.positions.at(4).before.has_value());
    REQUIRE(delta.positions.at(4).after.has_value());
    CHECK(delta.positions.at(4).after->x == 64.0f);
    CHECK(delta.positions.at(4).after->y == 0.0f);

    REQUIRE(delta.app_state.size() == 2);
    REQUIRE(delta.app_state.contains("rf_budget"));
    CHECK(delta.app_state.at("rf_budget").before == nlohmann::json{{"gain", 1}});
    CHECK(delta.app_state.at("rf_budget").after == nlohmann::json{{"gain", 2}});
    REQUIRE(delta.app_state.contains("na_points"));
    CHECK_FALSE(delta.app_state.at("na_points").before.has_value());

    CHECK(delta.components.empty());
    CHECK(delta.links_removed.empty());
    CHECK(delta.links_added.empty());
    CHECK_FALSE(delta.probes.has_value());
}

TEST_CASE("reversed(diff(a, b)) equals diff(b, a)", "[editor_history][diff]") {
    const auto [before, after] = changedSnapshotPair();

    const EditorDelta forward = diffSnapshots(before, after);
    REQUIRE_FALSE(forward.empty());

    CHECK(sameDelta(reversed(forward), diffSnapshots(after, before)));
    CHECK(sameDelta(reversed(reversed(forward)), forward));
}

TEST_CASE("push clears the redo stack", "[editor_history][stack]") {
    EditorHistory history;
    history.push(HistoryRecord{"A", EditorDelta{}});
    history.push(HistoryRecord{"B", EditorDelta{}});
    REQUIRE(history.undoCount() == 2);
    REQUIRE(history.nextUndo() != nullptr);
    REQUIRE(history.nextUndo()->label == "B");

    history.stepBack();
    REQUIRE(history.undoCount() == 1);
    REQUIRE(history.redoCount() == 1);
    REQUIRE(history.nextRedo() != nullptr);
    CHECK(history.nextRedo()->label == "B");

    history.push(HistoryRecord{"C", EditorDelta{}});

    CHECK(history.redoCount() == 0);
    CHECK(history.nextRedo() == nullptr);
    REQUIRE(history.nextUndo() != nullptr);
    CHECK(history.nextUndo()->label == "C");
    // The record that was undone before the push is dropped with the rest of the redo stack.
    CHECK(history.undoCount() == 2);
    history.stepBack();
    REQUIRE(history.nextUndo() != nullptr);
    CHECK(history.nextUndo()->label == "A");
}

TEST_CASE("History keeps the newest 100 records", "[editor_history][stack]") {
    EditorHistory history;
    for (int index = 0; index <= 100; ++index)
        history.push(HistoryRecord{std::to_string(index), EditorDelta{}});

    CHECK(EditorHistory::kMaxRecords == 100);
    CHECK(history.undoCount() == 100);
    CHECK(history.redoCount() == 0);

    std::string last_undo_label;
    for (std::size_t step = 0; step < EditorHistory::kMaxRecords; ++step) {
        REQUIRE(history.nextUndo() != nullptr);
        last_undo_label = history.nextUndo()->label;
        history.stepBack();
    }

    // Record "0" was evicted, so the oldest reachable record is "1".
    CHECK(last_undo_label == "1");
    CHECK(history.nextUndo() == nullptr);
    CHECK(history.undoCount() == 0);
    CHECK(history.redoCount() == 100);

    // Redo walks back to the newest record and stops there.
    history.stepForward();
    REQUIRE(history.nextUndo() != nullptr);
    CHECK(history.nextUndo()->label == "1");
    CHECK(history.redoCount() == 99);
}

TEST_CASE("clear empties both stacks", "[editor_history][stack]") {
    EditorHistory history;
    history.push(HistoryRecord{"A", EditorDelta{}});
    history.push(HistoryRecord{"B", EditorDelta{}});
    history.stepBack();
    REQUIRE(history.undoCount() == 1);
    REQUIRE(history.redoCount() == 1);

    history.clear();

    CHECK(history.undoCount() == 0);
    CHECK(history.redoCount() == 0);
    CHECK(history.nextUndo() == nullptr);
    CHECK(history.nextRedo() == nullptr);

    // A push after clear starts a fresh history with no redo.
    history.push(HistoryRecord{"C", EditorDelta{}});
    CHECK(history.undoCount() == 1);
    CHECK(history.redoCount() == 0);
    CHECK(history.nextUndo()->label == "C");
}
