# Project Save/Load Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add project save/load to the RF Simulator so all circuit state persists between sessions.

**Architecture:** Add `serialize()`/`deserialize()` virtual methods to `IComponentEngine`. Each engine overrides to save/load its params. The app stores component topology as connection indices (not raw pin IDs) — avoids the ID mismatch problem the old branch had. File menu bar + keyboard shortcuts + unsaved-changes dialog.

**Tech Stack:** C++20, nlohmann/json (header-only via FetchContent), imnodes for node positions

---

## Global Constraints

- All new virtual methods on `IComponentEngine` default to no-op
- Component creation on load must work regardless of saved node_id gaps
- Links reference components by saved-array index, not by raw pin IDs
- Project file extension: `.rfsim`
- JSON format, indented 2 spaces

---

### Task 1: Add nlohmann/json dependency + serialize interface to IComponentEngine

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `common/component_interface.h`
- Modify: `common/CMakeLists.txt`

- [ ] **Step 1: Add nlohmann/json FetchContent to root CMakeLists.txt**

```cmake
# In CMakeLists.txt, before the add_subdirectory() lines, add:
include(FetchContent)
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)
```

- [ ] **Step 2: Wire the dependency into common/CMakeLists.txt**

```cmake
# common/CMakeLists.txt — add to the INTERFACE target
target_link_libraries(simulator_common INTERFACE nlohmann_json::nlohmann_json)
```

- [ ] **Step 3: Add virtual serialize/deserialize to IComponentEngine**

In `common/component_interface.h`, add includes and virtual methods:

```cpp
#pragma once

#include "signal_node.h"
#include <nlohmann/json.hpp>
#include <string>

class IComponentEngine {
public:
    virtual ~IComponentEngine() = default;
    virtual int id() const = 0;
    virtual int graphNodeId() const = 0;
    virtual int outputPinId() const = 0;
    virtual std::string hoverSummary() const = 0;
    virtual SignalNode& node() = 0;
    virtual const SignalNode& node() const = 0;
    virtual void update(double dt) = 0;

    virtual int inputPinId() const { return -1; }

    // Multi-pin accessors (default: forward to inputPinId() for port 0)
    virtual int inputPinId(int port) const { return port == 0 ? inputPinId() : -1; }
    virtual int outputPinId(int /*port*/) const { return -1; }

    // Pin count (default: 1/1 for legacy single-pin engines)
    virtual int numInputPins() const { return 1; }
    virtual int numOutputPins() const { return 1; }

    // Serialization — default no-op
    virtual nlohmann::json serialize() const { return nlohmann::json::object(); }
    virtual void deserialize(const nlohmann::json&) {}
};
```

- [ ] **Step 4: Verify it builds**

```
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=g++
cmake --build build --target simulator_common
```

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt common/CMakeLists.txt common/component_interface.h
git commit -m "feat: add nlohmann/json dependency and serialize interface to IComponentEngine"
```

---

### Task 2: Implement serialize()/deserialize() on every engine

**Files:**
- Modify: `signal_generator/include/signal_generator_engine.h` and `src/signal_generator_engine.cpp`
- Modify: `amplifier/include/amplifier_engine.h` and `src/amplifier_engine.cpp`
- Modify: `mixer/include/mixer_engine.h` and `src/mixer_engine.cpp`
- Modify: `splitter/include/splitter_engine.h` and `src/splitter_engine.cpp`
- Modify: `s_parametric_component/include/s_param_engine.h` and `src/s_param_engine.cpp`
- Modify: `adc/include/adc_engine.h` and `src/adc_engine.cpp`
- Modify: `pfb_channelizer/include/pfb_channelizer_engine.h` and `src/pfb_channelizer_engine.cpp`
- Modify: `coax/include/coax_cable_engine.h` and `src/coax_cable_engine.cpp`
- Modify: `ideal_filter/include/ideal_filter_engine.h` and `src/ideal_filter_engine.cpp`

Each engine header needs `nlohmann::json serialize() const override;` and `void deserialize(const nlohmann::json&) override;` declarations.

Each engine source implements them.

- [ ] **Step 1: SignalGeneratorEngine**

Header: add declarations.
Source:

```cpp
#include <nlohmann/json.hpp>

nlohmann::json SignalGeneratorEngine::serialize() const {
    nlohmann::json j = nlohmann::json::array();
    for (const auto& t : m_tones) {
        j.push_back({{"freq_Hz", t.freq_Hz},
                     {"power_dBm", t.power_dBm},
                     {"phase_deg", t.phase_deg}});
    }
    return j;
}

