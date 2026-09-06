#include "project_serializer.h"
#include "component_registry.h"
#include "component_type_registry.h"
#include "graph_link_policy.h"
#include "imgui.h"
#include "imnodes.h"
#include "logging_core.h"
#include "network_analyzer_engine.h"
#include "node_graph_engine.h"
#include "node_graph_widget.h"
#include "pfb_channelizer_engine.h"
#include "pfb_view_manager.h"
#include "session_state.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

namespace fs = std::filesystem;

// --- S-param path containment (S1) -----------------------------------------
// Mirrors extension_manifest.cpp's resolveWithinRoot discipline: an S-param
// path read from an untrusted project file is only honored if its canonical
// form stays inside the project file's directory. Absolute paths outside the
// project dir, '..' traversal, and unresolvable paths are neutralized at the
// load boundary before any engine deserializes them.

bool containsParentTraversal(const fs::path &path) {
    for (const auto &part : path) {
        if (part == "..")
            return true;
    }
    return false;
}

bool pathWithinRoot(const fs::path &root, const fs::path &candidate) {
    std::error_code ec;
    const fs::path canonical_root = fs::weakly_canonical(root, ec);
    if (ec)
        return false;

    ec.clear();
    const fs::path canonical_candidate = fs::weakly_canonical(candidate, ec);
    if (ec)
        return false;

    auto root_it = canonical_root.begin();
    auto candidate_it = canonical_candidate.begin();
    for (; root_it != canonical_root.end(); ++root_it, ++candidate_it) {
        if (candidate_it == canonical_candidate.end() || *root_it != *candidate_it)
            return false;
    }
    return true;
}

// Resolve an S-param path from an untrusted project file against the project
// directory. Returns the canonical absolute path on success, or nullopt when
// the path must be neutralized (absolute outside the project dir, '..'
// traversal, or unresolvable).
std::optional<std::string> resolveSparamPath(const fs::path &project_dir,
                                             const std::string &input) {
    const fs::path p(input);
    if (p.empty())
        return std::nullopt;
    if (containsParentTraversal(p))
        return std::nullopt;

    const fs::path candidate = p.is_absolute() ? p : (project_dir / p);
    if (!pathWithinRoot(project_dir, candidate))
        return std::nullopt;

    std::error_code ec;
    const fs::path resolved = fs::weakly_canonical(candidate, ec);
    if (ec)
        return std::nullopt;
    return resolved.string();
}

// Load boundary: rewrite S-param path params in-place. Contained paths are
// resolved to their canonical absolute form (the engine loads them, and the
// save boundary re-relativizes them for round-trip); paths that escape the
// project dir are neutralized to "" with a warning.
void resolveSparamParams(nlohmann::json &params, const fs::path &project_dir) {
    if (!params.is_object())
        return;
    for (const char *key : {"sparam_filepath", "sparam_path"}) {
        if (!params.contains(key) || !params[key].is_string())
            continue;
        const std::string value = params[key].get<std::string>();
        if (value.empty())
            continue;
        if (const auto resolved = resolveSparamPath(project_dir, value)) {
            params[key] = *resolved;
        } else {
            LOG_WARN("Project S-param path rejected (outside project dir): %s", value.c_str());
            params[key] = "";
        }
    }
}

// Save boundary: keep the project file portable by persisting S-param paths
// relative to the project directory. Absolute paths the user configured that
// stay inside the project dir are re-written relative; anything else is left
// untouched (load-side containment already guards untrusted project files).
void relativizeSparamParams(nlohmann::json &params, const fs::path &project_dir) {
    if (!params.is_object())
        return;
    for (const char *key : {"sparam_filepath", "sparam_path"}) {
        if (!params.contains(key) || !params[key].is_string())
            continue;
        const fs::path p(params[key].get<std::string>());
        if (!p.is_absolute() || !pathWithinRoot(project_dir, p))
            continue;
        std::error_code ec;
        const fs::path rel = fs::relative(p, project_dir, ec);
        if (ec)
            continue;
        params[key] = rel.generic_string();
    }
}

