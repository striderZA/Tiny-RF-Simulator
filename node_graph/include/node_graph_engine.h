#pragma once

#include "group.h"
#include "signal_node.h"
#include <string>
#include <unordered_map>
#include <vector>

struct GraphNode {
    int node_id;
    std::vector<int> input_pin_ids;
    std::vector<int> output_pin_ids;
    SignalNode *signal_node;
    std::string label;
    std::string part_number; // library part number (e.g. "ZX60-33LN+"), empty for manual nodes

    // Per-pin labels for rendering (empty → default "IN"/"OUT")
    std::vector<std::string> input_labels;
    std::vector<std::string> output_labels;
};

struct GraphLink {
    int link_id;
    int start_pin_id;
    int end_pin_id;
};

// A resolved signal source: the owning node plus the output-port index whose
// Spectrum the link/probe carries. output_index == -1 means "no port" (null).
struct SignalSource {
    SignalNode *node = nullptr;
    int output_index = -1;
};

class NodeGraphEngine {
  public:
    int addNode(const std::string &label, SignalNode *signal_node, int num_inputs, int num_outputs);
    void removeNode(int node_id);

    int addLink(int start_pin, int end_pin);
    void removeLink(int link_id);
    void removeAllLinks();

    // Set internal ID counters for project save/load
    void setNextIds(int node_id, int pin_id, int link_id);

    int inputPinId(int node_id) const;
    int outputPinId(int node_id) const;

    SignalSource getSourceForInput(int input_pin_id) const;
    std::vector<SignalSource> getSourcesForInput(int input_pin_id) const;
    std::vector<int> topologicalOrder() const;

    static constexpr int MAX_PROBES = 4;

    const std::vector<int> &probePins() const { return m_probe_pins; }
    bool addProbePin(int pin_id);
    bool removeProbePin(int pin_id);
    void clearProbes();
    int probeSlotForPin(int pin_id) const;
    std::vector<SignalSource> probedSignalNodes() const;

    const std::vector<GraphNode> &nodes() const { return m_nodes; }
    const std::vector<GraphLink> &links() const { return m_links; }

    int nodeIdForPin(int pin_id) const;

    // Set pin labels for a node (for rendering in the node graph widget)
    void setNodePinLabels(int node_id, const std::vector<std::string> &input_labels,
                          const std::vector<std::string> &output_labels);

    // Set the library part number for a node (for display in the node graph)
    void setNodePartNumber(int node_id, const std::string &part_number);

    // Group counter accessors for project save/load
    int nextGroupId() const { return m_next_group_id; }
    int nextBoundaryPinId() const { return m_next_boundary_pin_id; }
    void setNextGroupId(int id) { m_next_group_id = id; }
    void setNextBoundaryPinId(int id) { m_next_boundary_pin_id = id; }

    // Group operations
    int addGroup(std::string name, std::vector<int> member_node_ids);
    void removeGroup(int group_id, bool rebuild_cache = true);

    // Group collection + accessors
    const std::vector<Group> &groups() const { return m_groups; }
    const Group *groupById(int group_id) const;
    int numGroups() const { return static_cast<int>(m_groups.size()); }
    int selectedGroupId() const { return m_selected_group_id; }
    void setSelectedGroupId(int id);
    void renameGroup(int group_id, std::string name);
    void setGroupCollapsed(int group_id, bool collapsed);
    bool isGroupCollapsed(int group_id) const;
    void rebuildGroupBoundaryPins(int group_id);
    int groupIdForNode(int node_id) const;
    const std::vector<int> &groupsContainingNode(int node_id) const;

  private:
    int m_next_node_id = 1;
    int m_next_pin_id = 100;
    int m_next_link_id = 1000;
    int m_next_group_id = 50000;
    int m_next_boundary_pin_id = 100000;
    int m_selected_group_id = -1;
    std::vector<int> m_probe_pins;
    std::vector<GraphNode> m_nodes;
    std::vector<GraphLink> m_links;
    std::vector<Group> m_groups;
    std::unordered_map<int, std::vector<int>> m_node_to_group_cache;

    void rebuildNodeToGroupCache();
};

// View-layer component type. Used by NodeGraphWidget to pick a color and
// schematic symbol. The engine never reads, stores, or returns this type;
// it is derived from GraphNode::label at render time.
enum class NodeKind {
    Unknown,
    Generator,
    Amplifier,
    Splitter,
    Mixer,
    Adc,
    PFB,
    IdealFilter,
    CoaxCable,
    Equalizer,
    Attenuator,
    Combiner,
    NetworkAnalyzer,
    GroupCollapsed
};

// Per-NodeKind ARGB color. Engine has no imgui include, so the return type
// is plain uint32_t (same bit layout as IM_COL32: 0xAARRGGBB). The widget
// casts to ImU32 at the call site.
inline uint32_t themeColor(NodeKind k) {
    switch (k) {
    case NodeKind::Generator:
        return 0xFF4ADE80; // green
    case NodeKind::Amplifier:
        return 0xFFFB923C; // orange
    case NodeKind::Mixer:
        return 0xFFC084FC; // purple
    case NodeKind::Splitter:
        return 0xFFFACC15; // amber
    case NodeKind::Adc:
        return 0xFF60A5FA; // blue
    case NodeKind::PFB:
        return 0xFF22D3EE; // cyan
    case NodeKind::IdealFilter:
        return 0xFF2DD4BF; // teal
    case NodeKind::CoaxCable:
        return 0xFF94A3B8; // slate
    case NodeKind::Equalizer:
        return 0xFF34D399; // emerald
    case NodeKind::Attenuator:
        return 0xFF64B48C; // muted green
    case NodeKind::Combiner:
        return 0xFFF87171; // red
    case NodeKind::NetworkAnalyzer:
        return 0xFFE879F9; // fuchsia
    case NodeKind::GroupCollapsed:
        return 0xFF818CF8;  // indigo
    case NodeKind::Unknown: // fallthrough
    default:
        return 0xFF9CA3AF; // gray
    }
}
