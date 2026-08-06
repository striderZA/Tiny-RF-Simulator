#include "project_serializer.h"
#include "component_registry.h"
#include "component_type_registry.h"
#include "imgui.h"
#include "imnodes.h"
#include "logging_core.h"
#include "node_graph_engine.h"
#include "node_graph_widget.h"
#include "pfb_channelizer_engine.h"
#include "pfb_view_manager.h"
#include "session_state.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

ProjectSerializer::ProjectSerializer(ComponentRegistry &components, NodeGraphEngine &graph,
                                     NodeGraphWidget &graph_widget, PFBViewManager &pfb_views,
                                     SessionState &state, int &next_component_id, bool &show_log,
                                     bool &show_spectrum, bool &show_properties,
                                     bool &show_node_editor)
    : m_components(components), m_graph(graph), m_graph_widget(graph_widget),
      m_pfb_views(pfb_views), m_state(state), m_next_component_id(next_component_id),
      m_show_log(show_log), m_show_spectrum(show_spectrum), m_show_properties(show_properties),
      m_show_node_editor(show_node_editor) {}

void ProjectSerializer::save(const std::string &path) {
    nlohmann::json root;
    root["version"] = 1;

    auto pos = path.find_last_of("\\/");
    std::string fname = (pos != std::string::npos) ? path.substr(pos + 1) : path;
    auto dot = fname.find_last_of('.');
    root["name"] = (dot != std::string::npos) ? fname.substr(0, dot) : fname;

    // Ensure all engine nodes are registered with the imnodes context
    // so GetNodeEditorSpacePos() doesn't assert on node IDs added without
    // a prior render frame (e.g. via newProject then programmatic add).
    m_graph_widget.syncNodesFromEngine();

    // Save components by iterating the registry
    nlohmann::json comps_arr = nlohmann::json::array();
    for (auto *comp : m_components.all()) {
        nlohmann::json cj;
        const auto *desc = ComponentTypeRegistry::instance().find(comp->type_name());
        cj["type"] = desc ? desc->project_type : "Unknown";
        cj["params"] = comp->serialize();

        // Save node position via imnodes
        int nid = comp->graphNodeId();
        ImNodes::EditorContextSet(m_graph_widget.context());
        ImVec2 pos_n = ImNodes::GetNodeEditorSpacePos(nid);
        cj["pos"]["x"] = pos_n.x;
        cj["pos"]["y"] = pos_n.y;

        // Save library part number if set
        for (const auto &gn : m_graph.nodes()) {
            if (gn.node_id == nid && !gn.part_number.empty()) {
                cj["part_number"] = gn.part_number;
                break;
            }
        }

        comps_arr.push_back(cj);
    }
    root["components"] = comps_arr;

    // Save links as component-index + port pairs (not raw pin IDs)
    nlohmann::json links_arr = nlohmann::json::array();
    // Build a map: pin_id \u2192 {comp_index, port, is_output}
    struct PinInfo {
        size_t comp;
        int port;
        bool is_output;
    };
    std::unordered_map<int, PinInfo> pin_map;
    for (size_t i = 0; i < m_components.size(); ++i) {
        auto *comp = m_components.all()[i];
        int nid = comp->graphNodeId();
        for (const auto &gn : m_graph.nodes()) {
            if (gn.node_id == nid) {
                for (size_t p = 0; p < gn.input_pin_ids.size(); ++p)
                    pin_map[gn.input_pin_ids[p]] = {i, (int)p, false};
                for (size_t p = 0; p < gn.output_pin_ids.size(); ++p)
                    pin_map[gn.output_pin_ids[p]] = {i, (int)p, true};
                break;
            }
        }
    }
    for (const auto &link : m_graph.links()) {
        auto from_it = pin_map.find(link.start_pin_id);
        auto to_it = pin_map.find(link.end_pin_id);
        if (from_it == pin_map.end() || to_it == pin_map.end())
            continue;
        nlohmann::json lj;
        lj["from"] = from_it->second.comp;
        lj["from_port"] = from_it->second.port;
        lj["to"] = to_it->second.comp;
        lj["to_port"] = to_it->second.port;
        links_arr.push_back(lj);
    }
    root["links"] = links_arr;

    // Save probes as component-index + port
    nlohmann::json probes_arr = nlohmann::json::array();
    for (int probe_pin : m_graph.probePins()) {
        auto it = pin_map.find(probe_pin);
        if (it != pin_map.end()) {
            nlohmann::json pj;
            pj["comp"] = it->second.comp;
            pj["port"] = it->second.port;
            pj["is_output"] = it->second.is_output;
            probes_arr.push_back(pj);
        }
    }
    root["probe_pins"] = probes_arr;

    // Save groups
    nlohmann::json groups_arr = nlohmann::json::array();
    // Build node_id \u2192 comp_index map
    std::unordered_map<int, size_t> nid_to_comp;
    for (size_t i = 0; i < m_components.size(); ++i)
        nid_to_comp[m_components.all()[i]->graphNodeId()] = i;

    for (const auto &g : m_graph.groups()) {
        nlohmann::json gj;
        gj["name"] = g.name;
        gj["collapsed"] = g.collapsed;
        gj["member_components"] = nlohmann::json::array();
        for (int member_nid : g.member_node_ids) {
            auto it = nid_to_comp.find(member_nid);
            if (it != nid_to_comp.end())
                gj["member_components"].push_back(it->second);
        }
        groups_arr.push_back(gj);
    }
    root["groups"] = groups_arr;

    // Window state
    root["window_state"]["log"] = m_show_log;
    root["window_state"]["spectrum_analyzer"] = m_show_spectrum;
    root["window_state"]["properties"] = m_show_properties;
    root["window_state"]["node_editor"] = m_show_node_editor;

    // Graph state counters (for later additions)
    root["graph_state"]["next_component_id"] = m_next_component_id;

    std::ofstream out(path);
    if (!out) {
        LOG_ERROR("Failed to open project file for writing: %s", path.c_str());
        return;
    }
    out << root.dump(2);
    out.close();

    LOG_INFO("Saved project to %s", path.c_str());
}