// Checked conversion of a JSON integer to int. is_number_integer() alone
// only guarantees the value is stored as an integer type: a number_unsigned
// larger than INT_MAX (or a number_integer below INT_MIN) would silently
// truncate/wrap in get<int>(). Verify representability against int limits
// with the nlohmann signed/unsigned integer APIs before converting; returns
// nullopt for non-integers and out-of-range values (callers log and skip).
std::optional<int> checkedJsonInt(const nlohmann::json &j) {
    if (j.is_number_unsigned()) {
        const std::uint64_t v = j.get<std::uint64_t>();
        if (v <= static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
            return static_cast<int>(v);
    } else if (j.is_number_integer()) {
        const std::int64_t v = j.get<std::int64_t>();
        if (v >= static_cast<std::int64_t>(std::numeric_limits<int>::min()) &&
            v <= static_cast<std::int64_t>(std::numeric_limits<int>::max()))
            return static_cast<int>(v);
    }
    return std::nullopt;
}

} // namespace

ProjectSerializer::ProjectSerializer(ComponentRegistry &components, NodeGraphEngine &graph,
                                     NodeGraphWidget &graph_widget, PFBViewManager &pfb_views,
                                     SessionState &state, int &next_component_id, bool &show_log,
                                     bool &show_spectrum, bool &show_properties,
                                     bool &show_node_editor, NetworkAnalyzerEngine &na_engine)
    : m_components(components), m_graph(graph), m_graph_widget(graph_widget),
      m_pfb_views(pfb_views), m_state(state), m_next_component_id(next_component_id),
      m_show_log(show_log), m_show_spectrum(show_spectrum), m_show_properties(show_properties),
      m_show_node_editor(show_node_editor), m_na_engine(na_engine) {}

bool ProjectSerializer::save(const std::string &path) {
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
    // S1: S-param paths are persisted relative to the project dir for portability.
    const fs::path save_project_dir = fs::absolute(fs::path(path)).parent_path();
    for (auto *comp : m_components.all()) {
        nlohmann::json cj;
        const auto *desc = ComponentTypeRegistry::instance().find(comp->type_name());
        cj["type"] = desc ? desc->project_type : "Unknown";
        cj["params"] = comp->serialize();
        relativizeSparamParams(cj["params"], save_project_dir);

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

    // Save the singleton Network Analyzer instrument state: the four sweep
    // params plus Point A/B as {comp, port, is_output} pairs, using the same
    // pin_map machinery as probe_pins (the engine-level serialize() stores raw
    // pin ids, which are not portable across graph rebuilds on load).
    nlohmann::json na_json;
    na_json["start_freq_hz"] = m_na_engine.startFrequency();
    na_json["stop_freq_hz"] = m_na_engine.stopFrequency();
    na_json["points"] = m_na_engine.points();
    na_json["stimulus_power_dBm"] = m_na_engine.stimulusPower();
    const auto pin_as_comp_port = [&](int pin_id) -> nlohmann::json {
        auto it = pin_map.find(pin_id);
        if (it == pin_map.end())
            return nullptr;
        nlohmann::json pj;
        pj["comp"] = it->second.comp;
        pj["port"] = it->second.port;
        pj["is_output"] = it->second.is_output;
        return pj;
    };
    na_json["point_a"] = pin_as_comp_port(m_na_engine.pointAPin());
    na_json["point_b"] = pin_as_comp_port(m_na_engine.pointBPin());
    root["network_analyzer"] = na_json;

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
        return false;
    }
    out << root.dump(2);
    out.flush();
    if (!out) {
        LOG_ERROR("Failed to write project file: %s", path.c_str());
        return false;
    }
    out.close();
    if (!out) {
        LOG_ERROR("Failed to close project file: %s", path.c_str());
        return false;
    }

    LOG_INFO("Saved project to %s", path.c_str());
    return true;
}

