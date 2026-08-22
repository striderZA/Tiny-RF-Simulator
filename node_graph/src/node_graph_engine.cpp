#include "node_graph_engine.h"
#include "logging_core.h"
#include <algorithm>
#include <queue>
#include <unordered_map>
#include <unordered_set>

int NodeGraphEngine::addNode(const std::string &label, SignalNode *signal_node, int num_inputs,
                             int num_outputs) {
    GraphNode node;
    node.node_id = m_next_node_id++;
    node.input_pin_ids.reserve(num_inputs);
    for (int i = 0; i < num_inputs; ++i) {
        node.input_pin_ids.push_back(m_next_pin_id++);
    }
    node.output_pin_ids.reserve(num_outputs);
    for (int i = 0; i < num_outputs; ++i) {
        node.output_pin_ids.push_back(m_next_pin_id++);
    }
    node.signal_node = signal_node;
    node.label = label;
    m_nodes.push_back(node);
    LOG_INFO("Added node '%s' (id=%d)", label.c_str(), node.node_id);
    return node.node_id;
}

void NodeGraphEngine::removeNode(int node_id) {
    auto it = std::find_if(m_nodes.begin(), m_nodes.end(),
                           [node_id](const GraphNode &n) { return n.node_id == node_id; });
    if (it == m_nodes.end())
        return;

    LOG_INFO("Removed node '%s' (id=%d)", it->label.c_str(), node_id);

    // Collect all pin IDs belonging to this node
    std::vector<int> node_pins;
    node_pins.reserve(it->input_pin_ids.size() + it->output_pin_ids.size());
    node_pins.insert(node_pins.end(), it->input_pin_ids.begin(), it->input_pin_ids.end());
    node_pins.insert(node_pins.end(), it->output_pin_ids.begin(), it->output_pin_ids.end());

    // Remove all links connected to any of this node's pins
    m_links.erase(std::remove_if(m_links.begin(), m_links.end(),
                                 [&node_pins](const GraphLink &l) {
                                     return std::find(node_pins.begin(), node_pins.end(),
                                                      l.start_pin_id) != node_pins.end() ||
                                            std::find(node_pins.begin(), node_pins.end(),
                                                      l.end_pin_id) != node_pins.end();
                                 }),
                  m_links.end());

    for (int pin : node_pins) {
        removeProbePin(pin);
    }

    m_nodes.erase(it);

    // NEW: cascade group cleanup — remove node_id from any groups, drop groups with < 2 members
    for (auto &g : m_groups) {
        auto member_it = std::find(g.member_node_ids.begin(), g.member_node_ids.end(), node_id);
        if (member_it != g.member_node_ids.end()) {
            g.member_node_ids.erase(member_it);
        }
    }
    std::vector<int> groups_to_remove;
    for (const auto &g : m_groups) {
        if (g.member_node_ids.size() < 2) {
            groups_to_remove.push_back(g.id);
        }
    }
    for (int gid : groups_to_remove) {
        removeGroup(gid, false);
    }
    rebuildNodeToGroupCache();

    // Rebuild boundary pins for all surviving groups
    for (const auto &g : m_groups) {
        rebuildGroupBoundaryPins(g.id);
    }
}

int NodeGraphEngine::addLink(int start_pin, int end_pin) {
    if (m_link_validator && !m_link_validator(start_pin, end_pin)) {
        LOG_WARN("Rejected link: %d -> %d", start_pin, end_pin);
        return -1;
    }

    GraphLink link;
    link.link_id = m_next_link_id++;
    link.start_pin_id = start_pin;
    link.end_pin_id = end_pin;
    m_links.push_back(link);
    LOG_INFO("Connected pins: %d -> %d (link id=%d)", start_pin, end_pin, link.link_id);
    return link.link_id;
}

void NodeGraphEngine::removeLink(int link_id) {
    auto it = std::find_if(m_links.begin(), m_links.end(),
                           [link_id](const GraphLink &l) { return l.link_id == link_id; });
    if (it != m_links.end()) {
        LOG_INFO("Disconnected pins: %d -> %d (link id=%d)", it->start_pin_id, it->end_pin_id,
                 link_id);
        m_links.erase(it);
    }
}

void NodeGraphEngine::removeAllLinks() {
    m_links.clear();
    LOG_INFO("Removed all links");
}

void NodeGraphEngine::setNextIds(int node_id, int pin_id, int link_id) {
    m_next_node_id = node_id;
    m_next_pin_id = pin_id;
    m_next_link_id = link_id;
}