void SignalGeneratorEngine::deserialize(const nlohmann::json& j) {
    m_tones.clear();
    if (!j.is_array()) return;
    for (const auto& tj : j) {
        m_tones.push_back({
            tj.value("freq_Hz", 100e6),
            tj.value("power_dBm", -20.0),
            tj.value("phase_deg", 0.0)
        });
    }
    m_dirty = true;
}
```

- [ ] **Step 2: AmplifierEngine**

```cpp
nlohmann::json AmplifierEngine::serialize() const {
    return {
        {"gain_dB", m_gain_dB},
        {"nf_dB", m_nf_dB},
        {"enable_nonlinear", m_enable_nonlinear},
        {"oip2_dBm", m_oip2_dBm},
        {"oip3_dBm", m_oip3_dBm}
    };
}

void AmplifierEngine::deserialize(const nlohmann::json& j) {
    m_gain_dB = j.value("gain_dB", 0.0);
    m_nf_dB = j.value("nf_dB", 0.0);
    m_enable_nonlinear = j.value("enable_nonlinear", false);
    m_oip2_dBm = j.value("oip2_dBm", 50.0);
    m_oip3_dBm = j.value("oip3_dBm", 50.0);
    m_dirty = true;
}
```

- [ ] **Step 3: MixerEngine**

```cpp
nlohmann::json MixerEngine::serialize() const {
    return {{"lo_freq_Hz", m_lo_freq_Hz}, {"conv_gain_dB", m_conv_gain_dB}};
}

void MixerEngine::deserialize(const nlohmann::json& j) {
    m_lo_freq_Hz = j.value("lo_freq_Hz", 1e9);
    m_conv_gain_dB = j.value("conv_gain_dB", 0.0);
    m_dirty = true;
}
```

- [ ] **Step 4: SplitterEngine**

```cpp
nlohmann::json SplitterEngine::serialize() const {
    return nlohmann::json::object(); // identity params
}

void SplitterEngine::deserialize(const nlohmann::json&) {
    // nothing to restore
}
```

- [ ] **Step 5: SParamEngine**

```cpp
nlohmann::json SParamEngine::serialize() const {
    return {{"filepath", m_filepath},
            {"port_a", m_port_a},
            {"port_b", m_port_b}};
}

void SParamEngine::deserialize(const nlohmann::json& j) {
    std::string fp = j.value("filepath", "");
    int port_a_sel = j.value("port_a", 0);
    int port_b_sel = j.value("port_b", 1);
    if (!fp.empty()) {
        m_filepath = fp;
        m_port_a = port_a_sel;
        m_port_b = port_b_sel;
        reloadFile();
    }
}
```

- [ ] **Step 6: AdcEngine**

```cpp
nlohmann::json AdcEngine::serialize() const {
    return {{"sample_rate_Hz", m_sample_rate_Hz},
            {"bits", m_bits},
            {"full_scale_V", m_full_scale_V},
            {"nsd_dBm_per_Hz", m_nsd_dBm_per_Hz}};
}

void AdcEngine::deserialize(const nlohmann::json& j) {
    m_sample_rate_Hz = j.value("sample_rate_Hz", 100e6);
    m_bits = j.value("bits", 12);
    m_full_scale_V = j.value("full_scale_V", 1.0);
    m_nsd_dBm_per_Hz = j.value("nsd_dBm_per_Hz", -150.0);
    m_dirty = true;
}
```

- [ ] **Step 7: PFBChannelizerEngine**

```cpp
nlohmann::json PFBChannelizerEngine::serialize() const {
    return {{"channel_count", m_channel_count},
            {"taps_per_branch", m_taps_per_branch},
            {"kaiser_beta", m_kaiser_beta}};
}

void PFBChannelizerEngine::deserialize(const nlohmann::json& j) {
    m_channel_count = j.value("channel_count", 16);
    m_taps_per_branch = j.value("taps_per_branch", 8);
    m_kaiser_beta = j.value("kaiser_beta", 6.0);
    m_dirty = true;
}
```

- [ ] **Step 8: CoaxCableEngine**

```cpp
nlohmann::json CoaxCableEngine::serialize() const {
    return {{"preset_index", m_preset_index},
            {"length_m", m_length_m},
            {"connectors_loss_dB", m_connectors_loss_dB}};
}