bool ProjectSerializer::load(const std::string &path) {
    std::ifstream in(path);
    if (!in) {
        LOG_ERROR("Failed to open project file: %s", path.c_str());
        return false;
    }
    // Reject oversized files before parsing (e.g. a truncated or corrupted
    // file could otherwise balloon memory during parse).
    in.seekg(0, std::ios::end);
    const std::streamoff file_size = in.tellg();
    if (file_size > 64 * 1024 * 1024) {
        LOG_ERROR("Project file too large to load (%lld bytes): %s",
                  static_cast<long long>(file_size), path.c_str());
        return false;
    }
    in.seekg(0, std::ios::beg);

    // S1: the project file's directory is the containment root for S-param
    // paths referenced from this project.
    const fs::path project_dir = fs::absolute(fs::path(path)).parent_path();

    nlohmann::json root;
    try {
        in >> root;
    } catch (const nlohmann::json::exception &e) {
        LOG_ERROR("Invalid project file: %s", e.what());
        return false;
    }
    if (!root.is_object()) {
        LOG_ERROR("Invalid project file (root is not a JSON object): %s", path.c_str());
        return false;
    }

    try {
        // Validate optional top-level section shapes before resetting any
        // state: a wrong-shaped section (e.g. "components": 5) must fail the
        // load cleanly instead of throwing mid-restore. Missing fields and
        // explicit nulls retain their existing defaults.
        const auto require_array = [&](const char *key) -> bool {
            if (root.contains(key) && !root[key].is_null() && !root[key].is_array()) {
                LOG_ERROR("Invalid project file %s: '%s' must be an array", path.c_str(), key);
                return false;
            }
            return true;
        };
        const auto require_object = [&](const char *key) -> bool {
            if (root.contains(key) && !root[key].is_null() && !root[key].is_object()) {
                LOG_ERROR("Invalid project file %s: '%s' must be an object", path.c_str(), key);
                return false;
            }
            return true;
        };
        if (!require_array("components") || !require_array("links") ||
            !require_array("probe_pins") || !require_array("groups") ||
            !require_object("network_analyzer") || !require_object("window_state") ||
            !require_object("graph_state")) {
            // A wrong-shaped top-level section makes the file unusable. Reset
            // to the empty state a normal load would produce (regression tests
            // assert a fresh app ends with zero components), then fail. The
            // checks above ran before any reset, so restoration never sees a
            // partially validated file.
            reset();
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
        size_t comp_index = 0;
        for (auto &it : comp_order) {
            auto &cj = *it;
            const size_t current_index = comp_index++;
            // One malformed component must not abort the whole load: validate
            // the record shape before any typed access (must be an object with
            // a string 'type' and, if present, object 'params'), log the saved
            // index, and skip it — new_node_ids keeps the saved-index → node
            // mapping intact with -1 so link/probe/group restoration stays in
            // step and valid sibling components still load.
            if (!cj.is_object() || !cj.contains("type") || !cj["type"].is_string()) {
                LOG_WARN("Skipping component %zu in project file %s: record must be an object "
                         "with a string 'type'",
                         current_index, path.c_str());
                new_node_ids.push_back(-1);
                continue;
            }
            if (cj.contains("params") && !cj["params"].is_object()) {
                LOG_WARN("Skipping component %zu in project file %s: 'params' must be an object",
                         current_index, path.c_str());
                new_node_ids.push_back(-1);
                continue;
            }
            const ComponentTypeDescriptor *desc = nullptr;
            IComponentEngine *comp = nullptr;
            try {
                std::string type = cj["type"].get<std::string>();
                // Absent 'params' keeps the existing behavior: operator[] yields
                // a JSON null, which the engine deserialize() treats as defaults.
                auto &params = cj["params"];

                desc = ComponentTypeRegistry::instance().findByProjectType(type);
                if (!desc) {
                    LOG_WARN("Unknown component type in project file: %s", type.c_str());
                    new_node_ids.push_back(-1);
                    continue;
                }
                comp = desc->create(m_components, m_graph, m_next_component_id++);
                // S1: resolve S-param paths against the project file's
                // directory and neutralize any path that escapes it (the
                // engine's deserialize() only sees the raw params JSON and
                // cannot know the project dir).
                resolveSparamParams(params, project_dir);
                comp->deserialize(params);
                if (desc->type == "pfb") {
                    // Restore IQ plot + PFB grid widgets for this PFB
                    m_pfb_views.addFor(*static_cast<PFBChannelizerEngine *>(comp), m_state);
                }

                // Restore position. Malformed optional metadata must not abort
                // an otherwise valid component, so only read the numeric fields
                // when their shapes actually match.
                if (cj.contains("pos") && cj["pos"].is_object()) {
                    const auto &pos = cj["pos"];
                    const float pos_x =
                        pos.contains("x") && pos["x"].is_number() ? pos["x"].get<float>() : 0.0f;
                    const float pos_y =
                        pos.contains("y") && pos["y"].is_number() ? pos["y"].get<float>() : 0.0f;
                    ImNodes::EditorContextSet(m_graph_widget.context());
                    ImNodes::SetNodeEditorSpacePos(comp->graphNodeId(), ImVec2(pos_x, pos_y));
                }

                // Restore library part number (only when it is a string)
                if (cj.contains("part_number") && cj["part_number"].is_string())
                    m_graph.setNodePartNumber(comp->graphNodeId(),
                                              cj["part_number"].get<std::string>());

                // Record the saved-index → node mapping only after every step
                // that can throw, so the saved index stays in step with the
                // file and the catch below pushes exactly one -1 per record.
                new_node_ids.push_back(comp->graphNodeId());
            } catch (const std::exception &e) {
                LOG_ERROR("Skipping malformed component %zu in project file %s: %s", current_index,
                          path.c_str(), e.what());
                // Exception-safe rollback: desc->create() already registered
                // the component (and its graph node) before nested
                // deserialization or metadata restoration threw. Remove the
                // partially created component and any PFB view state created
                // for it, so a malformed record is neither counted nor linked
                // while valid sibling components still load.
                if (comp) {
                    m_components.remove(comp->graphNodeId());
                    if (desc && desc->type == "pfb")
                        m_pfb_views.rebuild(m_components, m_state);
                }
                new_node_ids.push_back(-1);
            }
        }
        // After restoring all positions, inform the widget so subsequent
        // syncNodesFromEngine calls (e.g. from saveProject) don't reset them.
        m_graph_widget.markNodesRegistered();

        // Restore links (saved as component-index + port pairs). A malformed
        // entry is logged and skipped so one bad link cannot abort restoration
        // of the remaining links (and everything after them).
        auto &saved_links = root["links"];
        for (const auto &lj : saved_links) {
            bool malformed = !lj.is_object();
            if (malformed) {
                LOG_WARN("Skipping malformed link in project file %s: entry is not an object",
                         path.c_str());
                continue;
            }
            const auto index_field = [&](const char *key, int fallback) {
                if (lj.contains(key)) {
                    if (const auto v = checkedJsonInt(lj[key]))
                        return *v;
                    malformed = true;
                    return fallback;
                }
                return fallback;
            };
            const int from_idx = index_field("from", -1);
            const int to_idx = index_field("to", -1);
            const int from_port = index_field("from_port", 0);
            const int to_port = index_field("to_port", 0);
            if (malformed) {
                LOG_WARN("Skipping malformed link in project file %s", path.c_str());
                continue;
            }
            if (from_idx < 0 || to_idx < 0 ||
                static_cast<size_t>(from_idx) >= new_node_ids.size() ||
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
            if (start_pin >= 0 && end_pin >= 0 &&
                graphLinkAllowed(from_comp, to_comp, start_pin, end_pin)) {
                m_graph.addLink(start_pin, end_pin);
            } else if (start_pin >= 0 && end_pin >= 0) {
                LOG_WARN("Skipping physically invalid link in project file %s", path.c_str());
            }
        }

        // Restore probes. Malformed entries are logged and skipped so valid
        // probes (and all later sections) still restore.
        auto &saved_probes = root["probe_pins"];
        for (const auto &pj : saved_probes) {
            bool malformed = !pj.is_object();
            if (malformed) {
                LOG_WARN("Skipping malformed probe in project file %s: entry is not an object",
                         path.c_str());
                continue;
            }
            const auto index_field = [&](const char *key, int fallback) {
                if (pj.contains(key)) {
                    if (const auto v = checkedJsonInt(pj[key]))
                        return *v;
                    malformed = true;
                    return fallback;
                }
                return fallback;
            };
            const int comp_idx = index_field("comp", -1);
            const int port = index_field("port", 0);
            if (pj.contains("is_output") && !pj["is_output"].is_boolean())
                malformed = true;
            const bool is_output = pj.contains("is_output") && pj["is_output"].is_boolean()
                                       ? pj["is_output"].get<bool>()
                                       : true;
            if (malformed) {
                LOG_WARN("Skipping malformed probe in project file %s", path.c_str());
                continue;
            }
            // Resolve the saved component index through new_node_ids (the same
            // mapping the links and network-analyzer passes use) instead of the
            // compact registry: a component skipped earlier in the file must
            // not shift a later valid probe onto the wrong component or drop it
            // because the compacted registry has fewer entries.
            if (comp_idx < 0 || static_cast<size_t>(comp_idx) >= new_node_ids.size())
                continue;
            const int node_id = new_node_ids[static_cast<size_t>(comp_idx)];
            if (node_id < 0)
                continue; // saved index maps to a skipped/malformed record
            auto *comp = m_components.find(node_id);
            if (!comp)
                continue; // no component for this mapping; nothing to probe
            int pin = is_output ? comp->outputPinId(port) : comp->inputPinId(port);
            if (pin >= 0)
                m_graph.addProbePin(pin);
        }

        // Restore the singleton Network Analyzer instrument state: the four
        // sweep params plus Point A/B {comp, port, is_output} pairs. Points
        // resolve through new_node_ids (like the links pass) so a skipped
        // component elsewhere in the file cannot shift the index mapping.
        // Absent keys keep the engine's current (default or last-set) value.
        auto &saved_na = root["network_analyzer"];
        if (!saved_na.is_null()) {
            // Step 1 guarantees 'network_analyzer' is an object; guard each
            // field so a wrong-typed value cannot abort restoration (absent
            // keys keep the engine's current value).
            const auto number_field = [&](const char *key, double fallback) {
                return saved_na.contains(key) && saved_na[key].is_number()
                           ? saved_na[key].get<double>()
                           : fallback;
            };
            m_na_engine.setStartFrequency(
                number_field("start_freq_hz", m_na_engine.startFrequency()));
            m_na_engine.setStopFrequency(number_field("stop_freq_hz", m_na_engine.stopFrequency()));
            // 'points' must be an integer representable in int: a fractional
            // value would silently truncate and an oversized integer would
            // wrap, so both leave the engine's current value untouched.
            if (saved_na.contains("points")) {
                if (const auto n = checkedJsonInt(saved_na["points"])) {
                    m_na_engine.setPoints(*n);
                } else {
                    LOG_WARN("Ignoring malformed 'points' in project file %s: expected an integer "
                             "within int range; keeping current value (%d)",
                             path.c_str(), m_na_engine.points());
                }
            }
            m_na_engine.setStimulusPower(
                number_field("stimulus_power_dBm", m_na_engine.stimulusPower()));
            const auto restore_point = [&](const nlohmann::json &pj,
                                           void (NetworkAnalyzerEngine::*set)(int)) {
                if (!pj.is_object())
                    return; // unset point (saved as JSON null)
                bool malformed = false;
                const auto index_field = [&](const char *key, int fallback) {
                    if (pj.contains(key)) {
                        if (const auto v = checkedJsonInt(pj[key]))
                            return *v;
                        malformed = true;
                        return fallback;
                    }
                    return fallback;
                };
                const int comp_idx = index_field("comp", -1);
                const int port = index_field("port", 0);
                if (pj.contains("is_output") && !pj["is_output"].is_boolean())
                    malformed = true;
                const bool is_output = pj.contains("is_output") && pj["is_output"].is_boolean()
                                           ? pj["is_output"].get<bool>()
                                           : true;
                if (malformed) {
                    LOG_WARN("Skipping malformed network analyzer point in project file %s",
                             path.c_str());
                    return;
                }
                if (comp_idx < 0 || static_cast<size_t>(comp_idx) >= new_node_ids.size())
                    return;
                const int node_id = new_node_ids[static_cast<size_t>(comp_idx)];
                if (node_id < 0)
                    return;
                auto *comp = m_components.find(node_id);
                if (!comp)
                    return;
                const int pin = is_output ? comp->outputPinId(port) : comp->inputPinId(port);
                if (pin >= 0)
                    (m_na_engine.*set)(pin);
            };
            const nlohmann::json no_pin = nullptr;
            restore_point(saved_na.contains("point_a") ? saved_na["point_a"] : no_pin,
                          &NetworkAnalyzerEngine::setPointA);
            restore_point(saved_na.contains("point_b") ? saved_na["point_b"] : no_pin,
                          &NetworkAnalyzerEngine::setPointB);
        }

        // Restore groups. Malformed entries are logged and skipped so one bad
        // group cannot discard valid groups (or components restored earlier).
        auto &saved_groups = root["groups"];
        for (const auto &gj : saved_groups) {
            bool malformed = !gj.is_object();
            if (malformed) {
                LOG_WARN("Skipping malformed group in project file %s: entry is not an object",
                         path.c_str());
                continue;
            }
            std::string name = "Group";
            if (gj.contains("name")) {
                if (!gj["name"].is_string())
                    malformed = true;
                else
                    name = gj["name"].get<std::string>();
            }
            std::vector<int> member_ids;
            if (!gj.contains("member_components") || !gj["member_components"].is_array()) {
                malformed = true;
            } else {
                for (const auto &mj : gj["member_components"]) {
                    const auto comp_idx = checkedJsonInt(mj);
                    if (!comp_idx) {
                        malformed = true;
                        continue;
                    }
                    if (*comp_idx >= 0 && static_cast<size_t>(*comp_idx) < new_node_ids.size() &&
                        new_node_ids[*comp_idx] >= 0) {
                        member_ids.push_back(new_node_ids[*comp_idx]);
                    }
                }
            }
            bool collapsed = true;
            if (gj.contains("collapsed")) {
                if (!gj["collapsed"].is_boolean())
                    malformed = true;
                else
                    collapsed = gj["collapsed"].get<bool>();
            }
            if (malformed) {
                LOG_WARN("Skipping malformed group in project file %s", path.c_str());
                continue;
            }
            if (member_ids.size() >= 2) {
                int gid = m_graph.addGroup(name, member_ids);
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
    } catch (const std::exception &e) {
        // Broadened from nlohmann::json::exception: any exception escaping
        // restoration (including non-JSON engine/container errors on
        // malformed input) is converted into a logged false result instead of
        // propagating. Deliberately not catch (...) — std::exception covers
        // the expected failure modes and keeps the failure observable.
        LOG_ERROR("Malformed project file %s: %s", path.c_str(), e.what());
        return false;
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

    // Clear the Network Analyzer's probe points too — otherwise a stale pin
    // id survives into the next project, and since pin ids are reallocated
    // deterministically from the same base below, it can silently alias an
    // unrelated pin belonging to a different component (issue found in
    // review: load()'s restore only *sets* Point A/B when present in the
    // save file, so an absent/unset point left the previous project's pin
    // id in place).
    m_na_engine.setPointA(-1);
    m_na_engine.setPointB(-1);

    // Reset IQ / PFB widgets
    m_pfb_views.clear();

    // Reset graph counters
    m_graph.setNextIds(1, 100, 1000);
    m_graph.setNextGroupId(50000);
    m_graph.setNextBoundaryPinId(100000);

    m_next_component_id = 100;
    m_graph_widget.clearPositionCache();
}
