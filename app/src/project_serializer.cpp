#include "project_serializer.h"
#include "circuit_runtime.h"
#include "component_registry.h"
#include "component_type_registry.h"
#include "graph_editor_actions.h"
#include "imgui.h"
#include "imnodes.h"
#include "logging_core.h"
#include "network_analyzer_engine.h"
#include "node_graph_engine.h"
#include "node_graph_widget.h"
#include "pfb_view_manager.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
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

// nlohmann::json rejects a valid JSON number such as 1e400 when conversion
// to double overflows. Normalize such tokens only inside the isolated
// receiver_requirements value so its typed validator can report invalid fields.
// The scanner copies quoted strings verbatim.
std::string normalizeNonFiniteJsonNumbers(const std::string &text) {
    std::string normalized;
    normalized.reserve(text.size());
    bool in_string = false;
    bool escaped = false;
    for (std::size_t i = 0; i < text.size();) {
        const char ch = text[i];
        if (in_string) {
            normalized.push_back(ch);
            ++i;
            if (escaped)
                escaped = false;
            else if (ch == '\\')
                escaped = true;
            else if (ch == '"')
                in_string = false;
            continue;
        }
        if (ch == '"') {
            in_string = true;
            normalized.push_back(ch);
            ++i;
            continue;
        }

        if (ch == '-' || (ch >= '0' && ch <= '9')) {
            std::size_t end = i;
            if (text[end] == '-')
                ++end;
            if (end < text.size() && text[end] == '0') {
                ++end;
            } else if (end < text.size() && text[end] >= '1' && text[end] <= '9') {
                do {
                    ++end;
                } while (end < text.size() && text[end] >= '0' && text[end] <= '9');
            } else {
                normalized.push_back(ch);
                ++i;
                continue;
            }
            if (end < text.size() && text[end] == '.') {
                ++end;
                const std::size_t fraction_start = end;
                while (end < text.size() && text[end] >= '0' && text[end] <= '9')
                    ++end;
                if (end == fraction_start) {
                    normalized.push_back(ch);
                    ++i;
                    continue;
                }
            }
            if (end < text.size() && (text[end] == 'e' || text[end] == 'E')) {
                ++end;
                if (end < text.size() && (text[end] == '+' || text[end] == '-'))
                    ++end;
                const std::size_t exponent_start = end;
                while (end < text.size() && text[end] >= '0' && text[end] <= '9')
                    ++end;
                if (end == exponent_start) {
                    normalized.push_back(ch);
                    ++i;
                    continue;
                }
            }

            const std::string token = text.substr(i, end - i);
            char *parsed_end = nullptr;
            const double value = std::strtod(token.c_str(), &parsed_end);
            if (parsed_end == token.c_str() + token.size() && !std::isfinite(value))
                normalized += "null";
            else
                normalized += token;
            i = end;
            continue;
        }

        normalized.push_back(ch);
        ++i;
    }
    return normalized;
}

std::optional<std::size_t> jsonStringEnd(const std::string &text, std::size_t start) {
    if (start >= text.size() || text[start] != '"')
        return std::nullopt;
    for (std::size_t i = start + 1; i < text.size(); ++i) {
        if (text[i] == '\\') {
            if (i + 1 >= text.size())
                return std::nullopt;
            ++i;
        } else if (text[i] == '"') {
            return i + 1;
        }
    }
    return std::nullopt;
}