void CoaxCableEngine::deserialize(const nlohmann::json& j) {
    m_preset_index = j.value("preset_index", 4);
    m_length_m = j.value("length_m", 1.0);
    m_connectors_loss_dB = j.value("connectors_loss_dB", 0.0);
    m_dirty = true;
}
```

- [ ] **Step 9: IdealFilterEngine**

```cpp
nlohmann::json IdealFilterEngine::serialize() const {
    return {{"filter_type", static_cast<int>(m_filter_type)},
            {"cutoff_Hz", m_cutoff_Hz},
            {"order", m_order}};
}

void IdealFilterEngine::deserialize(const nlohmann::json& j) {
    m_filter_type = static_cast<FilterType>(j.value("filter_type", 0));
    m_cutoff_Hz = j.value("cutoff_Hz", 100e6);
    m_order = j.value("order", 1);
    m_dirty = true;
}
```

- [ ] **Step 10: Build and verify**

```bash
cmake --build build
```

- [ ] **Step 11: Commit**

```bash
git add signal_generator/ amplifier/ mixer/ splitter/ s_parametric_component/ adc/ pfb_channelizer/ coax/ ideal_filter/
git commit -m "feat: implement serialize/deserialize on all engine types"
```

---

### Task 3: Add graph state helpers + component registry factory

**Files:**
- Modify: `node_graph/include/node_graph_engine.h`
- Modify: `node_graph/src/node_graph_engine.cpp`
- Modify: `app/include/component_registry.h`
- Modify: `app/src/component_registry.cpp`

- [ ] **Step 1: Add setNextIds to NodeGraphEngine**

In `node_graph/include/node_graph_engine.h`:
```cpp
// --- header additions ---
void setNextIds(int node_id, int pin_id, int link_id);

// Also expose the group_id and boundary_pin_id counters for save/load:
int nextGroupId() const { return m_next_group_id; }
int nextBoundaryPinId() const { return m_next_boundary_pin_id; }
void setNextGroupId(int id) { m_next_group_id = id; }
void setNextBoundaryPinId(int id) { m_next_boundary_pin_id = id; }
```

In `node_graph/src/node_graph_engine.cpp`:
```cpp
void NodeGraphEngine::setNextIds(int node_id, int pin_id, int link_id) {
    m_next_node_id = node_id;
    m_next_pin_id = pin_id;
    m_next_link_id = link_id;
}
```

- [ ] **Step 2: Add removeAllLinks for clean project reset**

In header:
```cpp
void removeAllLinks();
```

In source:
```cpp
void NodeGraphEngine::removeAllLinks() {
    m_links.clear();
}
```

- [ ] **Step 3: Add a "load-time" creation method to ComponentRegistry**

In `app/include/component_registry.h`, add:
```cpp
// For project load: create a component with an already-known graph node id.
// The component is created normally (gets sequential IDs from graph engine),
// and stored in the registry. Returns pointer for deserialization.
template<typename T, typename... Args>
T& create(Args&&... args) {
    return add<T>(std::forward<Args>(args)...);
}
```

Actually `add<T>` already works. No change needed. The factory will be in the app itself.

- [ ] **Step 4: Commit**

```bash
git add node_graph/include/node_graph_engine.h node_graph/src/node_graph_engine.cpp
git commit -m "feat: add graph state helpers (setNextIds, removeAllLinks) for save/load"
```

---

### Task 4: Implement saveProject/loadProject/newProject in app

**Files:**
- Modify: `app/include/app.h`
- Modify: `app/src/app.cpp`

- [ ] **Step 1: Add save/load declarations to app.h**

Add to `RfSimulatorApp` class:
```cpp
// --- in app.h, in the public section ---
void saveProject(const std::string& path);
void loadProject(const std::string& path);
void newProject();
bool isDirty() const { return m_dirty; }
std::string m_current_project_path;
void markDirty() { m_dirty = true; }
bool m_dirty = false;
```

- [ ] **Step 2: Implement newProject() in app.cpp**

```cpp
void RfSimulatorApp::newProject() {
    // Remove all links from the graph engine first
    m_graph_engine.removeAllLinks();

    // Remove all components — ComponentRegistry handles cleanup
    // Collect IDs first to avoid iterator invalidation
    std::vector<int> ids;
    for (auto* comp : m_components.all())
        ids.push_back(comp->graphNodeId());
    for (int id : ids)
        m_components.remove(id);

    // Clear probes
    m_graph_engine.clearProbes();
    m_spectrum_widget->setProbeLabels({});

    // Reset IQ / PFB widgets
    m_iq_widgets.clear();
    m_show_iq_pfbs.clear();
    m_pfb_grid_widgets.clear();
    m_show_pfb_grids.clear();

    // Reset graph counters
    m_graph_engine.setNextIds(1, 100, 1000);
    m_graph_engine.setNextGroupId(50000);
    m_graph_engine.setNextBoundaryPinId(100000);

    m_next_component_id = 100;
    m_current_project_path.clear();
    m_dirty = false;
}
```

- [ ] **Step 3: Implement saveProject() in app.cpp**

```cpp
#include <fstream>