int NodeGraphEngine::inputPinId(int node_id) const {
    for (const auto &n : m_nodes) {
        if (n.node_id == node_id && !n.input_pin_ids.empty())
            return n.input_pin_ids[0];
    }
    return -1;
}

int NodeGraphEngine::outputPinId(int node_id) const {
    for (const auto &n : m_nodes) {
        if (n.node_id == node_id && !n.output_pin_ids.empty())
            return n.output_pin_ids[0];
    }
    return -1;
}

SignalSource NodeGraphEngine::getSourceForInput(int input_pin_id) const {
    if (input_pin_id < 0)
        return {};
    for (const auto &link : m_links) {
        if (link.end_pin_id == input_pin_id) {
            for (const auto &node : m_nodes) {
                for (size_t i = 0; i < node.output_pin_ids.size(); ++i) {
                    if (node.output_pin_ids[i] == link.start_pin_id) {
                        return {node.signal_node, static_cast<int>(i)};
                    }
                }
            }
        }
    }
    return {};
}

std::vector<SignalSource> NodeGraphEngine::getSourcesForInput(int input_pin_id) const {
    std::vector<SignalSource> result;
    if (input_pin_id < 0)
        return result;
    for (const auto &link : m_links) {
        if (link.end_pin_id == input_pin_id) {
            for (const auto &node : m_nodes) {
                for (size_t i = 0; i < node.output_pin_ids.size(); ++i) {
                    if (node.output_pin_ids[i] == link.start_pin_id) {
                        result.push_back({node.signal_node, static_cast<int>(i)});
                        break;
                    }
                }
            }
        }
    }
    return result;
}

bool NodeGraphEngine::addProbePin(int pin_id) {
    if (pin_id < 0)
        return false;
    for (int p : m_probe_pins)
        if (p == pin_id)
            return false;
    if (m_probe_pins.size() >= static_cast<size_t>(MAX_PROBES))
        return false;
    m_probe_pins.push_back(pin_id);
    LOG_INFO("Probe added: pin %d (slot %zu)", pin_id, m_probe_pins.size());
    return true;
}

bool NodeGraphEngine::removeProbePin(int pin_id) {
    for (auto it = m_probe_pins.begin(); it != m_probe_pins.end(); ++it) {
        if (*it == pin_id) {
            m_probe_pins.erase(it);
            LOG_INFO("Probe removed: pin %d", pin_id);
            return true;
        }
    }
    return false;
}

void NodeGraphEngine::clearProbes() { m_probe_pins.clear(); }

int NodeGraphEngine::probeSlotForPin(int pin_id) const {
    for (size_t i = 0; i < m_probe_pins.size(); ++i)
        if (m_probe_pins[i] == pin_id)
            return static_cast<int>(i);
    return -1;
}

std::vector<SignalSource> NodeGraphEngine::probedSignalNodes() const {
    std::vector<SignalSource> result;
    result.reserve(m_probe_pins.size());
    for (int pin_id : m_probe_pins) {
        bool found = false;
        // Check output pins first — resolve the output-port index too.
        for (const auto &node : m_nodes) {
            for (size_t i = 0; i < node.output_pin_ids.size(); ++i) {
                if (node.output_pin_ids[i] == pin_id) {
                    result.push_back({node.signal_node, static_cast<int>(i)});
                    found = true;
                    break;
                }
            }
            if (found)
                break;
        }
        if (found)
            continue;
        // Input pin — resolve to the upstream source's output (node + index)
        for (const auto &node : m_nodes) {
            for (size_t i = 0; i < node.input_pin_ids.size(); ++i) {
                if (node.input_pin_ids[i] == pin_id) {
                    result.push_back(getSourceForInput(pin_id));
                    found = true;
                    break;
                }
            }
            if (found)
                break;
        }
        if (!found)
            result.push_back({});
    }
    return result;
}

int NodeGraphEngine::nodeIdForPin(int pin_id) const {
    for (const auto &node : m_nodes) {
        for (int p : node.input_pin_ids)
            if (p == pin_id)
                return node.node_id;
        for (int p : node.output_pin_ids)
            if (p == pin_id)
                return node.node_id;
    }
    return -1;
}

