#include "editor_history.h"
#include <algorithm>
#include <iterator>
#include <utility>

namespace {

// LayoutPoint carries no operator==; grid positions compare by exact value.
bool operator==(const LayoutPoint &lhs, const LayoutPoint &rhs) {
    return lhs.x == rhs.x && lhs.y == rhs.y;
}

template <class Key, class Value>
std::map<Key, Change<Value>> diffMaps(const std::map<Key, Value> &before,
                                      const std::map<Key, Value> &after) {
    std::map<Key, Change<Value>> changes;
    for (const auto &[key, value] : before) {
        const auto found = after.find(key);
        if (found == after.end())
            changes.emplace(key, Change<Value>{value, std::nullopt});
        else if (!(found->second == value))
            changes.emplace(key, Change<Value>{value, found->second});
    }
    for (const auto &[key, value] : after) {
        if (!before.contains(key))
            changes.emplace(key, Change<Value>{std::nullopt, value});
    }
    return changes;
}

std::set<LinkEnds> setDifference(const std::set<LinkEnds> &lhs, const std::set<LinkEnds> &rhs) {
    std::set<LinkEnds> difference;
    std::set_difference(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
                        std::inserter(difference, difference.end()));
    return difference;
}

template <class V> Change<V> reverseChange(const Change<V> &change) {
    return Change<V>{change.after, change.before};
}

template <class Key, class Value>
std::map<Key, Change<Value>> reverseChanges(const std::map<Key, Change<Value>> &changes) {
    std::map<Key, Change<Value>> reversed_changes;
    for (const auto &[key, change] : changes)
        reversed_changes.emplace(key, reverseChange(change));
    return reversed_changes;
}

} // namespace

bool EditorDelta::empty() const {
    return components.empty() && links_removed.empty() && links_added.empty() && !probes &&
           groups.empty() && positions.empty() && app_state.empty();
}

EditorDelta diffSnapshots(const EditorSnapshot &before, const EditorSnapshot &after) {
    EditorDelta delta;
    delta.components = diffMaps(before.components, after.components);
    delta.links_removed = setDifference(before.links, after.links);
    delta.links_added = setDifference(after.links, before.links);
    if (before.probes != after.probes)
        delta.probes = Change<std::vector<int>>{before.probes, after.probes};
    delta.groups = diffMaps(before.groups, after.groups);
    delta.positions = diffMaps(before.positions, after.positions);
    delta.app_state = diffMaps(before.app_state, after.app_state);
    return delta;
}

EditorDelta reversed(const EditorDelta &delta) {
    EditorDelta reversed_delta;
    reversed_delta.components = reverseChanges(delta.components);
    reversed_delta.links_removed = delta.links_added;
    reversed_delta.links_added = delta.links_removed;
    if (delta.probes)
        reversed_delta.probes = reverseChange(*delta.probes);
    reversed_delta.groups = reverseChanges(delta.groups);
    reversed_delta.positions = reverseChanges(delta.positions);
    reversed_delta.app_state = reverseChanges(delta.app_state);
    return reversed_delta;
}

void EditorHistory::push(HistoryRecord record) {
    m_records.erase(m_records.begin() + static_cast<std::ptrdiff_t>(m_cursor), m_records.end());
    m_records.push_back(std::move(record));
    if (m_records.size() > kMaxRecords)
        m_records.pop_front();
    m_cursor = m_records.size();
}

const HistoryRecord *EditorHistory::nextUndo() const {
    if (m_cursor == 0)
        return nullptr;
    return &m_records[m_cursor - 1];
}

const HistoryRecord *EditorHistory::nextRedo() const {
    if (m_cursor >= m_records.size())
        return nullptr;
    return &m_records[m_cursor];
}

void EditorHistory::stepBack() {
    if (m_cursor > 0)
        --m_cursor;
}

void EditorHistory::stepForward() {
    if (m_cursor < m_records.size())
        ++m_cursor;
}

void EditorHistory::clear() {
    m_records.clear();
    m_cursor = 0;
}

std::size_t EditorHistory::undoCount() const { return m_cursor; }

std::size_t EditorHistory::redoCount() const { return m_records.size() - m_cursor; }