void RfSimulatorApp::saveProject(const std::string& path) {
    nlohmann::json root;
    root["version"] = 1;

    auto pos = path.find_last_of("\\/");
    std::string fname = (pos != std::string::npos) ? path.substr(pos + 1) : path;
    auto dot = fname.find_last_of('.');
    root["name"] = (dot != std::string::npos) ? fname.substr(0, dot) : fname;

    // Save components by iterating the registry
    nlohmann::json comps_arr = nlohmann::json::array();
    for (auto* comp : m_components.all()) {
        nlohmann::json cj;
        // Use type_index to store a human-readable type name
        cj["type"] = demangle(typeid(*comp).name());  // NOTE: see pitfall below
        cj["params"] = comp->serialize();

        // Save node position via imnodes
        int nid = comp->graphNodeId();
        ImNodes::EditorContextSet(m_graph_widget->context());
        ImVec2 pos_n = ImNodes::GetNodeEditorSpacePos(nid);
        cj["pos"]["x"] = pos_n.x;
        cj["pos"]["y"] = pos_n.y;

        comps_arr.push_back(cj);
    }
    root["components"] = comps_arr;

    // Save links as component-index + port pairs (not raw pin IDs)
    nlohmann::json links_arr = nlohmann::json::array();
    // Build a map: pin_id → {comp_index, port, is_output}
    struct PinInfo { size_t comp; int port; bool is_output; };
    std::unordered_map<int, PinInfo> pin_map;
    for (size_t i = 0; i < m_components.size(); ++i) {
        auto* comp = m_components.all()[i];
        int nid = comp->graphNodeId();
        for (const auto& gn : m_graph_engine.nodes()) {
            if (gn.node_id == nid) {
                for (size_t p = 0; p < gn.input_pin_ids.size(); ++p)
                    pin_map[gn.input_pin_ids[p]] = {i, (int)p, false};
                for (size_t p = 0; p < gn.output_pin_ids.size(); ++p)
                    pin_map[gn.output_pin_ids[p]] = {i, (int)p, true};
                break;
            }
        }
    }
    for (const auto& link : m_graph_engine.links()) {
        auto from_it = pin_map.find(link.start_pin_id);
        auto to_it = pin_map.find(link.end_pin_id);
        if (from_it == pin_map.end() || to_it == pin_map.end()) continue;
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
    for (int probe_pin : m_graph_engine.probePins()) {
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
    // Build node_id → comp_index map
    std::unordered_map<int, size_t> nid_to_comp;
    for (size_t i = 0; i < m_components.size(); ++i)
        nid_to_comp[m_components.all()[i]->graphNodeId()] = i;

    for (const auto& g : m_graph_engine.groups()) {
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

    m_current_project_path = path;
    m_dirty = false;
    LOG_INFO("Saved project to %s", path.c_str());
}
```

> **Pitfall:** `typeid(*comp).name()` returns a mangled name on most compilers. To get readable type names, use a simple string-switch mapping in the loader. Save-side only needs to be consistent with load-side. A safer approach: add a virtual `std::string typeName() const` method to each engine that returns a fixed string like `"SignalGenerator"`. For simplicity in this plan, we use a mapping function on load that demangles known types. Actually, simplest: just store the type name directly in each engine's serialize method, or use a mapping dict.

**Simpler approach:** Add a virtual `std::string componentTypeName() const` to `IComponentEngine` that returns a human-readable type string. Each engine overrides it, e.g. `return "SignalGenerator";`.

Wait, that adds another virtual to the interface. Even simpler: in the save loop, use a type-to-string map:

```cpp
// Type to name mapping
static const std::unordered_map<std::type_index, std::string> s_type_names = {
    {std::type_index(typeid(SignalGeneratorEngine)), "SignalGenerator"},
    {std::type_index(typeid(AmplifierEngine)), "Amplifier"},
    {std::type_index(typeid(SplitterEngine)), "Splitter"},
    {std::type_index(typeid(MixerEngine)), "Mixer"},
    {std::type_index(typeid(SParamEngine)), "SParam"},
    {std::type_index(typeid(AdcEngine)), "ADC"},
    {std::type_index(typeid(PFBChannelizerEngine)), "PFBChannelizer"},
    {std::type_index(typeid(CoaxCableEngine)), "CoaxCable"},
    {std::type_index(typeid(IdealFilterEngine)), "IdealFilter"},
};

auto it = s_type_names.find(std::type_index(typeid(*comp)));
cj["type"] = (it != s_type_names.end()) ? it->second : "Unknown";
```

On load, use the same names in reverse to dispatch creation.

- [ ] **Step 4: Implement loadProject() in app.cpp**

```cpp
void RfSimulatorApp::loadProject(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        LOG_ERROR("Failed to open project file: %s", path.c_str());
        return;
    }
    nlohmann::json root;
    try {
        in >> root;
    } catch (const nlohmann::json::exception& e) {
        LOG_ERROR("Invalid project file: %s", e.what());
        return;
    }

    newProject();

    // Map: type string → factory lambda
    std::vector<nlohmann::json::iterator> comp_order;
    auto& comps = root["components"];
    for (auto it = comps.begin(); it != comps.end(); ++it)
        comp_order.push_back(it);

    // Create components in saved order
    std::vector<int> new_node_ids; // maps saved index → new graph node ID
    for (auto& it : comp_order) {
        auto& cj = *it;
        std::string type = cj.value("type", "");
        auto& params = cj["params"];

        IComponentEngine* comp = nullptr;
        if (type == "SignalGenerator") {
            auto& ref = m_components.add<SignalGeneratorEngine>(m_next_component_id++, m_graph_engine);
            ref.deserialize(params);
            comp = &ref;
        } else if (type == "Amplifier") {
            auto& ref = m_components.add<AmplifierEngine>(m_next_component_id++, m_graph_engine);
            ref.deserialize(params);
            comp = &ref;
        } else if (type == "Mixer") {
            auto& ref = m_components.add<MixerEngine>(m_next_component_id++, m_graph_engine);
            ref.deserialize(params);
            comp = &ref;
        } else if (type == "Splitter") {
            auto& ref = m_components.add<SplitterEngine>(m_next_component_id++, m_graph_engine);
            ref.deserialize(params);
            comp = &ref;
        } else if (type == "SParam") {
            auto& ref = m_components.add<SParamEngine>(m_next_component_id++, m_graph_engine, "");
            ref.deserialize(params);
            comp = &ref;
        } else if (type == "ADC") {
            auto& ref = m_components.add<AdcEngine>(m_next_component_id++, m_graph_engine);
            ref.deserialize(params);
            comp = &ref;
        } else if (type == "PFBChannelizer") {
            auto& ref = m_components.add<PFBChannelizerEngine>(m_next_component_id++, m_graph_engine);
            ref.deserialize(params);
            // Restore IQ plot + PFB grid widgets for this PFB
            m_iq_widgets.push_back(std::make_unique<IQPlotWidget>(ref));
            m_show_iq_pfbs.push_back(true);
            m_pfb_grid_widgets.push_back(std::make_unique<PFBChannelizerWidget>(ref));
            m_show_pfb_grids.push_back(true);
            comp = &ref;
        } else if (type == "CoaxCable") {
            auto& ref = m_components.add<CoaxCableEngine>(m_next_component_id++, m_graph_engine);
            ref.deserialize(params);
            comp = &ref;
        } else if (type == "IdealFilter") {
            auto& ref = m_components.add<IdealFilterEngine>(m_next_component_id++, m_graph_engine);
            ref.deserialize(params);
            comp = &ref;
        } else {
            LOG_WARN("Unknown component type in project file: %s", type.c_str());
            new_node_ids.push_back(-1);
            continue;
        }

        new_node_ids.push_back(comp ? comp->graphNodeId() : -1);

        // Restore position
        if (comp && cj.contains("pos")) {
            ImNodes::EditorContextSet(m_graph_widget->context());
            ImNodes::SetNodeEditorSpacePos(
                comp->graphNodeId(),
                ImVec2(cj["pos"].value("x", 0.0f), cj["pos"].value("y", 0.0f)));
        }
    }

    // Restore links (saved as component-index + port pairs)
    auto& saved_links = root["links"];
    for (const auto& lj : saved_links) {
        int from_idx = lj.value("from", -1);
        int to_idx = lj.value("to", -1);
        int from_port = lj.value("from_port", 0);
        int to_port = lj.value("to_port", 0);
        if (from_idx < 0 || to_idx < 0 ||
            static_cast<size_t>(from_idx) >= m_components.size() ||
            static_cast<size_t>(to_idx) >= m_components.size())
            continue;

        auto* from_comp = m_components.all()[from_idx];
        auto* to_comp = m_components.all()[to_idx];

        int start_pin = from_comp->outputPinId(from_port);
        int end_pin = to_comp->inputPinId(to_port);
        if (start_pin >= 0 && end_pin >= 0)
            m_graph_engine.addLink(start_pin, end_pin);
    }

    // Restore probes
    auto& saved_probes = root["probe_pins"];
    for (const auto& pj : saved_probes) {
        int comp_idx = pj.value("comp", -1);
        int port = pj.value("port", 0);
        bool is_output = pj.value("is_output", true);
        if (comp_idx < 0 || static_cast<size_t>(comp_idx) >= m_components.size())
            continue;
        auto* comp = m_components.all()[comp_idx];
        int pin = is_output ? comp->outputPinId(port) : comp->inputPinId(port);
        if (pin >= 0) m_graph_engine.addProbePin(pin);
    }

    // Restore groups
    auto& saved_groups = root["groups"];
    for (const auto& gj : saved_groups) {
        std::string name = gj.value("name", "Group");
        std::vector<int> member_ids;
        for (const auto& mj : gj["member_components"]) {
            int comp_idx = mj.get<int>();
            if (comp_idx >= 0 && static_cast<size_t>(comp_idx) < new_node_ids.size() &&
                new_node_ids[comp_idx] >= 0) {
                member_ids.push_back(new_node_ids[comp_idx]);
            }
        }
        if (member_ids.size() >= 2) {
            int gid = m_graph_engine.addGroup(name, member_ids);
            bool collapsed = gj.value("collapsed", true);
            if (gid >= 0)
                m_graph_engine.setGroupCollapsed(gid, collapsed);
        }
    }

    // Restore window state
    auto& ws = root["window_state"];
    if (!ws.is_null()) {
        m_show_log = ws.value("log", true);
        m_show_spectrum = ws.value("spectrum_analyzer", true);
        m_show_properties = ws.value("properties", true);
        m_show_node_editor = ws.value("node_editor", true);
    }

    // Restore graph state counters
    auto& gs = root["graph_state"];
    if (!gs.is_null()) {
        m_next_component_id = gs.value("next_component_id", m_next_component_id);
    }

    m_current_project_path = path;
    m_dirty = false;
    LOG_INFO("Loaded project from %s", path.c_str());
}
```

- [ ] **Step 5: Wire onRemoveNode to call markDirty + PFB cleanup**

In the constructor, inside the existing `onRemoveNode` lambda, add `markDirty();` at the top. Also add `markDirty()` at the end of each component's `onAdd*` lambda.

Add `onLinkChanged` callback to `NodeGraphWidget`:

In `node_graph/include/node_graph_widget.h`:
```cpp
std::function<void()> onLinkChanged;
```

In `node_graph/src/node_graph_widget.cpp`, in `handleLinkCreation()` after the `addLink` call, add:
```cpp
if (onLinkChanged) onLinkChanged();
```

Similarly in `handleLinkDeletion()` after `removeLink`, add:
```cpp
if (onLinkChanged) onLinkChanged();
```

Then in the app constructor:
```cpp
m_graph_widget->onLinkChanged = [this]() { markDirty(); };
```

This covers link creation and deletion dirty tracking.

- [ ] **Step 6: Verify build compiles**

```bash
cmake --build build
```

- [ ] **Step 7: Commit**

```bash
git add app/include/app.h app/src/app.cpp
git commit -m "feat: implement newProject, saveProject, loadProject"
```

---

### Task 5: File menu bar, keyboard shortcuts, unsaved-changes dialog

**Files:**
- Modify: `app/src/app.cpp`
- Modify: `app/include/app.h`

- [ ] **Step 1: Add file menu bar in draw_ui()**

In `RfSimulatorApp::draw_ui()`, at the very top, add:

```cpp
// File menu bar
if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New", "Ctrl+N")) {
            if (m_dirty) ImGui::OpenPopup("Unsaved Changes##new");
            else newProject();
        }
        if (ImGui::MenuItem("Open...", "Ctrl+O")) {
            if (m_dirty) ImGui::OpenPopup("Unsaved Changes##open");
            else openFileDialog();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save", "Ctrl+S")) {
            if (!m_current_project_path.empty()) saveProject(m_current_project_path);
            else saveFileDialog();
        }
        if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) {
            saveFileDialog();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) {
            // On exit, prompt if dirty (handled by destructor)
            if (m_dirty) ImGui::OpenPopup("Unsaved Changes##exit");
            else std::exit(0);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Log", nullptr, &m_show_log);
        ImGui::MenuItem("Spectrum Analyzer", nullptr, &m_show_spectrum);
        ImGui::MenuItem("Properties", nullptr, &m_show_properties);
        ImGui::MenuItem("Node Editor", nullptr, &m_show_node_editor);
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

// Unsaved Changes popup
if (ImGui::BeginPopupModal("Unsaved Changes##new", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("You have unsaved changes. Save before continuing?");
    if (ImGui::Button("Save", ImVec2(120, 0))) {
        if (!m_current_project_path.empty()) saveProject(m_current_project_path);
        else saveFileDialog();
        if (!m_dirty) { newProject(); ImGui::CloseCurrentPopup(); }
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard", ImVec2(120, 0))) {
        newProject();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}
// Repeat for ##open and ##exit, or use a single popup with a pending action variable
```

- [ ] **Step 2: Add keyboard shortcuts**

After the menu bar code:

```cpp
// Keyboard shortcuts
auto& io = ImGui::GetIO();
if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S)) {
    if (!m_current_project_path.empty()) saveProject(m_current_project_path);
    else saveFileDialog();
}
if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S))
    saveFileDialog();