void NodeGraphEngine::setNodePinLabels(int node_id, const std::vector<std::string> &input_labels,
                                       const std::vector<std::string> &output_labels) {
    for (auto &node : m_nodes) {
        if (node.node_id == node_id) {
            node.input_labels = input_labels;
            node.output_labels = output_labels;
            return;
        }
    }
}

void NodeGraphEngine::setNodePartNumber(int node_id, const std::string &part_number) {
    for (auto &node : m_nodes) {
        if (node.node_id == node_id) {
            node.part_number = part_number;
            return;
        }
    }
}

std::vector<int> NodeGraphEngine::topologicalOrder() const {
    std::unordered_map<int, int> in_degree;
    for (const auto &n : m_nodes)
        in_degree[n.node_id] = 0;

    std::unordered_map<int, int> pin_owner;
    for (const auto &n : m_nodes) {
        for (int p : n.output_pin_ids)
            pin_owner[p] = n.node_id;
        for (int p : n.input_pin_ids)
            pin_owner[p] = n.node_id;
    }

    for (const auto &link : m_links) {
        auto it = pin_owner.find(link.end_pin_id);
        if (it != pin_owner.end())
            in_degree[it->second]++;
    }

    std::queue<int> q;
    for (const auto &[node_id, deg] : in_degree) {
        if (deg == 0)
            q.push(node_id);
    }

    std::vector<int> result;
    result.reserve(m_nodes.size());
    while (!q.empty()) {
        int id = q.front();
        q.pop();
        result.push_back(id);

        auto node_it = std::find_if(m_nodes.begin(), m_nodes.end(),
                                    [id](const GraphNode &n) { return n.node_id == id; });
        if (node_it == m_nodes.end())
            continue;

        for (int out_pin : node_it->output_pin_ids) {
            for (const auto &link : m_links) {
                if (link.start_pin_id == out_pin) {
                    auto target_it = pin_owner.find(link.end_pin_id);
                    if (target_it != pin_owner.end()) {
                        if (--in_degree[target_it->second] == 0)
                            q.push(target_it->second);
                    }
                }
            }
        }
    }

    if (static_cast<int>(result.size()) < static_cast<int>(m_nodes.size())) {
        LOG_WARN("topologicalOrder: cycle detected, %zu nodes not ordered",
                 m_nodes.size() - result.size());
        for (const auto &n : m_nodes) {
            if (std::find(result.begin(), result.end(), n.node_id) == result.end())
                result.push_back(n.node_id);
        }
    }

    return result;
}

const Group *NodeGraphEngine::groupById(int group_id) const {
    for (const auto &g : m_groups) {
        if (g.id == group_id)
            return &g;
    }
    return nullptr;
}

int NodeGraphEngine::addGroup(std::string name, std::vector<int> member_node_ids) {
    if (member_node_ids.size() < 2)
        return -1;

    // Validate: no unknown nodes, no node already in a group
    auto find_node = [this](int nid) {
        return std::find_if(m_nodes.begin(), m_nodes.end(),
                            [nid](const GraphNode &n) { return n.node_id == nid; });
    };
    for (int nid : member_node_ids) {
        if (find_node(nid) == m_nodes.end())
            return -1;
        if (groupIdForNode(nid) != -1)
            return -1;
    }

    // Reject duplicates
    std::vector<int> sorted = member_node_ids;
    std::sort(sorted.begin(), sorted.end());
    if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end())
        return -1;

    Group g;
    g.id = m_next_group_id++;
    g.name = std::move(name);
    g.member_node_ids = std::move(member_node_ids);
    g.collapsed = true;
    m_groups.push_back(std::move(g));
    rebuildNodeToGroupCache();
    rebuildGroupBoundaryPins(m_groups.back().id);
    return m_groups.back().id;
}

void NodeGraphEngine::removeGroup(int group_id, bool rebuild_cache) {
    auto it = std::find_if(m_groups.begin(), m_groups.end(),
                           [group_id](const Group &g) { return g.id == group_id; });
    if (it == m_groups.end())
        return;

    if (m_selected_group_id == group_id) {
        m_selected_group_id = -1;
    }
    m_groups.erase(it);
    if (rebuild_cache) {
        rebuildNodeToGroupCache();
    }
}

void NodeGraphEngine::setSelectedGroupId(int id) { m_selected_group_id = id; }

void NodeGraphEngine::renameGroup(int group_id, std::string name) {
    for (auto &g : m_groups) {
        if (g.id == group_id) {
            g.name = std::move(name);
            return;
        }
    }
}

