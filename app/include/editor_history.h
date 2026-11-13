#pragma once

#include "node_layout.h"
#include <compare>
#include <cstddef>
#include <deque>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <string>
#include <vector>

// Captured state of one subcircuit group, used to recreate it under its
// original id after an undo removes it.
struct GroupState {
    std::string name;
    std::vector<int> members;
    bool collapsed = false;
    bool operator==(const GroupState &) const = default;
};

// Captured state of one placed component. In an EditorSnapshot it is keyed by graph node id.
struct ComponentState {
    int component_id = -1;
    std::string type_name; // IComponentEngine::type_name(), a ComponentTypeRegistry key
    std::vector<int> input_pins;
    std::vector<int> output_pins;
    nlohmann::json state; // IComponentEngine::serialize()
    std::string part_number;
    bool operator==(const ComponentState &) const = default;
};

// A link identified by its pin pair rather than its link id, so a diff survives a re-link.
struct LinkEnds {
    int start_pin = -1;
    int end_pin = -1;
    auto operator<=>(const LinkEnds &) const = default;
};

// Everything undo can restore. Maps and sets are keyed for diffing; probes keep slot order.
struct EditorSnapshot {
    std::map<int, ComponentState> components; // key: graph node id
    std::set<LinkEnds> links;
    std::vector<int> probes;                         // slot order
    std::map<int, GroupState> groups;                // key: group id
    std::map<int, LayoutPoint> positions;            // key: graph node id; grid space
    std::map<std::string, nlohmann::json> app_state; // key: app-defined name
};

// The two sides of one changed key. A missing side means the key was absent in that snapshot.
template <class V> struct Change {
    std::optional<V> before;
    std::optional<V> after;
};

// What changed between two snapshots. Undo applies the reversed delta; redo applies it forward.
struct EditorDelta {
    std::map<int, Change<ComponentState>> components;
    std::set<LinkEnds> links_removed;
    std::set<LinkEnds> links_added;
    std::optional<Change<std::vector<int>>> probes;
    std::map<int, Change<GroupState>> groups;
    std::map<int, Change<LayoutPoint>> positions;
    std::map<std::string, Change<nlohmann::json>> app_state;

    bool empty() const;
};

// Positions compare by exact value: a snapshot records the value it read, not a tolerance.
EditorDelta diffSnapshots(const EditorSnapshot &before, const EditorSnapshot &after);

// Swaps every before/after side and the removed/added link sets.
EditorDelta reversed(const EditorDelta &delta);

struct HistoryRecord {
    std::string label;
    EditorDelta delta;
};

// Linear undo history: a push after an undo drops the records that were redoable, and the oldest
// record is evicted beyond kMaxRecords.
class EditorHistory {
  public:
    static constexpr std::size_t kMaxRecords = 100;

    // Clears redo; evicts the oldest record beyond kMaxRecords.
    void push(HistoryRecord record);
    // nullptr when empty.
    const HistoryRecord *nextUndo() const;
    const HistoryRecord *nextRedo() const;
    // After a successful undo.
    void stepBack();
    // After a successful redo.
    void stepForward();
    void clear();
    std::size_t undoCount() const;
    std::size_t redoCount() const;

  private:
    std::deque<HistoryRecord> m_records;
    // Number of records applied: the next undo is m_cursor - 1, the next redo is m_cursor.
    std::size_t m_cursor = 0;
};