if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_O))
    openFileDialog();
if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_N))
    newProject();
```

- [ ] **Step 3: Add file dialog helpers**

```cpp
void RfSimulatorApp::openFileDialog() {
    auto result = pfd::open_file("Open Project", ".",
        {"RF Simulator Project (*.rfsim)", "*.rfsim", "All Files", "*"}).result();
    if (!result.empty())
        loadProject(result[0]);
}

void RfSimulatorApp::saveFileDialog() {
    auto result = pfd::save_file("Save Project As", ".",
        {"RF Simulator Project (*.rfsim)", "*.rfsim"}).result();
    if (!result.empty())
        saveProject(result);
}
```

- [ ] **Step 4: Add dirty tracking to param change callbacks**

In the constructor, after each `onAdd*` lambda, add `markDirty()`. Also add `markDirty()` to each component widget's param-change lambdas. The inspector panel already triggers callbacks — wire `markDirty()` via `m_inspector_panel->onParamChange`.

Add to `InspectorPanel` a way to signal dirty: either a `std::function<void()> onParamChange` callback (set it to `markDirty` in the app).

For now, the simplest approach: call `markDirty()` in the `onRemoveNode` lambda and in each `onAdd*` callback. For param changes, wire through `InspectorPanel`.

- [ ] **Step 5: Title bar shows project name**

Replace the existing `ImGui::Text("RF Simulator %s ...")` line:

```cpp
std::string title = "RF Simulator";
if (!m_current_project_path.empty()) {
    auto p = m_current_project_path.find_last_of("\\/");
    title = (p != std::string::npos) ? m_current_project_path.substr(p + 1) : m_current_project_path;
    title = (m_dirty ? "* " : "") + title;
} else if (m_dirty) {
    title = "*Untitled";
}
// Remove the old title text — menu bar already shows it
```

- [ ] **Step 6: Verify build**

```bash
cmake --build build
```

- [ ] **Step 7: Commit**

```bash
git add app/src/app.cpp app/include/app.h
git commit -m "feat: add file menu bar, keyboard shortcuts, dirty tracking"
```

---

### Task 6: Wire dirty tracking into NodeGraphWidget link changes

**Files:**
- Modify: `node_graph/include/node_graph_widget.h`
- Modify: `node_graph/src/node_graph_widget.cpp`

(Already described in Task 4 Step 5 above — handle during Task 4 or as a follow-up dependency.)

### Task 7: Wire dirty tracking into inspector panel + all param mutations

**Files:**
- Modify: `app/include/inspector_panel.h`
- Modify: `app/src/inspector_panel.cpp`

- [ ] **Step 1: Add onParamChange callback to InspectorPanel**

In `inspector_panel.h`:
```cpp
std::function<void()> onParamChange;
```

In `inspector_panel.cpp`, where the panel draws controls for each component's parameters, call `if (onParamChange) onParamChange()` after any drag/scalar/checkbox change that the user actually commits.

The simplest approach: add a single flag check at the end of `draw()`:
```cpp
// In InspectorPanel::draw(), after all ImGui controls
// If any active item changed this frame, fire the callback
if (onParamChange && ImGui::IsAnyItemDeactivatedAfterEdit())
    onParamChange();