bool isJsonWhitespace(char ch) { return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r'; }

std::optional<std::size_t> jsonValueEnd(const std::string &text, std::size_t start) {
    if (start >= text.size())
        return std::nullopt;
    if (text[start] == '"')
        return jsonStringEnd(text, start);
    if (text[start] == '{' || text[start] == '[') {
        std::vector<char> closing;
        closing.push_back(text[start] == '{' ? '}' : ']');
        for (std::size_t i = start + 1; i < text.size(); ++i) {
            if (text[i] == '"') {
                const auto end = jsonStringEnd(text, i);
                if (!end)
                    return std::nullopt;
                i = *end - 1;
            } else if (text[i] == '{' || text[i] == '[') {
                closing.push_back(text[i] == '{' ? '}' : ']');
            } else if (text[i] == '}' || text[i] == ']') {
                if (closing.empty() || closing.back() != text[i])
                    return std::nullopt;
                closing.pop_back();
                if (closing.empty())
                    return i + 1;
            }
        }
        return std::nullopt;
    }

    std::size_t end = start;
    while (end < text.size() && !isJsonWhitespace(text[end]) && text[end] != ',' &&
           text[end] != ']' && text[end] != '}')
        ++end;
    return end == start ? std::nullopt : std::optional<std::size_t>(end);
}

std::string normalizeReceiverRequirementsOverflowNumbers(const std::string &text) {
    struct ValueRange {
        std::size_t begin;
        std::size_t end;
    };
    std::vector<ValueRange> values;
    std::size_t cursor = 0;
    // nlohmann accepts a leading UTF-8 BOM; keep it in the text and scan past it.
    if (text.compare(cursor, 3, "\xEF\xBB\xBF") == 0)
        cursor += 3;
    while (cursor < text.size() && isJsonWhitespace(text[cursor]))
        ++cursor;
    if (cursor >= text.size() || text[cursor++] != '{')
        return text;

    while (cursor < text.size()) {
        while (cursor < text.size() && isJsonWhitespace(text[cursor]))
            ++cursor;
        if (cursor < text.size() && text[cursor] == '}')
            break;
        const std::size_t key_start = cursor;
        const auto key_end = jsonStringEnd(text, key_start);
        if (!key_end)
            return text;
        std::string key;
        try {
            key = nlohmann::json::parse(text.substr(key_start, *key_end - key_start))
                      .get<std::string>();
        } catch (const nlohmann::json::exception &) {
            return text;
        }
        cursor = *key_end;
        while (cursor < text.size() && isJsonWhitespace(text[cursor]))
            ++cursor;
        if (cursor >= text.size() || text[cursor++] != ':')
            return text;
        while (cursor < text.size() && isJsonWhitespace(text[cursor]))
            ++cursor;

        const std::size_t value_start = cursor;
        const auto value_end = jsonValueEnd(text, value_start);
        if (!value_end)
            return text;
        if (key == "receiver_requirements")
            values.push_back({value_start, *value_end});
        cursor = *value_end;
        while (cursor < text.size() && isJsonWhitespace(text[cursor]))
            ++cursor;
        if (cursor < text.size() && text[cursor] == ',') {
            ++cursor;
            continue;
        }
        if (cursor < text.size() && text[cursor] == '}')
            break;
        return text;
    }

    if (values.empty())
        return text;
    std::string normalized = text;
    for (auto it = values.rbegin(); it != values.rend(); ++it) {
        const std::string value = text.substr(it->begin, it->end - it->begin);
        normalized.replace(it->begin, it->end - it->begin, normalizeNonFiniteJsonNumbers(value));
    }
    return normalized;
}

ReceiverRequirementsState parseReceiverRequirements(const nlohmann::json &value) {
    ReceiverRequirementsState state;
    const auto invalidate = [&state](std::string reason) {
        state.config.reset();
        state.invalid_reason = std::move(reason);
        return state;
    };
    if (!value.is_object())
        return invalidate("Receiver requirements must be a JSON object.");
    if (value.contains("invalid_configuration")) {
        if (value["invalid_configuration"].is_boolean() &&
            value["invalid_configuration"].get<bool>()) {
            if (value.contains("diagnostic") && value["diagnostic"].is_string() &&
                !value["diagnostic"].get<std::string>().empty())
                return invalidate(value["diagnostic"].get<std::string>());
            return invalidate("Saved receiver requirements are marked invalid.");
        }
        return invalidate("Receiver requirements contain a malformed invalid marker.");
    }

    if (!value.contains("band_start_hz") || !value["band_start_hz"].is_number() ||
        !value.contains("band_stop_hz") || !value["band_stop_hz"].is_number())
        return invalidate("Receiver requirements band endpoints must be numbers.");

    ReceiverRequirementsConfig config;
    config.band_start_Hz = value["band_start_hz"].get<double>();
    config.band_stop_Hz = value["band_stop_hz"].get<double>();
    const auto readOptionalNumber = [&value](const char *key, std::optional<double> &field) {
        if (!value.contains(key))
            return true;
        if (!value[key].is_number())
            return false;
        field = value[key].get<double>();
        return true;
    };

    std::optional<double> gain_minimum;
    std::optional<double> gain_maximum;
    if (!readOptionalNumber("gain_min_db", gain_minimum) ||
        !readOptionalNumber("gain_max_db", gain_maximum))
        return invalidate("Receiver gain limits must be numbers.");
    if (gain_minimum.has_value() != gain_maximum.has_value())
        return invalidate("Receiver gain minimum and maximum must be saved together.");
    if (gain_minimum)
        config.gain = ReceiverGainLimits{*gain_minimum, *gain_maximum};
    if (!readOptionalNumber("nf_max_db", config.nf_max_dB))
        return invalidate("Receiver noise figure limit must be a number.");

    std::optional<double> output_minimum;
    std::optional<double> output_maximum;
    if (!readOptionalNumber("output_power_min_dbm", output_minimum) ||
        !readOptionalNumber("output_power_max_dbm", output_maximum))
        return invalidate("Receiver output power limits must be numbers.");
    if (output_minimum.has_value() != output_maximum.has_value())
        return invalidate("Receiver output power minimum and maximum must be saved together.");
    if (output_minimum)
        config.output_power = ReceiverOutputPowerLimits{*output_minimum, *output_maximum};
    if (!readOptionalNumber("iip3_min_dbm", config.iip3_min_dBm))
        return invalidate("Receiver IIP3 limit must be a number.");

    if (value.contains("measurement_conditions")) {
        const auto &conditions = value["measurement_conditions"];
        if (!conditions.is_object())
            return invalidate("Receiver measurement conditions must be an object.");
        if (conditions.contains("output_reference_tone_frequency_hz")) {
            if (!conditions["output_reference_tone_frequency_hz"].is_number())
                return invalidate("Output reference tone frequency must be a number.");
            config.measurement_conditions.output_reference_tone_frequency_Hz =
                conditions["output_reference_tone_frequency_hz"].get<double>();
        }
        if (conditions.contains("iip3")) {
            const auto &iip3 = conditions["iip3"];
            constexpr std::array<const char *, 4> iip3_keys = {"tone_spacing_hz", "input_start_dbm",
                                                               "input_stop_dbm", "input_step_db"};
            double iip3_fields[4]{};
            if (!iip3.is_object())
                return invalidate("IIP3 test settings must be an object.");
            for (std::size_t i = 0; i < iip3_keys.size(); ++i) {
                if (!iip3.contains(iip3_keys[i]) || !iip3[iip3_keys[i]].is_number())
                    return invalidate(std::string("IIP3 test field '") + iip3_keys[i] +
                                      "' must be a number.");
                iip3_fields[i] = iip3[iip3_keys[i]].get<double>();
            }
            config.measurement_conditions.iip3 = ReceiverIIP3TestSettings{
                iip3_fields[0], iip3_fields[1], iip3_fields[2], iip3_fields[3]};
        }
    }
    if (auto reason = validateReceiverRequirementsConfig(config))
        return invalidate(*reason);
    state.config = config;
    return state;
}

} // namespace