bool ProjectSerializer::load(const std::string &path) {
    std::ifstream in(path);
    if (!in) {
        LOG_ERROR("Failed to open project file: %s", path.c_str());
        return false;
    }
    nlohmann::json root;
    try {
        in >> root;
    } catch (const nlohmann::json::exception &e) {
        LOG_ERROR("Invalid project file: %s", e.what());
        return false;
    }

    reset();

    // Map: type string \u2192 factory lambda
    std::vector<nlohmann::json::iterator> comp_order;
    auto &comps = root["components"];
    for (auto it = comps.begin(); it != comps.end(); ++it)
        comp_order.push_back(it);

    // Create components in saved order
    std::vector<int> new_node_ids; // maps saved index \u2192 new graph node ID
    for (auto &it : comp_order) {
        auto &cj = *it;
        std::string type = cj.value("type", "");
        auto &params = cj["params"];

        const auto *desc = ComponentTypeRegistry::instance().findByProjectType(type);
        if (!desc) {
            LOG_WARN("Unknown component type in project file: %s", type.c_str());
            new_node_ids.push_back(-1);
            continue;
        }
        IComponentEngine *comp = desc->create(m_components, m_graph, m_next_component_id++);
        comp->deserialize(params);
        if (desc->type == "pfb") {
            // Restore IQ plot + PFB grid widgets for this PFB
            m_pfb_views.addFor(*static_cast<PFBChannelizerEngine *>(comp), m_state);
        }

        new_node_ids.push_back(comp ? comp->graphNodeId() : -1);

        // Restore position
        if (comp && cj.contains("pos")) {
            ImNodes::EditorContextSet(m_graph_widget.context());
            ImNodes::SetNodeEditorSpacePos(comp->graphNodeId(), ImVec2(cj["pos"].value("x", 0.0f),
                                                                       cj["pos"].value("y", 0.0f)));
        }

        // Restore library part number
        if (comp && cj.contains("part_number"))
            m_graph.setNodePartNumber(comp->graphNodeId(), cj["part_number"].get<std::string>());
    }
    // After restoring all positions, inform the widget so subsequent
    // syncNodesFromEngine calls (e.g. from saveProject) don't reset them.
    m_graph_widget.markNodesRegistered();

    // Restore links (saved as component-index + port pairs)
    auto &saved_links = root["links"];
    for (const auto &lj : saved_links) {
        int from_idx = lj.value("from", -1);
        int to_idx = lj.value("to", -1);
        int from_port = lj.value("from_port", 0);
        int to_port = lj.value("to_port", 0);
        if (from_idx < 0 || to_idx < 0 || static_cast<size_t>(from_idx) >= new_node_ids.size() ||
            static_cast<size_t>(to_idx) >= new_node_ids.size())
            continue;

        int from_node = new_node_ids[from_idx];
        int to_node = new_node_ids[to_idx];
        if (from_node < 0 || to_node < 0)
            continue;

        auto *from_comp = m_components.find(from_node);
        auto *to_comp = m_components.find(to_node);
        if (!from_comp || !to_comp)
            continue;

        int start_pin = from_comp->outputPinId(from_port);
        int end_pin = to_comp->inputPinId(to_port);
        if (start_pin >= 0 && end_pin >= 0)
            m_graph.addLink(start_pin, end_pin);
    }

    // Restore probes
    auto &saved_probes = root["probe_pins"];
    for (const auto &pj : saved_probes) {
        int comp_idx = pj.value("comp", -1);
        int port = pj.value("port", 0);
        bool is_output = pj.value("is_output", true);
        if (comp_idx < 0 || static_cast<size_t>(comp_idx) >= m_components.size())
            continue;
        auto *comp = m_components.all()[comp_idx];
        int pin = is_output ? comp->outputPinId(port) : comp->inputPinId(port);
        if (pin >= 0)
            m_graph.addProbePin(pin);
    }

    // Restore groups
    auto &saved_groups = root["groups"];
    for (const auto &gj : saved_groups) {
        std::string name = gj.value("name", "Group");
        std::vector<int> member_ids;
        for (const auto &mj : gj["member_components"]) {
            int comp_idx = mj.get<int>();
            if (comp_idx >= 0 && static_cast<size_t>(comp_idx) < new_node_ids.size() &&
                new_node_ids[comp_idx] >= 0) {
                member_ids.push_back(new_node_ids[comp_idx]);
            }
        }
        if (member_ids.size() >= 2) {
            int gid = m_graph.addGroup(name, member_ids);
            bool collapsed = gj.value("collapsed", true);
            if (gid >= 0)
                m_graph.setGroupCollapsed(gid, collapsed);
        }
    }

    // Restore window state
    auto &ws = root["window_state"];
    if (!ws.is_null()) {
        m_show_log = ws.value("log", true);
        m_show_spectrum = ws.value("spectrum_analyzer", true);
        m_show_properties = ws.value("properties", true);
        m_show_node_editor = ws.value("node_editor", true);
    }

    // Restore graph state counters
    auto &gs = root["graph_state"];
    if (!gs.is_null()) {
        m_next_component_id = gs.value("next_component_id", m_next_component_id);
    }

    LOG_INFO("Loaded project from %s", path.c_str());
    return true;
}

void ProjectSerializer::reset() {
    // Remove all links from the graph engine first
    m_graph.removeAllLinks();

    // Remove all components — ComponentRegistry handles cleanup
    // Collect IDs first to avoid iterator invalidation
    std::vector<int> ids;
    for (auto *comp : m_components.all())
        ids.push_back(comp->graphNodeId());
    for (int id : ids)
        m_components.remove(id);

    // Clear probes
    m_graph.clearProbes();

    // Reset IQ / PFB widgets
    m_pfb_views.clear();

    // Reset graph counters
    m_graph.setNextIds(1, 100, 1000);
    m_graph.setNextGroupId(50000);
    m_graph.setNextBoundaryPinId(100000);

    m_next_component_id = 100;
    m_graph_widget.clearPositionCache();
}