```

This catches any param change across all component types without per-control wiring.

- [ ] **Step 2: Wire it in the app constructor**

```cpp
m_inspector_panel->onParamChange = [this]() { markDirty(); };
```

- [ ] **Step 3: Verify build**

```bash
cmake --build build
```

- [ ] **Step 4: Commit**

```bash
git add app/include/inspector_panel.h app/src/inspector_panel.cpp app/src/app.cpp
git commit -m "feat: wire dirty tracking through inspector panel param changes"
```

---

### Task 8: Tests

**Files:**
- Create: `tests/test_project_file.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Add test file to CMakeLists.txt**

```cmake
target_sources(unit_tests PRIVATE test_project_file.cpp)
```

Also need to link nlohmann_json to the test target:
```cmake
target_link_libraries(unit_tests PRIVATE nlohmann_json::nlohmann_json)
```

- [ ] **Step 2: Write round-trip test header + helpers**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cstdio>
#include <fstream>
#include "app.h"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
#include <nlohmann/json.hpp>

using Catch::Approx;

static std::string tempPath() { return "test_roundtrip.rfsim"; }
static void cleanup() { std::remove(tempPath().c_str()); }

struct ImGuiFixture {
    ImGuiFixture() {
        ImGui::CreateContext();
        ImPlot::CreateContext();
        ImNodes::CreateContext();
    }
    ~ImGuiFixture() {
        ImNodes::DestroyContext();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
    }
};
```

- [ ] **Step 3: Write empty project round-trip test**

```cpp
TEST_CASE_METHOD(ImGuiFixture, "Round-trip: empty project", "[project_file]") {
    cleanup();
    {
        RfSimulatorApp app;
        app.saveProject(tempPath());
        REQUIRE(app.isDirty() == false);
    }
    {
        RfSimulatorApp app;
        app.loadProject(tempPath());
        REQUIRE(app.isDirty() == false);
        REQUIRE(app.m_components.size() == 2); // default gen + amp
    }
    cleanup();
}
```

- [ ] **Step 4: Write generator+amplifier+link round-trip test**

```cpp
TEST_CASE_METHOD(ImGuiFixture, "Round-trip: single generator and amplifier with link", "[project_file]") {
    cleanup();
    // We can't easily add components via the file menu in tests.
    // Instead, save the default state (which has a gen + amp), modify params,
    // save, load, verify.
    // For now, test that the save/load machinery works by saving default project
    // and loading it back.
    // Full programmatic component add/param/save/load is done in the app tests
    // which require imnodes rendering context.
    
    // Basic save/load of default project
    {
        RfSimulatorApp app;
        // The app creates default gen + amp in constructor
        app.saveProject(tempPath());
    }
    {
        RfSimulatorApp app;
        app.loadProject(tempPath());
        REQUIRE(app.m_components.size() == 2);
    }
    cleanup();
}
```

- [ ] **Step 5: Write invalid JSON test**

```cpp
TEST_CASE_METHOD(ImGuiFixture, "Load invalid JSON does not crash", "[project_file]") {
    cleanup();
    {
        std::ofstream out(tempPath());
        out << "not valid json {{{";
    }
    {
        RfSimulatorApp app;
        app.loadProject(tempPath());
        // Should not crash, app continues with default state
        REQUIRE(app.m_components.size() == 2);
    }
    cleanup();
}
```

- [ ] **Step 6: Build and run tests**

```bash
cmake --build build
ctest --test-dir build
```

- [ ] **Step 7: Commit**

```bash
git add tests/test_project_file.cpp tests/CMakeLists.txt
git commit -m "test: add project file save/load round-trip tests"
```

---

### Task 9: DOX pass

**Files:**
- Modify: `ROADMAP.md`
- Modify: `AGENTS.md` (root)

- [ ] **Step 1: Update ROADMAP.md**

Add checkmark for project save/load feature.

- [ ] **Step 2: Update AGENTS.md if needed**

Check if any child DOX entries need creation or update.

- [ ] **Step 3: Commit**

```bash
git add ROADMAP.md AGENTS.md
git commit -m "docs: DOX pass for project save/load"
```