ProjectSerializer::ProjectSerializer(CircuitRuntime &runtime, GraphEditorActions &editor_actions,
                                     NodeGraphWidget &graph_widget, PFBViewManager &pfb_views,
                                     ReceiverRequirementsState &receiver_requirements,
                                     bool &show_log, bool &show_spectrum, bool &show_properties,
                                     bool &show_node_editor, NetworkAnalyzerEngine &na_engine)
    : m_runtime(runtime), m_editor_actions(editor_actions), m_graph_widget(graph_widget),
      m_pfb_views(pfb_views), m_receiver_requirements(receiver_requirements), m_show_log(show_log),
      m_show_spectrum(show_spectrum), m_show_properties(show_properties),
      m_show_node_editor(show_node_editor), m_na_engine(na_engine) {}

const ComponentRegistry &ProjectSerializer::components() const { return m_runtime.components(); }

const NodeGraphEngine &ProjectSerializer::graph() const { return m_runtime.graph(); }

std::array<ProjectSerializer::WindowFlag, 4> ProjectSerializer::windowFlags() {
    return {{{"log", &m_show_log},
             {"spectrum_analyzer", &m_show_spectrum},
             {"properties", &m_show_properties},
             {"node_editor", &m_show_node_editor}}};
}

bool ProjectSerializer::save(const std::string &path) {
    nlohmann::json root;
    root["version"] = 1;
    if (m_receiver_requirements.config) {
        const auto &config = *m_receiver_requirements.config;
        nlohmann::json requirements = {{"band_start_hz", config.band_start_Hz},
                                       {"band_stop_hz", config.band_stop_Hz}};
        if (config.gain) {
            requirements["gain_min_db"] = config.gain->minimum_dB;
            requirements["gain_max_db"] = config.gain->maximum_dB;
        }
        if (config.nf_max_dB)
            requirements["nf_max_db"] = *config.nf_max_dB;
        if (config.output_power) {
            requirements["output_power_min_dbm"] = config.output_power->minimum_dBm;
            requirements["output_power_max_dbm"] = config.output_power->maximum_dBm;
        }
        if (config.iip3_min_dBm)
            requirements["iip3_min_dbm"] = *config.iip3_min_dBm;
        nlohmann::json conditions = nlohmann::json::object();
        if (config.measurement_conditions.output_reference_tone_frequency_Hz)
            conditions["output_reference_tone_frequency_hz"] =
                *config.measurement_conditions.output_reference_tone_frequency_Hz;
        if (config.measurement_conditions.iip3) {
            const auto &settings = *config.measurement_conditions.iip3;
            conditions["iip3"] = {{"tone_spacing_hz", settings.tone_spacing_Hz},
                                  {"input_start_dbm", settings.input_start_dBm},
                                  {"input_stop_dbm", settings.input_stop_dBm},
                                  {"input_step_db", settings.input_step_dB}};
        }
        if (!conditions.empty())
            requirements["measurement_conditions"] = std::move(conditions);
        root["receiver_requirements"] = std::move(requirements);
    } else if (!m_receiver_requirements.invalid_reason.empty()) {
        root["receiver_requirements"] = {{"invalid_configuration", true},
                                         {"diagnostic", m_receiver_requirements.invalid_reason}};
    }

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
    for (auto *comp : components().all()) {
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
        for (const auto &gn : graph().nodes()) {
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
    for (size_t i = 0; i < components().size(); ++i) {
        auto *comp = components().all()[i];
        int nid = comp->graphNodeId();
        for (const auto &gn : graph().nodes()) {
            if (gn.node_id == nid) {
                for (size_t p = 0; p < gn.input_pin_ids.size(); ++p)
                    pin_map[gn.input_pin_ids[p]] = {i, (int)p, false};
                for (size_t p = 0; p < gn.output_pin_ids.size(); ++p)
                    pin_map[gn.output_pin_ids[p]] = {i, (int)p, true};
                break;
            }
        }
    }
    for (const auto &link : graph().links()) {
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
    for (int probe_pin : graph().probePins()) {
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
    for (size_t i = 0; i < components().size(); ++i)
        nid_to_comp[components().all()[i]->graphNodeId()] = i;

    for (const auto &g : graph().groups()) {
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

    // Window state (canonical flag list shared with the load-time shape guard
    // and restore — see windowFlags()).
    for (const auto &[key, member] : windowFlags())
        root["window_state"][key] = *member;

    // Graph state counters (for later additions)
    root["graph_state"]["next_component_id"] = m_runtime.nextComponentId();

    // Atomic save (issue #113): the previous contents must survive a failed
    // write so issue #77's retry contract never retries against a truncated
    // file. Serialize into a sibling "<path>.tmp", flush/close it, and only
    // then rename it over the target. A failure before the rename leaves the
    // original byte-identical; the fixed sibling name is what the regression
    // test blocks to force a write failure deterministically.
    const fs::path target(path);
    fs::path temp = target;
    temp += ".tmp";
    const std::string temp_str = temp.string();

    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) {
        LOG_ERROR("Failed to open project file for writing: %s", temp_str.c_str());
        return false;
    }
    out << root.dump(2);
    out.flush();
    if (!out) {
        LOG_ERROR("Failed to write project file: %s", temp_str.c_str());
        out.close();
        std::error_code rm_ec;
        fs::remove(temp, rm_ec);
        return false;
    }
    out.close();
    if (!out) {
        LOG_ERROR("Failed to close project file: %s", temp_str.c_str());
        std::error_code rm_ec;
        fs::remove(temp, rm_ec);
        return false;
    }

    // std::filesystem::rename atomically replaces an existing target on every
    // supported platform (POSIX rename / Windows MoveFileEx with
    // MOVEFILE_REPLACE_EXISTING), so there is deliberately no remove+rename
    // fallback: deleting the target first would reintroduce the data-loss
    // window this function exists to close. If the rename fails the original
    // is still intact — drop the temp and report the failure.
    std::error_code ec;
    fs::rename(temp, target, ec);
    if (ec) {
        LOG_ERROR("Failed to replace project file %s: %s", path.c_str(), ec.message().c_str());
        std::error_code rm_ec;
        fs::remove(temp, rm_ec);
        return false;
    }

    LOG_INFO("Saved project to %s", path.c_str());
    return true;
}

bool ProjectSerializer::load(const std::string &path) {
    m_last_load_reset = false;
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
        const std::string source((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
        try {
            root = nlohmann::json::parse(source);
        } catch (const nlohmann::json::exception &) {
            const std::string normalized = normalizeReceiverRequirementsOverflowNumbers(source);
            if (normalized == source)
                throw;
            root = nlohmann::json::parse(normalized);
        }
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
            m_last_load_reset = true;
            reset();
            return false;
        }

        // Field-level shape guard for the two singleton sections, validated
        // here (before reset) so an invalid optional scalar is *rejected* and
        // cannot mutate the live project: a corrupt UI flag or counter must not
        // discard the project the user already has open (issue #113). This
        // deliberately differs from the section-shape failure above, which
        // resets because no coherent project can be recovered from it.
        const auto window_state_ok = [&]() -> bool {
            if (!root.contains("window_state") || root["window_state"].is_null())
                return true;
            const auto &ws = root["window_state"];
            for (const auto &flag : windowFlags()) {
                const char *key = flag.first;
                if (ws.contains(key) && !ws[key].is_boolean()) {
                    LOG_ERROR("Invalid project file %s: 'window_state.%s' must be a boolean",
                              path.c_str(), key);
                    return false;
                }
            }
            return true;
        };
        const auto graph_state_ok = [&]() -> bool {
            if (!root.contains("graph_state") || root["graph_state"].is_null())
                return true;
            const auto &gs = root["graph_state"];
            if (gs.contains("next_component_id")) {
                const auto v = checkedJsonInt(gs["next_component_id"]);
                if (!v || *v < 0) {
                    LOG_ERROR("Invalid project file %s: 'graph_state.next_component_id' must be a "
                              "non-negative integer within int range",
                              path.c_str());
                    return false;
                }
            }
            return true;
        };
        if (!window_state_ok() || !graph_state_ok()) {
            // Leave m_last_load_reset false: the live project and its current
            // save path are untouched, so the caller keeps them.
            return false;
        }

        m_last_load_reset = true;
        reset();
        if (root.contains("receiver_requirements")) {
            m_receiver_requirements = parseReceiverRequirements(root["receiver_requirements"]);
            if (!m_receiver_requirements.invalid_reason.empty())
                LOG_WARN("Invalid receiver requirements in project file %s: %s", path.c_str(),
                         m_receiver_requirements.invalid_reason.c_str());
        }

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
                comp = m_runtime.createComponent(desc->create);
                // S1: resolve S-param paths against the project file's
                // directory and neutralize any path that escapes it (the
                // engine's deserialize() only sees the raw params JSON and
                // cannot know the project dir).
                resolveSparamParams(params, project_dir);
                comp->deserialize(params);

                // Restore position. Malformed optional metadata must not abort
                // an otherwise valid component, so only read the numeric fields
                // when their shapes actually match. The position is always
                // applied (defaulting to the origin) so every loaded node is
                // registered in the imnodes pool — captureGridPositions() and
                // saveProject() read it back without asserting.
                ImVec2 restored_pos(0.0f, 0.0f);
                if (cj.contains("pos") && cj["pos"].is_object()) {
                    const auto &pos = cj["pos"];
                    if (pos.contains("x") && pos["x"].is_number())
                        restored_pos.x = pos["x"].get<float>();
                    if (pos.contains("y") && pos["y"].is_number())
                        restored_pos.y = pos["y"].get<float>();
                }
                ImNodes::EditorContextSet(m_graph_widget.context());
                ImNodes::SetNodeEditorSpacePos(comp->graphNodeId(), restored_pos);

                // Restore library part number (only when it is a string)
                if (cj.contains("part_number") && cj["part_number"].is_string())
                    m_editor_actions.setNodePartNumber(comp->graphNodeId(),
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
                // partially created component so a malformed record is neither
                // counted nor linked while valid sibling components still load.
                // Component-bound views are synced by the app after load().
                if (comp)
                    m_runtime.removeComponent(comp->graphNodeId());
                new_node_ids.push_back(-1);
            }
        }
        // Snapshot every loaded node's grid position before they can be dropped
        // from the imnodes pool: a collapsed group's members are never drawn, so
        // the widget needs this cache to render their block (issue #116).
        m_graph_widget.captureGridPositions();
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

            auto *from_comp = components().find(from_node);
            auto *to_comp = components().find(to_node);
            if (!from_comp || !to_comp)
                continue;

            const int start_pin = from_comp->outputPinId(from_port);
            const int end_pin = to_comp->inputPinId(to_port);
            if (start_pin >= 0 && end_pin >= 0 && !m_runtime.connect(start_pin, end_pin))
                LOG_WARN("Skipping invalid link in project file %s", path.c_str());
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
            auto *comp = components().find(node_id);
            if (!comp)
                continue; // no component for this mapping; nothing to probe
            int pin = is_output ? comp->outputPinId(port) : comp->inputPinId(port);
            if (pin >= 0)
                m_editor_actions.addProbePin(pin);
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
                auto *comp = components().find(node_id);
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
                const int group_id = m_editor_actions.createGroup(name, member_ids);
                if (group_id >= 0)
                    m_editor_actions.setGroupCollapsed(group_id, collapsed);
            }
        }
        // Rebuild derived group boundaries once, after all restored topology and groups exist.
        m_editor_actions.topologyChanged();

        // Restore window state. These fields were shape-validated before
        // reset(), so a present key is guaranteed boolean; an absent key keeps
        // the section default (true). Null sections leave the live values.
        auto &ws = root["window_state"];
        if (!ws.is_null()) {
            // An absent flag keeps the section default (true). Presence was
            // shape-validated before reset(), so get<bool>() cannot throw.
            for (const auto &[key, member] : windowFlags())
                *member = ws.contains(key) ? ws[key].get<bool>() : true;
        }

        // Restore the saved counter without losing IDs consumed during project
        // reconstruction. Pre-validation guarantees a present next_component_id
        // is a non-negative int, so no wrap or truncation is possible.
        auto &gs = root["graph_state"];
        if (!gs.is_null() && gs.contains("next_component_id")) {
            m_runtime.setNextComponentId(
                std::max(gs["next_component_id"].get<int>(), m_runtime.nextComponentId()));
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
    // Views hold engine references: drop them before the engines are destroyed.
    m_pfb_views.clear();
    m_runtime.clearComponentsAndResetIds();
    m_editor_actions.resetForProjectReplacement();
    m_receiver_requirements = {};

    // Clear the Network Analyzer's probe points too — otherwise a stale pin
    // id survives into the next project and can alias a newly allocated pin.
    m_na_engine.setPointA(-1);
    m_na_engine.setPointB(-1);

    m_graph_widget.clearPositionCache();
}