void NodeGraphEngine::setGroupCollapsed(int group_id, bool collapsed) {
    for (auto &g : m_groups) {
        if (g.id == group_id) {
            g.collapsed = collapsed;
            if (collapsed) {
                rebuildGroupBoundaryPins(group_id);
            } else {
                // On expand, clear boundary pins so rebuildSynthMaps does not
                // register stale synthetic pins that lack imnodes attribute registration.
                g.boundary_pins.clear();
            }
            return;
        }
    }
}

bool NodeGraphEngine::isGroupCollapsed(int group_id) const {
    for (const auto &g : m_groups) {
        if (g.id == group_id)
            return g.collapsed;
    }
    return false;
}

int NodeGraphEngine::groupIdForNode(int node_id) const {
    auto it = m_node_to_group_cache.find(node_id);
    if (it == m_node_to_group_cache.end())
        return -1;
    if (it->second.empty())
        return -1;
    return it->second.front();
}

const std::vector<int> &NodeGraphEngine::groupsContainingNode(int node_id) const {
    auto it = m_node_to_group_cache.find(node_id);
    if (it != m_node_to_group_cache.end())
        return it->second;
    static const std::vector<int> empty;
    return empty;
}

void NodeGraphEngine::rebuildNodeToGroupCache() {
    m_node_to_group_cache.clear();
    for (const auto &g : m_groups) {
        for (int nid : g.member_node_ids) {
            m_node_to_group_cache[nid].push_back(g.id);
        }
    }
}
void NodeGraphEngine::rebuildGroupBoundaryPins(int group_id) {
    auto *g = const_cast<Group *>(groupById(group_id));
    if (!g)
        return;

    // Build a set of member node ids for O(1) lookup
    std::unordered_set<int> members(g->member_node_ids.begin(), g->member_node_ids.end());

    // Helper: find the node id that owns a pin
    auto node_for_pin = [this](int pin_id) -> int {
        for (const auto &n : m_nodes) {
            for (int p : n.input_pin_ids)
                if (p == pin_id)
                    return n.node_id;
            for (int p : n.output_pin_ids)
                if (p == pin_id)
                    return n.node_id;
        }
        return -1;
    };

    // Helper: get label for a node
    auto node_label = [this](int nid) -> std::string {
        for (const auto &n : m_nodes) {
            if (n.node_id == nid)
                return n.label;
        }
        return "";
    };

    // Helper: get pin label for a node's input or output
    auto pin_label = [](const GraphNode &n, int pin_id, bool is_output) -> std::string {
        const auto &labels = is_output ? n.output_labels : n.input_labels;
        const auto &pins = is_output ? n.output_pin_ids : n.input_pin_ids;
        for (size_t i = 0; i < pins.size(); ++i) {
            if (pins[i] == pin_id) {
                if (i < labels.size() && !labels[i].empty())
                    return labels[i];
                return is_output ? "OUT" : "IN";
            }
        }
        return is_output ? "OUT" : "IN";
    };

    std::vector<GroupBoundaryPin> new_pins;
    for (const auto &link : m_links) {
        int start_node = node_for_pin(link.start_pin_id);
        int end_node = node_for_pin(link.end_pin_id);
        if (start_node < 0 || end_node < 0)
            continue;

        bool start_in = members.count(start_node) > 0;
        bool end_in = members.count(end_node) > 0;
        if (start_in && end_in)
            continue; // internal link
        if (!start_in && !end_in)
            continue; // external link

        GroupBoundaryPin bp;
        bp.id = m_next_boundary_pin_id++;
        if (start_in) {
            // source is in-group; boundary pin is an output
            bp.is_output = true;
            bp.internal_node_id = start_node;
            bp.internal_pin_id = link.start_pin_id;
            for (const auto &n : m_nodes) {
                if (n.node_id == start_node) {
                    bp.label = node_label(start_node) + " " + pin_label(n, link.start_pin_id, true);
                    break;
                }
            }
        } else {
            // target is in-group; boundary pin is an input
            bp.is_output = false;
            bp.internal_node_id = end_node;
            bp.internal_pin_id = link.end_pin_id;
            for (const auto &n : m_nodes) {
                if (n.node_id == end_node) {
                    bp.label = node_label(end_node) + " " + pin_label(n, link.end_pin_id, false);
                    break;
                }
            }
        }
        new_pins.push_back(std::move(bp));
    }
    g->boundary_pins = std::move(new_pins);
}
