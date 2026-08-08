# Component Registration Unification + App Decomposition — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make adding a new RF component touch one registry row + one engine module instead of ~10 files, by unifying all six parallel type-dispatch tables into the extended `ComponentTypeRegistry`, then extracting `PFBViewManager` and `ProjectSerializer` from the `RfSimulatorApp` god-object.

**Architecture:** Extend the existing `app/` `ComponentTypeRegistry` into the single table (canonical type, `.rfsim` project name, display name, menu label, label prefix, `NodeKind`, `create()` factory, inspector draw callback). Engines self-identify via a new pure-virtual `type_name()` on `IComponentEngine`. All dispatch (canvas menu, add, duplicate, save/load, inspector) consumes the table. Then two extractions: `PFBViewManager` (owns the four lockstep PFB widget vectors) and `ProjectSerializer` (owns save/load/new JSON logic).

**Tech Stack:** C++20, CMake ≥ 3.20, Ninja, MinGW-w64 g++ (Windows) / GCC / Clang, nlohmann/json, ImGui/ImNodes/ImPlot, Catch2 v3.4.0.

## Global Constraints

- `.rfsim` project files keep today's type strings verbatim: `SignalGenerator`, `Amplifier`, `Splitter`, `Mixer`, `Attenuator`, `Combiner`, `Equalizer`, `ADC`, `PFBChannelizer`, `CoaxCable`, `IdealFilter`. `saveProject` writes them; `loadProject` accepts legacy AND canonical (`amplifier`, ...) names.
- Library JSON (`component_data/library/**`, `rf-sim-libraries/**`) keeps lowercase type strings: `amplifier`, `attenuator`, `splitter`, `filter`, `mixer`, `equalizer`, `combiner`, `adc`.
- Canvas menu labels stay byte-identical: `Add Generator`, `Add Amplifier`, `Add Splitter`, `Add Combiner`, `Add Coax Cable`, `Add Equalizer`, `Add Mixer`, `Add RF ADC`, `Add PFB Channelizer`, `Add Ideal Filter`, `Add Attenuator`. UI tests in `test_engine/ui_tests.cpp` click these exact strings.
- Library-form parameter keys must keep working after `instantiate()` moves to `create()`+`deserialize()` — the key-parity fixes in Task 2b are mandatory, not optional.
- Only widget files may `#include <imgui.h>` / `<implot.h>` / `<imnodes.h>` (per CONTRIBUTING). `app/src/app.cpp` and `inspector_panel.cpp` are widget-layer files and already include them.
- `uint64_t` requires explicit `#include <cstdint>`.
- MinGW test-registration ceiling (~217 TEST_CASEs in the `tests` binary): any NEW test cases go into a NEW standalone executable (`tests/test_component_dispatch.cpp`). Do not add TEST_CASEs to `test_main.cpp` or any file already in the `tests` target. REMOVING cases from `test_node_graph_engine.cpp` is fine.
- Format: run `scripts/format.sh` after each task; CI enforces clang-format 18.
- Commit per task with an imperative subject; verify `cmake --build build && ctest --test-dir build` before committing.

---

## Phase 1 — Unified registry + `type_name()`

### Task 1: `type_name()` pure virtual on IComponentEngine

**Files:**
- Modify: `common/component_interface.h`
- Modify: 11 engine headers (`signal_generator`, `amplifier`, `splitter`, `mixer`, `adc`, `pfb_channelizer`, `coax`, `equalizer`, `ideal_filter`, `attenuator`, `combiner` — each `include/*_engine.h`)
- Modify: `tests/test_component_registry.cpp` (test engines `TestEngineA`/`TestEngineB`)

**Interfaces:**
- Produces: `virtual std::string_view type_name() const = 0;` on `IComponentEngine`; every engine returns its canonical lowercase key.

- [ ] **Step 1: Add the pure virtual to the interface**

`common/component_interface.h` — add `#include <string_view>` and the method (after the `inputPinId(int)` block, before `serialize()`):

```cpp
#pragma once

#include "signal_node.h"
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

class IComponentEngine {
  public:
    virtual ~IComponentEngine() = default;
    virtual int id() const = 0;
    virtual int graphNodeId() const = 0;
    virtual int outputPinId() const = 0;
    virtual std::string hoverSummary() const = 0;
    virtual SignalNode &node() = 0;
    virtual const SignalNode &node() const = 0;
    virtual void update(double dt) = 0;

    virtual int inputPinId() const { return -1; }

    // Multi-pin accessors (default: forward to inputPinId() for port 0)
    virtual int inputPinId(int port) const { return port == 0 ? inputPinId() : -1; }
    virtual int outputPinId(int port) const { return port == 0 ? outputPinId() : -1; }

    // Pin count (default: 1/1 for legacy single-pin engines)
    virtual int numInputPins() const { return 1; }
    virtual int numOutputPins() const { return 1; }

    // Canonical type key (e.g. "amplifier"). Single source of truth for the
    // component-type dispatch tables (save/load, duplicate, inspector, menu).
    virtual std::string_view type_name() const = 0;

    // Serialization — default no-op
    virtual nlohmann::json serialize() const { return nlohmann::json::object(); }
    virtual void deserialize(const nlohmann::json &) {}
};
```

- [ ] **Step 2: Implement in all 11 engines** — add this inline override to each engine header (put it next to the other `int id() const override`-style one-liners):

| Header | exact line |
|---|---|
| `signal_generator/include/signal_generator_engine.h` | `std::string_view type_name() const override { return "generator"; }` |
| `amplifier/include/amplifier_engine.h` | `std::string_view type_name() const override { return "amplifier"; }` |
| `splitter/include/splitter_engine.h` | `std::string_view type_name() const override { return "splitter"; }` |
| `mixer/include/mixer_engine.h` | `std::string_view type_name() const override { return "mixer"; }` |
| `adc/include/adc_engine.h` | `std::string_view type_name() const override { return "adc"; }` |
| `pfb_channelizer/include/pfb_channelizer_engine.h` | `std::string_view type_name() const override { return "pfb"; }` |
| `coax/include/coax_cable_engine.h` | `std::string_view type_name() const override { return "coax"; }` |
| `equalizer/include/equalizer_engine.h` | `std::string_view type_name() const override { return "equalizer"; }` |
| `ideal_filter/include/ideal_filter_engine.h` | `std::string_view type_name() const override { return "filter"; }` |
| `attenuator/include/attenuator_engine.h` | `std::string_view type_name() const override { return "attenuator"; }` |
| `combiner/include/combiner_engine.h` | `std::string_view type_name() const override { return "combiner"; }` |

Each header already includes `component_interface.h` (which now provides `<string_view>`), so no extra include is needed.

- [ ] **Step 3: Implement in the two test engines**

`tests/test_component_registry.cpp` — in `TestEngineA` add `std::string_view type_name() const override { return "test_a"; }`; in `TestEngineB` add `std::string_view type_name() const override { return "test_b"; }`.

- [ ] **Step 4: Build**

Run: `cmake --build build`
Expected: compiles clean (the pure virtual forces every engine to implement it — the build is the test).

- [ ] **Step 5: Run existing tests**

Run: `build/bin/tests.exe` (Windows) or `build/bin/tests` (Linux/macOS); also `build/bin/test_issue37_pfb_input_removal.exe` (or without `.exe`).
Expected: all pass (no behavior change).

- [ ] **Step 6: Commit**

```bash
git add common/component_interface.h \
    signal_generator/include/signal_generator_engine.h \
    amplifier/include/amplifier_engine.h \
    splitter/include/splitter_engine.h \
    mixer/include/mixer_engine.h \
    adc/include/adc_engine.h \
    pfb_channelizer/include/pfb_channelizer_engine.h \
    coax/include/coax_cable_engine.h \
    equalizer/include/equalizer_engine.h \
    ideal_filter/include/ideal_filter_engine.h \
    attenuator/include/attenuator_engine.h \
    combiner/include/combiner_engine.h \
    tests/test_component_registry.cpp
git commit -m "refactor: add type_name() virtual to IComponentEngine"
```

---

### Task 2a: Extend descriptor struct + populate 11 registry rows (additive)

**Files:**
- Modify: `app/include/component_type_registry.h`
- Modify: `app/src/component_type_registry.cpp`
- Modify: `tests/test_component_authoring.cpp`

**Interfaces:**
- Consumes: `NodeKind` from `node_graph/include/node_graph_engine.h` (app already depends on node_graph).
- Produces: `ComponentTypeDescriptor` with `type`, `project_type`, `display_name`, `menu_label`, `label_prefix`, `kind`, `authorable`, `supports_sparam_file`, `fields`, `create`, `draw_inspector` — AND the old `factory` field kept (additive, so this task compiles and commits alone; removed in Task 2b). `ComponentTypeRegistry` with `find(std::string_view)`, `findByProjectType(std::string_view)`, `all()` returning non-const pointers.

- [ ] **Step 1: Rewrite the header**

`app/include/component_type_registry.h` — full file:

```cpp
#pragma once

#include "node_graph_engine.h"
#include <functional>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

class ComponentRegistry;
class NodeGraphEngine;
class IComponentEngine;
class InspectorPanel;

enum class FieldKind { Number, String, Enum, FilePath, Bool };

struct ParameterField {
    std::string key;   // JSON key under "parameters", e.g. "gain_dB"
    std::string label; // UI label, e.g. "Gain"
    std::string unit;  // e.g. "dB", "Hz"; empty if none
    FieldKind kind = FieldKind::Number;
    bool required = false;
    double min = -std::numeric_limits<double>::infinity(); // Number only
    double max = std::numeric_limits<double>::infinity();  // Number only
    std::vector<std::string> enum_values;                  // Enum only
    nlohmann::json default_value;                          // optional
    std::string help;                                      // optional tooltip
};

struct ComponentTypeDescriptor {
    std::string type;         // canonical key, e.g. "amplifier"
    std::string project_type; // .rfsim save/load name, e.g. "Amplifier"
    std::string display_name; // e.g. "Amplifier"
    std::string menu_label;   // canvas menu item, e.g. "Add Amplifier"
    std::string label_prefix; // graph label prefix, e.g. "Amplifier"
    NodeKind kind = NodeKind::Unknown;
    bool authorable = false;  // appears in New Component form combo
    bool supports_sparam_file = false;
    std::vector<ParameterField> fields;

    // Create a default engine of this type (no params). Callers apply params
    // via engine->deserialize(). Replaces the old params-taking `factory`.
    std::function<IComponentEngine *(ComponentRegistry &, NodeGraphEngine &, int)> create;
    // Inspector property draw. Receives the panel so PFB's multi-instance
    // selector and dirty-flag state stay reachable.
    std::function<void(InspectorPanel &, IComponentEngine &)> draw_inspector;

    // Legacy params-taking factory; still used by ComponentLibrary until
    // Task 2b migrates instantiate to create()+deserialize().
    std::function<IComponentEngine *(ComponentRegistry &, NodeGraphEngine &, int,
                                     const nlohmann::json &)>
        factory;
};

class ComponentTypeRegistry {
  public:
    static ComponentTypeRegistry &instance();

    const ComponentTypeDescriptor *find(std::string_view type) const;
    const ComponentTypeDescriptor *findByProjectType(std::string_view name) const;
    std::vector<ComponentTypeDescriptor *> all();

  private:
    ComponentTypeRegistry();
    std::vector<ComponentTypeDescriptor> m_descriptors;
};
```

- [ ] **Step 2: Rewrite the registry implementation**

`app/src/component_type_registry.cpp` — full file. Rows are registered in this order so the New-Component form combo keeps today's order (`amplifier, attenuator, splitter, filter, mixer, equalizer, combiner, adc`). The 8 authorable rows carry BOTH `factory` (old, unchanged semantics) and `create`; the 3 new rows (generator/coax/pfb) carry only `create` (they are not library-authorable, so no factory needed):

```cpp
// app/src/component_type_registry.cpp
#include "component_type_registry.h"

#include "adc_engine.h"
#include "amplifier_engine.h"
#include "attenuator_engine.h"
#include "coax_cable_engine.h"
#include "combiner_engine.h"
#include "component_registry.h"
#include "equalizer_engine.h"
#include "ideal_filter_engine.h"
#include "mixer_engine.h"
#include "pfb_channelizer_engine.h"
#include "signal_generator_engine.h"
#include "splitter_engine.h"

ComponentTypeRegistry &ComponentTypeRegistry::instance() {
    static ComponentTypeRegistry reg;
    return reg;
}

const ComponentTypeDescriptor *ComponentTypeRegistry::find(std::string_view type) const {
    for (const auto &d : m_descriptors)
        if (d.type == type)
            return &d;
    return nullptr;
}

const ComponentTypeDescriptor *ComponentTypeRegistry::findByProjectType(
    std::string_view name) const {
    for (const auto &d : m_descriptors)
        if (d.type == name || d.project_type == name)
            return &d;
    return nullptr;
}

std::vector<ComponentTypeDescriptor *> ComponentTypeRegistry::all() {
    std::vector<ComponentTypeDescriptor *> result;
    result.reserve(m_descriptors.size());
    for (auto &d : m_descriptors)
        result.push_back(&d);
    return result;
}

ComponentTypeRegistry::ComponentTypeRegistry() {
    ComponentTypeDescriptor amp;
    amp.type = "amplifier";
    amp.project_type = "Amplifier";
    amp.display_name = "Amplifier";
    amp.menu_label = "Add Amplifier";
    amp.label_prefix = "Amplifier";
    amp.kind = NodeKind::Amplifier;
    amp.authorable = true;
    amp.supports_sparam_file = true;
    amp.fields = {
        {"gain_dB", "Gain", "dB", FieldKind::Number, true, -50.0, 100.0, {}, {}, ""},
        {"nf_dB", "Noise Figure", "dB", FieldKind::Number, false, 0.0, 30.0, {}, {}, ""},
        {"oip2_dBm", "OIP2", "dBm", FieldKind::Number, false, -20.0, 100.0, {}, {}, ""},
        {"oip3_dBm", "OIP3", "dBm", FieldKind::Number, false, -20.0, 100.0, {}, {}, ""},
        {"p1db_dBm", "P1dB", "dBm", FieldKind::Number, false, -20.0, 100.0, {}, {}, ""},
    };
    amp.create = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id) {
        return static_cast<IComponentEngine *>(&registry.add<AmplifierEngine>(id, graph));
    };
    // Legacy factory: applied library params directly. Removed in Task 2b.
    amp.factory = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id,
                     const nlohmann::json &parameters) -> IComponentEngine * {
        auto &e = registry.add<AmplifierEngine>(id, graph);
        if (parameters.contains("gain_dB"))
            e.setGain_dB(parameters["gain_dB"].get<double>());
        if (parameters.contains("nf_dB"))
            e.setNF_dB(parameters["nf_dB"].get<double>());
        if (parameters.contains("oip2_dBm"))
            e.setOIP2_dBm(parameters["oip2_dBm"].get<double>());
        if (parameters.contains("oip3_dBm"))
            e.setOIP3_dBm(parameters["oip3_dBm"].get<double>());
        if (parameters.contains("p1db_dBm"))
            e.setP1dB_dBm(parameters["p1db_dBm"].get<double>());
        bool has_nonlinear = parameters.contains("oip2_dBm") || parameters.contains("oip3_dBm") ||
                             parameters.contains("p1db_dBm");
        if (has_nonlinear)
            e.setEnableNonlinear(true);
        return &e;
    };
    m_descriptors.push_back(amp);

    ComponentTypeDescriptor att;
    att.type = "attenuator";
    att.project_type = "Attenuator";
    att.display_name = "Attenuator";
    att.menu_label = "Add Attenuator";
    att.label_prefix = "Attenuator";
    att.kind = NodeKind::Attenuator;
    att.authorable = true;
    att.fields = {
        {"attenuation_dB", "Attenuation", "dB", FieldKind::Number, true, 0.0, 100.0, {}, {}, ""},
    };
    att.create = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id) {
        return static_cast<IComponentEngine *>(&registry.add<AttenuatorEngine>(id, graph));
    };
    att.factory = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id,
                     const nlohmann::json &parameters) -> IComponentEngine * {
        auto &e = registry.add<AttenuatorEngine>(id, graph);
        if (parameters.contains("attenuation_dB"))
            e.setAttenuation(parameters["attenuation_dB"].get<double>());
        return &e;
    };
    m_descriptors.push_back(att);

    ComponentTypeDescriptor spl;
    spl.type = "splitter";
    spl.project_type = "Splitter";
    spl.display_name = "Splitter";
    spl.menu_label = "Add Splitter";
    spl.label_prefix = "Splitter";
    spl.kind = NodeKind::Splitter;
    spl.authorable = true;
    spl.create = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id) {
        return static_cast<IComponentEngine *>(&registry.add<SplitterEngine>(id, graph));
    };
    spl.factory = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id,
                     const nlohmann::json &) -> IComponentEngine * {
        auto &e = registry.add<SplitterEngine>(id, graph);
        return &e;
    };
    m_descriptors.push_back(spl);

    ComponentTypeDescriptor flt;
    flt.type = "filter";
    flt.project_type = "IdealFilter";
    flt.display_name = "IdealFilter";
    flt.menu_label = "Add Ideal Filter";
    flt.label_prefix = "IdealFilter";
    flt.kind = NodeKind::IdealFilter;
    flt.authorable = true;
    flt.fields = {
        {"filter_type",
         "Filter Type",
         "",
         FieldKind::Enum,
         true,
         0,
         0,
         {"LPF", "HPF", "BPF", "BSF"},
         {},
         ""},
        {"fc_low_Hz", "Low Cutoff", "Hz", FieldKind::Number, false, 0.0, 1e12, {}, {}, ""},
        {"fc_high_Hz", "High Cutoff", "Hz", FieldKind::Number, false, 0.0, 1e12, {}, {}, ""},
    };
    flt.create = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id) {
        return static_cast<IComponentEngine *>(&registry.add<IdealFilterEngine>(id, graph));
    };
    flt.factory = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id,
                     const nlohmann::json &parameters) -> IComponentEngine * {
        auto &e = registry.add<IdealFilterEngine>(id, graph);
        if (parameters.contains("filter_type")) {
            std::string ft = parameters["filter_type"].get<std::string>();
            if (ft == "LPF")
                e.setFilterType(FilterType::LPF);
            else if (ft == "HPF")
                e.setFilterType(FilterType::HPF);
            else if (ft == "BPF")
                e.setFilterType(FilterType::BPF);
            else if (ft == "BSF")
                e.setFilterType(FilterType::BSF);
        }
        double fc_low = parameters.value("fc_low_Hz", 100e6);
        double fc_high = parameters.value("fc_high_Hz", 200e6);
        if (parameters.contains("fc_low_Hz") && parameters.contains("fc_high_Hz"))
            e.setCutoffs_Hz(fc_low, fc_high);
        else if (parameters.contains("fc_low_Hz"))
            e.setCutoff_Hz(fc_low);
        return &e;
    };
    m_descriptors.push_back(flt);

    ComponentTypeDescriptor mix;
    mix.type = "mixer";
    mix.project_type = "Mixer";
    mix.display_name = "Mixer";
    mix.menu_label = "Add Mixer";
    mix.label_prefix = "Mixer";
    mix.kind = NodeKind::Mixer;
    mix.authorable = true;
    mix.fields = {
        {"lo_freq_Hz", "LO Frequency", "Hz", FieldKind::Number, true, 0.0, 1e12, {}, {}, ""},
        {"conversion_gain_dB",
         "Conversion Gain",
         "dB",
         FieldKind::Number,
         false,
         -60.0,
         30.0,
         {},
         {},
         ""},
        {"nf_dB", "Noise Figure", "dB", FieldKind::Number, false, 0.0, 30.0, {}, {}, ""},
    };
    mix.create = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id) {
        return static_cast<IComponentEngine *>(&registry.add<MixerEngine>(id, graph));
    };
    mix.factory = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id,
                     const nlohmann::json &parameters) -> IComponentEngine * {
        auto &e = registry.add<MixerEngine>(id, graph);
        if (parameters.contains("lo_freq_Hz"))
            e.setLoFreq_Hz(parameters["lo_freq_Hz"].get<double>());
        if (parameters.contains("conversion_gain_dB"))
            e.setConversionGain_dB(parameters["conversion_gain_dB"].get<double>());
        if (parameters.contains("nf_dB"))
            e.setNF_dB(parameters["nf_dB"].get<double>());
        return &e;
    };
    m_descriptors.push_back(mix);

    ComponentTypeDescriptor eq;
    eq.type = "equalizer";
    eq.project_type = "Equalizer";
    eq.display_name = "Equalizer";
    eq.menu_label = "Add Equalizer";
    eq.label_prefix = "Equalizer";
    eq.kind = NodeKind::Equalizer;
    eq.authorable = true;
    eq.fields = {
        {"ref_gain_dB", "Reference Gain", "dB", FieldKind::Number, false, -50.0, 50.0, {}, {}, ""},
        {"ref_freq_Hz",
         "Reference Frequency",
         "Hz",
         FieldKind::Number,
         false,
         0.0,
         1e12,
         {},
         {},
         ""},
        {"slope_dB_per_decade",
         "Slope",
         "dB/decade",
         FieldKind::Number,
         false,
         -100.0,
         100.0,
         {},
         {},
         ""},
    };
    eq.create = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id) {
        return static_cast<IComponentEngine *>(&registry.add<EqualizerEngine>(id, graph));
    };
    eq.factory = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id,
                    const nlohmann::json &parameters) -> IComponentEngine * {
        auto &e = registry.add<EqualizerEngine>(id, graph);
        if (parameters.contains("ref_gain_dB"))
            e.setRefGain_dB(parameters["ref_gain_dB"].get<double>());
        if (parameters.contains("ref_freq_Hz"))
            e.setRefFreq_Hz(parameters["ref_freq_Hz"].get<double>());
        if (parameters.contains("slope_dB_per_decade"))
            e.setSlope_dBPerDecade(parameters["slope_dB_per_decade"].get<double>());
        return &e;
    };
    m_descriptors.push_back(eq);

    ComponentTypeDescriptor comb;
    comb.type = "combiner";
    comb.project_type = "Combiner";
    comb.display_name = "Combiner";
    comb.menu_label = "Add Combiner";
    comb.label_prefix = "Combiner";
    comb.kind = NodeKind::Combiner;
    comb.authorable = true;
    comb.fields = {
        {"manual_mode", "Manual Mode", "", FieldKind::Bool, false, 0, 0, {}, false, ""},
    };
    comb.create = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id) {
        return static_cast<IComponentEngine *>(&registry.add<CombinerEngine>(id, graph));
    };
    comb.factory = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id,
                      const nlohmann::json &parameters) -> IComponentEngine * {
        auto &e = registry.add<CombinerEngine>(id, graph);
        if (parameters.contains("manual_mode"))
            e.setManualMode(parameters["manual_mode"].get<bool>());
        return &e;
    };
    m_descriptors.push_back(comb);

    ComponentTypeDescriptor adc;
    adc.type = "adc";
    adc.project_type = "ADC";
    adc.display_name = "ADC";
    adc.menu_label = "Add RF ADC";
    adc.label_prefix = "ADC";
    adc.kind = NodeKind::Adc;
    adc.authorable = true;
    adc.fields = {
        {"fs_Hz", "Sample Rate", "Hz", FieldKind::Number, true, 0.0, 1e12, {}, {}, ""},
        {"nsd_dBm_per_Hz",
         "Noise Spectral Density",
         "dBm/Hz",
         FieldKind::Number,
         false,
         -200.0,
         0.0,
         {},
         {},
         ""},
    };
    adc.create = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id) {
        return static_cast<IComponentEngine *>(&registry.add<AdcEngine>(id, graph));
    };
    adc.factory = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id,
                     const nlohmann::json &parameters) -> IComponentEngine * {
        auto &e = registry.add<AdcEngine>(id, graph);
        if (parameters.contains("fs_Hz"))
            e.setFs_Hz(parameters["fs_Hz"].get<double>());
        if (parameters.contains("nsd_dBm_per_Hz"))
            e.setNsd_dBm_per_Hz(parameters["nsd_dBm_per_Hz"].get<double>());
        return &e;
    };
    m_descriptors.push_back(adc);

    ComponentTypeDescriptor gen;
    gen.type = "generator";
    gen.project_type = "SignalGenerator";
    gen.display_name = "Generator";
    gen.menu_label = "Add Generator";
    gen.label_prefix = "Generator";
    gen.kind = NodeKind::Generator;
    gen.create = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id) {
        return static_cast<IComponentEngine *>(&registry.add<SignalGeneratorEngine>(id, graph));
    };
    m_descriptors.push_back(gen);

    ComponentTypeDescriptor coax;
    coax.type = "coax";
    coax.project_type = "CoaxCable";
    coax.display_name = "Coax Cable";
    coax.menu_label = "Add Coax Cable";
    coax.label_prefix = "Coax Cable";
    coax.kind = NodeKind::CoaxCable;
    coax.create = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id) {
        return static_cast<IComponentEngine *>(&registry.add<CoaxCableEngine>(id, graph));
    };
    m_descriptors.push_back(coax);

    ComponentTypeDescriptor pfb;
    pfb.type = "pfb";
    pfb.project_type = "PFBChannelizer";
    pfb.display_name = "PFB";
    pfb.menu_label = "Add PFB Channelizer";
    pfb.label_prefix = "PFB";
    pfb.kind = NodeKind::PFB;
    pfb.create = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id) {
        return static_cast<IComponentEngine *>(&registry.add<PFBChannelizerEngine>(id, graph));
    };
    m_descriptors.push_back(pfb);
}
```

- [ ] **Step 3: Update the registry-count test**

`tests/test_component_authoring.cpp` — change the `"ComponentTypeRegistry covers all 8 existing types"` TEST_CASE to expect 11:

```cpp
TEST_CASE("ComponentTypeRegistry covers all 11 existing types", "[type_registry]") {
    auto all = ComponentTypeRegistry::instance().all();
    std::vector<std::string> types;
    for (auto *d : all)
        types.push_back(d->type);
    std::sort(types.begin(), types.end());
    std::vector<std::string> expected = {"adc",       "amplifier", "attenuator", "coax",
                                         "combiner",  "equalizer", "filter",     "generator",
                                         "mixer",     "pfb",       "splitter"};
    REQUIRE(types == expected);
}
```

- [ ] **Step 4: Build**

Run: `cmake --build build`
Expected: compiles clean (additive change — `factory` still present, `ComponentLibrary` untouched).

- [ ] **Step 5: Run authoring tests**

Run: `build/bin/test_component_authoring.exe` (or without `.exe`).
Expected: all pass — 11-type registry test, amplifier descriptor field test, filter enum test, `ComponentLibrary::validate`/`upsert`/instantiate tests, form-model tests.

- [ ] **Step 6: Commit**

```bash
git add app/include/component_type_registry.h app/src/component_type_registry.cpp tests/test_component_authoring.cpp
git commit -m "refactor: extend ComponentTypeRegistry to all 11 types"
```

---

### Task 2b: Switch instantiate to `create()`+`deserialize()`, remove `factory`, fix deserialize key parity

**Files:**
- Modify: `app/src/component_library.cpp`
- Modify: `amplifier/src/amplifier_engine.cpp` (enable_nonlinear derivation)
- Modify: `adc/src/adc_engine.cpp` (`fs_Hz` key alias)
- Modify: `mixer/src/mixer_engine.cpp` (`conversion_gain_dB` key alias)
- Modify: `attenuator/src/attenuator_engine.cpp` (`attenuation_dB` key alias)
- Modify: `ideal_filter/src/ideal_filter_engine.cpp` (string enum + conditional cutoff logic)

**Interfaces:**
- Consumes: `ComponentTypeDescriptor::create` from Task 2a.
- Produces: `ComponentLibrary::instantiate()` creates via `create()` then applies params via `deserialize()`. Each affected engine's `deserialize()` accepts BOTH the engine's serialize keys (project files) and the library-form keys.

- [ ] **Step 1: Rewire `ComponentLibrary::instantiate`**

`app/src/component_library.cpp` — replace the factory call (around line 164) with:

```cpp
    IComponentEngine *result = descriptor->create(registry, graph, id);
    if (!result)
        return nullptr;
    result->deserialize(def.parameters);
```

Everything after (the `def.type == "amplifier"` S-param special case and part-number restore) stays.

- [ ] **Step 2: Amplifier — enable_nonlinear derivation parity**

`amplifier/src/amplifier_engine.cpp` — replace the body of `deserialize` with:

```cpp
void AmplifierEngine::deserialize(const nlohmann::json &j) {
    m_gain_dB = j.value("gain_dB", 0.0);
    m_nf_dB = j.value("nf_dB", 0.0);
    m_nonlinear.setEnabled(j.value("enable_nonlinear", false));
    m_nonlinear.setOIP2_dBm(j.value("oip2_dBm", 50.0));
    m_nonlinear.setOIP3_dBm(j.value("oip3_dBm", 50.0));
    m_nonlinear.setP1dB_dBm(j.value("p1db_dBm", 100.0));
    // Library definitions (schema v1/v2) omit `enable_nonlinear` but include
    // OIP/P1dB params. The old registry factory enabled nonlinearity whenever
    // any of those were present; project files always serialize the explicit
    // key, so only fall back when it is absent.
    if (!j.contains("enable_nonlinear") &&
        (j.contains("oip2_dBm") || j.contains("oip3_dBm") || j.contains("p1db_dBm")))
        m_nonlinear.setEnabled(true);
    m_sparam_mode = j.value("sparam_mode", false);
    m_sparam_filepath = j.value("sparam_filepath", "");
    m_sparam_fwd_idx = j.value("sparam_fwd_idx", 0);
    m_dirty = true;
}
```

- [ ] **Step 3: ADC — accept `fs_Hz` (library) alongside `sample_rate_Hz` (project)**

`adc/src/adc_engine.cpp` — replace the body of `deserialize` with:

```cpp
void AdcEngine::deserialize(const nlohmann::json &j) {
    m_fs_Hz = j.contains("sample_rate_Hz") ? j["sample_rate_Hz"].get<double>()
                                           : j.value("fs_Hz", 1e9);
    m_nsd_dBm_per_Hz = j.value("nsd_dBm_per_Hz", -155.0);
    m_dirty = true;
}
```

- [ ] **Step 4: Mixer — accept `conversion_gain_dB` (library) alongside `conv_gain_dB` (project)**

`mixer/src/mixer_engine.cpp` — replace the body of `deserialize` with:

```cpp
void MixerEngine::deserialize(const nlohmann::json &j) {
    m_lo_freq_Hz = j.value("lo_freq_Hz", 1e9);
    m_conv_gain_dB = j.contains("conv_gain_dB") ? j["conv_gain_dB"].get<double>()
                                                : j.value("conversion_gain_dB", -6.0);
    m_nf_dB = j.value("nf_dB", 0.0);
    m_dirty = true;
}
```

- [ ] **Step 5: Attenuator — accept `attenuation_dB` (library) alongside `atten_dB` (project)**

`attenuator/src/attenuator_engine.cpp` — replace the body of `deserialize` with:

```cpp
void AttenuatorEngine::deserialize(const nlohmann::json &j) {
    m_atten_dB = j.contains("atten_dB") ? j["atten_dB"].get<double>()
                                        : j.value("attenuation_dB", 0.0);
    m_sparam_mode = j.value("sparam_mode", false);
    m_sparam_path = j.value("sparam_path", "");
    m_dirty = true;
}
```

- [ ] **Step 6: Ideal filter — accept string enum + preserve factory cutoff logic**

`ideal_filter/src/ideal_filter_engine.cpp` — replace the body of `deserialize` with:

```cpp
void IdealFilterEngine::deserialize(const nlohmann::json &j) {
    if (j.contains("filter_type")) {
        if (j["filter_type"].is_string()) {
            const std::string ft = j["filter_type"].get<std::string>();
            if (ft == "LPF")
                m_type = FilterType::LPF;
            else if (ft == "HPF")
                m_type = FilterType::HPF;
            else if (ft == "BPF")
                m_type = FilterType::BPF;
            else if (ft == "BSF")
                m_type = FilterType::BSF;
        } else {
            int ft = j.value("filter_type", 0);
            if (ft < 0)
                ft = 0;
            if (ft > 3)
                ft = 3;
            m_type = static_cast<FilterType>(ft);
        }
    }
    // Preserve the old registry factory's conditional cutoff semantics:
    // both present -> setCutoffs; only fc_low -> setCutoff (mirrors high);
    // neither -> keep constructor defaults.
    if (j.contains("fc_low_Hz") && j.contains("fc_high_Hz")) {
        setCutoffs_Hz(j["fc_low_Hz"].get<double>(), j["fc_high_Hz"].get<double>());
    } else if (j.contains("fc_low_Hz")) {
        setCutoff_Hz(j["fc_low_Hz"].get<double>());
    }
    m_sparam_mode = j.value("sparam_mode", false);
    m_sparam_filepath = j.value("sparam_filepath", "");
    m_sparam_fwd_idx = j.value("sparam_fwd_idx", 0);
    m_dirty = true;
}
```

- [ ] **Step 7: Remove the now-dead `factory` field from the descriptor**

`app/include/component_type_registry.h` — delete the `factory` member and its comment. `app/src/component_type_registry.cpp` — delete all 8 `xxx.factory = ...` assignments (the bodies above have already been superseded; do not carry them into `create` — params are applied by the caller through `deserialize()`).

- [ ] **Step 8: Build**

Run: `cmake --build build`
Expected: compiles clean.

- [ ] **Step 9: Run library + authoring tests**

Run: `build/bin/test_component_authoring.exe` and `build/bin/tests.exe`.
Expected: all pass — `ComponentTypeRegistry` 11-type test, amplifier descriptor field test, filter enum test, `ComponentLibrary::validate`/`upsert`/instantiate tests, form-model tests.

- [ ] **Step 10: Commit**

```bash
git add app/src/component_library.cpp app/include/component_type_registry.h app/src/component_type_registry.cpp amplifier/src/amplifier_engine.cpp adc/src/adc_engine.cpp mixer/src/mixer_engine.cpp attenuator/src/attenuator_engine.cpp ideal_filter/src/ideal_filter_engine.cpp
git commit -m "refactor: instantiate via create()+deserialize(), drop factory field"
```

---

### Task 3: Rewire saveProject / loadProject / duplicateComponent through the registry

**Files:**
- Modify: `app/src/app.cpp` (saveProject type map ~line 422; loadProject branch chain ~line 586; duplicateComponent dynamic_cast chain ~line 224)

**Interfaces:**
- Consumes: `IComponentEngine::type_name()`, `ComponentTypeRegistry::find()`/`findByProjectType()`.
- Produces: saveProject writes `project_type` strings byte-identically; loadProject accepts legacy + canonical; duplicateComponent clones via `create()`+`deserialize()`.

- [ ] **Step 1: Rewire saveProject**

`app/src/app.cpp` `saveProject` — delete the `s_type_names` static map (lines ~422-441). Inside the component loop, replace:

```cpp
        auto it = s_type_names.find(std::type_index(typeid(*comp)));
        cj["type"] = (it != s_type_names.end()) ? it->second : "Unknown";
```

with:

```cpp
        const auto *desc = ComponentTypeRegistry::instance().find(comp->type_name());
        cj["type"] = desc ? desc->project_type : "Unknown";
```

Also remove the now-unused includes at the top of `app.cpp`: `<typeindex>` and `<unordered_map>` (verify with a build; `<unordered_map>` is used elsewhere in `app.cpp` for the pin map in `saveProject` — keep it if still referenced).

- [ ] **Step 2: Rewire loadProject**

`app/src/app.cpp` `loadProject` — replace the entire 11-branch `if (type == ...)` chain (from `IComponentEngine *comp = nullptr;` through the final `} else { LOG_WARN... }`) with:

```cpp
        const auto *desc = ComponentTypeRegistry::instance().findByProjectType(type);
        if (!desc) {
            LOG_WARN("Unknown component type in project file: %s", type.c_str());
            new_node_ids.push_back(-1);
            continue;
        }
        IComponentEngine *comp = desc->create(m_components, m_graph_engine, m_next_component_id++);
        comp->deserialize(params);
        if (desc->type == "pfb") {
            auto *pfb = static_cast<PFBChannelizerEngine *>(comp);
            // Restore IQ plot + PFB grid widgets for this PFB
            m_iq_widgets.push_back(std::make_unique<IQPlotWidget>(*pfb));
            m_show_iq_pfbs.push_back(true);
            m_pfb_grid_widgets.push_back(std::make_unique<PFBChannelizerWidget>(*pfb));
            m_show_pfb_grids.push_back(true);
        }

        new_node_ids.push_back(comp ? comp->graphNodeId() : -1);
```

> `comp` is never null on this path (create always succeeds); the rest of the loop (position restore, part-number restore) keeps using `comp` unchanged. The PFB block above is replaced by `m_pfb_views.addFor(...)` in Phase 2.

- [ ] **Step 3: Rewire duplicateComponent**

`app/src/app.cpp` `duplicateComponent` — delete the `dup` lambda and the entire 11-way `dynamic_cast` chain. Replace from the `// Helper: create a new engine of type T...` comment through the final closing brace of the if-chain with:

```cpp
    // Clone via the registry: create a default engine, then copy params through
    // serialize/deserialize. Removes the 11-way dynamic_cast chain.
    const auto *desc = ComponentTypeRegistry::instance().find(src->type_name());
    if (!desc)
        return;
    IComponentEngine *copy = desc->create(m_components, m_graph_engine, m_next_component_id++);
    copy->deserialize(src->serialize());
    int new_nid = copy->graphNodeId();
    // Register with imnodes pool and set position
    ImNodes::EditorContextSet(m_graph_widget->context());
    ImNodes::SetNodeEditorSpacePos(new_nid, ImVec2(src_pos.x + OFFSET, src_pos.y + OFFSET));
    // Copy library part number
    if (!src_part_number.empty())
        m_graph_engine.setNodePartNumber(new_nid, src_part_number);
    // PFB also needs IQ plot widget and grid widget (same as onAddPFB)
    if (desc->type == "pfb") {
        auto *new_pfb = static_cast<PFBChannelizerEngine *>(copy);
        m_iq_widgets.push_back(std::make_unique<IQPlotWidget>(*new_pfb));
        m_show_iq_pfbs.push_back(m_state.loadBool(
            "WindowState", ("IQPlot_" + std::to_string(new_pfb->id())).c_str(), true));
        m_pfb_grid_widgets.push_back(std::make_unique<PFBChannelizerWidget>(*new_pfb));
        m_show_pfb_grids.push_back(m_state.loadBool(
            "WindowState", ("PFBGrid_" + std::to_string(new_pfb->id())).c_str(), true));
    }

    markDirty();
```

> The PFB block above is replaced by `m_pfb_views.addFor(...)` in Phase 2.

- [ ] **Step 4: Build**

Run: `cmake --build build`
Expected: compiles clean.

- [ ] **Step 5: Run round-trip tests**

Run: `build/bin/test_project_file.exe`, `build/bin/tests.exe`, `build/bin/test_component_authoring.exe`.
Expected: all pass (save/load/duplicate rewired without breaking round-trips).

- [ ] **Step 6: Commit**

```bash
git add app/src/app.cpp
git commit -m "refactor: save/load/duplicate dispatch through ComponentTypeRegistry"
```

---

### Task 4: Data-driven canvas menu + unified addComponent (fixes Equalizer bug)

**Files:**
- Create: `tests/test_component_dispatch.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `node_graph/include/node_graph_widget.h`
- Modify: `node_graph/src/node_graph_widget.cpp`
- Modify: `app/include/app.h`
- Modify: `app/src/app.cpp`

**Interfaces:**
- Consumes: `ComponentTypeRegistry::all()`.
- Produces: `NodeGraphWidget::setAddableComponents(std::vector<AddableComponent>)` replacing the 11 `onAdd*` callbacks; `RfSimulatorApp::addComponent(const ComponentTypeDescriptor*, ImVec2)`; the canvas context menu iterates the list. `test_component_dispatch.cpp` hosts the Equalizer dirty-flag regression (red→green this task).

- [ ] **Step 1: Write the failing regression test (old API still present)**

`tests/test_component_dispatch.cpp` — create the new standalone test file:

```cpp
#include "app.h"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
#include <catch2/catch_test_macros.hpp>

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

TEST_CASE_METHOD(ImGuiFixture, "Adding an Equalizer marks the project dirty (issue #51)",
                 "[dispatch][regression]") {
    RfSimulatorApp app;
    REQUIRE(app.isDirty() == false);
    // Canvas menu path for Equalizer. Today's onAddEqualizer lambda omits
    // markDirty(); the unified addComponent path always marks dirty.
    app.testGraphWidget().onAddEqualizer(ImVec2(0, 0));
    REQUIRE(app.isDirty() == true);
}
```

Add to `tests/CMakeLists.txt` (after the `test_issue37_pfb_input_removal` block):

```cmake
add_executable(test_component_dispatch test_component_dispatch.cpp)
target_link_libraries(test_component_dispatch PRIVATE
    simulator::app
    Catch2::Catch2WithMain
)
target_compile_definitions(test_component_dispatch PRIVATE PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME test_component_dispatch COMMAND test_component_dispatch WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
```

- [ ] **Step 2: Build + run the new test to see it fail**

Run: `cmake --build build && build/bin/test_component_dispatch.exe`
Expected: FAIL — `app.isDirty()` is still `false` after `onAddEqualizer` (the bug).

- [ ] **Step 3: Widget header — replace 11 callbacks with one list**

`node_graph/include/node_graph_widget.h` — delete the 11 `onAdd*` members (`onAddGenerator` ... `onAddCombiner`). Add:

```cpp
    // Data-driven canvas menu: app populates from ComponentTypeRegistry.
    struct AddableComponent {
        std::string menu_label;
        std::function<void(ImVec2)> on_add;
    };
    void setAddableComponents(std::vector<AddableComponent> addable) {
        m_addable_components = std::move(addable);
    }
    const std::vector<AddableComponent> &addableComponents() const { return m_addable_components; }
```

In the private section, add:

```cpp
    std::vector<AddableComponent> m_addable_components;
```

- [ ] **Step 4: Widget cpp — iterate the list in handleContextMenu**

`node_graph/src/node_graph_widget.cpp` `handleContextMenu` — replace the 11 `if (ImGui::MenuItem(...))` blocks inside `if (ImGui::BeginPopup("canvas_context_menu"))` with:

```cpp
        for (const auto &addable : m_addable_components) {
            if (ImGui::MenuItem(addable.menu_label.c_str())) {
                if (addable.on_add)
                    addable.on_add(m_context_menu_pos);
            }
        }
```

- [ ] **Step 5: App header — add addComponent**

`app/include/app.h` — in the private section add:

```cpp
    void addComponent(const ComponentTypeDescriptor *desc, ImVec2 pos);
```

- [ ] **Step 6: App cpp — build the menu from the registry + unified add path**

`app/src/app.cpp` constructor — delete all 11 `m_graph_widget->onAdd* = ...` lambda assignments (Generator through Combiner). Replace them with:

```cpp
    std::vector<NodeGraphWidget::AddableComponent> addable;
    for (const auto *desc : ComponentTypeRegistry::instance().all()) {
        addable.push_back({desc->menu_label,
                           [this, desc](ImVec2 pos) { addComponent(desc, pos); }});
    }
    m_graph_widget->setAddableComponents(std::move(addable));
```

Add the method at file scope after the constructor:

```cpp
void RfSimulatorApp::addComponent(const ComponentTypeDescriptor *desc, ImVec2 pos) {
    IComponentEngine *comp = desc->create(m_components, m_graph_engine, m_next_component_id++);
    ImNodes::EditorContextSet(m_graph_widget->context());
    ImNodes::SetNodeEditorSpacePos(comp->graphNodeId(), pos);
    if (desc->type == "pfb") {
        auto *pfb = static_cast<PFBChannelizerEngine *>(comp);
        m_iq_widgets.push_back(std::make_unique<IQPlotWidget>(*pfb));
        m_show_iq_pfbs.push_back(m_state.loadBool(
            "WindowState", ("IQPlot_" + std::to_string(pfb->id())).c_str(), true));
        m_pfb_grid_widgets.push_back(std::make_unique<PFBChannelizerWidget>(*pfb));
        m_show_pfb_grids.push_back(m_state.loadBool(
            "WindowState", ("PFBGrid_" + std::to_string(pfb->id())).c_str(), true));
    }
    markDirty(); // unconditional — fixes the Equalizer missing-markDirty bug
}
```

> The PFB block above is replaced by `m_pfb_views.addFor(...)` in Phase 2.

- [ ] **Step 7: Update the test to the new menu API**

`tests/test_component_dispatch.cpp` — replace the test body with:

```cpp
TEST_CASE_METHOD(ImGuiFixture, "Adding an Equalizer marks the project dirty (issue #51)",
                 "[dispatch][regression]") {
    RfSimulatorApp app;
    REQUIRE(app.isDirty() == false);
    bool clicked = false;
    for (const auto &addable : app.testGraphWidget().addableComponents()) {
        if (addable.menu_label == "Add Equalizer") {
            addable.on_add(ImVec2(0, 0));
            clicked = true;
        }
    }
    REQUIRE(clicked);
    REQUIRE(app.isDirty() == true);
}
```

- [ ] **Step 8: Build + run tests**

Run: `cmake --build build && build/bin/test_component_dispatch.exe`
Expected: PASS (Equalizer dirty regression green). Also run `build/bin/tests.exe` and `build/bin/test_ui.exe` (Xvfb on Linux; on Windows run directly) — all pass, including UI tests that click `Add Splitter` / `Add Mixer` / `Add Coax Cable` menu items (labels unchanged).

- [ ] **Step 9: Commit**

```bash
git add tests/test_component_dispatch.cpp tests/CMakeLists.txt node_graph/include/node_graph_widget.h node_graph/src/node_graph_widget.cpp app/include/app.h app/src/app.cpp
git commit -m "feat: data-driven canvas menu with unified addComponent path"
```

---

### Task 5: Inspector dispatch through the registry

**Files:**
- Modify: `app/include/inspector_panel.h`
- Modify: `app/src/inspector_panel.cpp`
- Modify: `app/src/app.cpp` (call `registerDrawers`)

**Interfaces:**
- Consumes: `ComponentTypeDescriptor::draw_inspector`, `type_name()`.
- Produces: `Hit { const ComponentTypeDescriptor* desc; IComponentEngine* engine; }` (the private `ComponentType` enum is deleted); `findSelected()` via `registry.find(engine->type_name())`; `draw()` dispatches via `desc->draw_inspector`; `InspectorPanel::registerDrawers(ComponentTypeRegistry&)` assigns the per-type lambdas; `drawXProperties` methods become public.

- [ ] **Step 1: Header changes**

`app/include/inspector_panel.h`:
- Add `#include "component_type_registry.h"`.
- Delete the `enum class ComponentType { ... };` block.
- Change `Hit` to:

```cpp
    struct Hit {
        const ComponentTypeDescriptor *desc = nullptr;
        IComponentEngine *engine = nullptr;
    };
```

- Move the 11 `drawXProperties` + `drawPFBProperties` + `drawGroupPanel` declarations from `private:` to `public:`.
- In the public section add:

```cpp
    // Called once at startup; wires ComponentTypeRegistry draw_inspector
    // callbacks to this panel's property drawers.
    void registerDrawers(ComponentTypeRegistry &registry);
```

- [ ] **Step 2: findSelected + labelForHit**

`app/src/inspector_panel.cpp` `findSelected` — replace the body after the `engine` null-check with:

```cpp
    return {ComponentTypeRegistry::instance().find(engine->type_name()), engine};
```

`labelForHit` — replace the whole body with:

```cpp
std::string InspectorPanel::labelForHit(const Hit &hit) const {
    if (!hit.desc || !hit.engine)
        return "";
    return hit.desc->display_name + " " + std::to_string(hit.engine->id());
}
```

> This also fixes the latent bug where CoaxCable/Equalizer fell through to an empty panel title.

- [ ] **Step 3: draw() dispatch**

`inspector_panel.cpp` `draw` — replace the big `switch (hit.type) { ... }` with:

```cpp
    if (hit.desc->type == "pfb") {
        // PFB keeps its multi-instance selector combo (needs m_pfb_ptrs).
        auto *pfb = static_cast<PFBChannelizerEngine *>(hit.engine);
        for (int i = 0; i < static_cast<int>(m_pfb_ptrs.size()); ++i) {
            if (m_pfb_ptrs[i] == pfb) {
                m_selected_pfb_index = i;
                break;
            }
        }
        if (!m_pfb_ptrs.empty()) {
            int display_id = (m_selected_pfb_index < static_cast<int>(m_pfb_ptrs.size()) &&
                              m_pfb_ptrs[m_selected_pfb_index])
                                 ? m_pfb_ptrs[m_selected_pfb_index]->id()
                                 : m_selected_pfb_index;
            std::string combo_label = "PFB##selector";
            std::string preview = "PFB " + std::to_string(display_id);
            if (ImGui::BeginCombo(combo_label.c_str(), preview.c_str())) {
                for (int i = 0; i < static_cast<int>(m_pfb_ptrs.size()); ++i) {
                    if (!m_pfb_ptrs[i])
                        continue;
                    bool selected = (i == m_selected_pfb_index);
                    std::string item = "PFB " + std::to_string(m_pfb_ptrs[i]->id());
                    if (ImGui::Selectable(item.c_str(), &selected))
                        m_selected_pfb_index = i;
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }
    }

    if (hit.desc->draw_inspector)
        hit.desc->draw_inspector(*this, *hit.engine);

    if (m_param_edited && onParamChange)
        onParamChange();
```

> The PFB block previously ended by calling `drawPFBProperties` inside the switch; that call now happens via the PFB `draw_inspector` lambda in `registerDrawers` (Step 4).

- [ ] **Step 4: registerDrawers**

`inspector_panel.cpp` — add this member definition:

```cpp
void InspectorPanel::registerDrawers(ComponentTypeRegistry &registry) {
    for (auto *d : registry.all()) {
        if (d->type == "generator") {
            d->draw_inspector = [](InspectorPanel &p, IComponentEngine &e) {
                p.drawGeneratorProperties(static_cast<SignalGeneratorEngine &>(e), e.id());
            };
        } else if (d->type == "amplifier") {
            d->draw_inspector = [](InspectorPanel &p, IComponentEngine &e) {
                p.drawAmplifierProperties(static_cast<AmplifierEngine &>(e), e.id());
            };
        } else if (d->type == "splitter") {
            d->draw_inspector = [](InspectorPanel &p, IComponentEngine &e) {
                p.drawSplitterProperties(static_cast<SplitterEngine &>(e), e.id());
            };
        } else if (d->type == "mixer") {
            d->draw_inspector = [](InspectorPanel &p, IComponentEngine &e) {
                p.drawMixerProperties(static_cast<MixerEngine &>(e), e.id());
            };
        } else if (d->type == "adc") {
            d->draw_inspector = [](InspectorPanel &p, IComponentEngine &e) {
                p.drawAdcProperties(static_cast<AdcEngine &>(e), e.id());
            };
        } else if (d->type == "pfb") {
            d->draw_inspector = [](InspectorPanel &p, IComponentEngine &e) {
                p.drawPFBProperties(static_cast<PFBChannelizerEngine &>(e));
            };
        } else if (d->type == "filter") {
            d->draw_inspector = [](InspectorPanel &p, IComponentEngine &e) {
                p.drawIdealFilterProperties(static_cast<IdealFilterEngine &>(e), e.id());
            };
        } else if (d->type == "coax") {
            d->draw_inspector = [](InspectorPanel &p, IComponentEngine &e) {
                p.drawCoaxCableProperties(static_cast<CoaxCableEngine &>(e), e.id());
            };
        } else if (d->type == "equalizer") {
            d->draw_inspector = [](InspectorPanel &p, IComponentEngine &e) {
                p.drawEqualizerProperties(static_cast<EqualizerEngine &>(e), e.id());
            };
        } else if (d->type == "attenuator") {
            d->draw_inspector = [](InspectorPanel &p, IComponentEngine &e) {
                p.drawAttenuatorProperties(static_cast<AttenuatorEngine &>(e), e.id());
            };
        } else if (d->type == "combiner") {
            d->draw_inspector = [](InspectorPanel &p, IComponentEngine &e) {
                p.drawCombinerProperties(static_cast<CombinerEngine &>(e), e.id());
            };
        }
    }
}
```

- [ ] **Step 5: Call registerDrawers at startup**

`app/src/app.cpp` constructor — right after `m_inspector_panel = std::make_unique<InspectorPanel>(...)` add:

```cpp
    m_inspector_panel->registerDrawers(ComponentTypeRegistry::instance());
```

- [ ] **Step 6: Build**

Run: `cmake --build build`
Expected: compiles clean.

- [ ] **Step 7: Run tests**

Run: `build/bin/tests.exe` and `build/bin/test_ui.exe`.
Expected: all pass (UI tests still click the same menu labels; inspector behavior unchanged).

- [ ] **Step 8: Commit**

```bash
git add app/include/inspector_panel.h app/src/inspector_panel.cpp app/src/app.cpp
git commit -m "refactor: inspector dispatch through ComponentTypeRegistry"
```

---

### Task 6: Data-driven node-kind mapping (delete nodeKindFromLabel)

**Files:**
- Modify: `node_graph/include/node_graph_engine.h`
- Modify: `node_graph/include/node_graph_widget.h`
- Modify: `node_graph/src/node_graph_widget.cpp`
- Modify: `app/src/app.cpp`
- Modify: `tests/test_node_graph_engine.cpp`
- Modify: `tests/test_component_dispatch.cpp`

**Interfaces:**
- Consumes: descriptor `label_prefix` + `kind` from the registry.
- Produces: `NodeGraphWidget::registerNodeKind(std::string label_prefix, NodeKind kind)` and public `kindForLabel(const std::string&)` replacing `nodeKindFromLabel`.

- [ ] **Step 1: Delete nodeKindFromLabel from the engine header**

`node_graph/include/node_graph_engine.h` — delete the `nodeKindFromLabel` inline function (the 11-branch prefix chain). Keep `NodeKind` enum and `themeColor` (still used by the widget).

- [ ] **Step 2: Widget header — register + lookup**

`node_graph/include/node_graph_widget.h` — public section add:

```cpp
    void registerNodeKind(std::string label_prefix, NodeKind kind) {
        m_kind_prefixes.push_back({std::move(label_prefix), kind});
    }
    NodeKind kindForLabel(const std::string &label) const;
```

private section add:

```cpp
    std::vector<std::pair<std::string, NodeKind>> m_kind_prefixes;
```

- [ ] **Step 3: Widget cpp — implement kindForLabel + use it**

`node_graph/src/node_graph_widget.cpp` — add:

```cpp
NodeKind NodeGraphWidget::kindForLabel(const std::string &label) const {
    for (const auto &[prefix, kind] : m_kind_prefixes)
        if (label.rfind(prefix, 0) == 0)
            return kind;
    return NodeKind::Unknown;
}
```

Replace the call site in `drawNodes` (currently `const NodeKind kind = nodeKindFromLabel(node.label);`) with:

```cpp
        const NodeKind kind = kindForLabel(node.label);
```

- [ ] **Step 4: App feeds the mapping**

`app/src/app.cpp` constructor — inside the registry loop that builds the addable list, add:

```cpp
        m_graph_widget->registerNodeKind(desc->label_prefix, desc->kind);
```

- [ ] **Step 5: Drop the obsolete nodeKindFromLabel tests**

`tests/test_node_graph_engine.cpp` — delete the two TEST_CASEs `"nodeKindFromLabel maps known prefixes"` and `"nodeKindFromLabel returns Unknown for unrecognised input"`. KEEP the `themeColor` TEST_CASE.

- [ ] **Step 6: Add the replacement registry-driven test**

`tests/test_component_dispatch.cpp` — append:

```cpp
TEST_CASE_METHOD(ImGuiFixture, "Every registry label_prefix maps to its kind", "[dispatch]") {
    RfSimulatorApp app;
    for (const auto *d : ComponentTypeRegistry::instance().all()) {
        REQUIRE(app.testGraphWidget().kindForLabel(d->label_prefix + " 1") == d->kind);
    }
    REQUIRE(app.testGraphWidget().kindForLabel("UnknownThing 1") == NodeKind::Unknown);
}
```

Add `#include "component_type_registry.h"` to the test file.

- [ ] **Step 7: Build + run tests**

Run: `cmake --build build && build/bin/test_component_dispatch.exe`, then `build/bin/tests.exe` and `build/bin/test_ui.exe`.
Expected: all pass.

- [ ] **Step 8: Commit**

```bash
git add node_graph/include/node_graph_engine.h node_graph/include/node_graph_widget.h node_graph/src/node_graph_widget.cpp app/src/app.cpp tests/test_node_graph_engine.cpp tests/test_component_dispatch.cpp
git commit -m "refactor: data-driven NodeKind mapping in node graph widget"
```

---

### Task 7: New Component form combo from the registry

**Files:**
- Modify: `app/src/app.cpp` (`drawComponentFormModal`)

**Interfaces:**
- Consumes: `ComponentTypeDescriptor::authorable`.

- [ ] **Step 1: Replace the hardcoded combo**

`app/src/app.cpp` `drawComponentFormModal` — inside the `if (!m_component_form_is_edit)` block, replace the hardcoded `type_names[]` + combo with:

```cpp
            std::vector<const ComponentTypeDescriptor *> authorable;
            for (auto *d : ComponentTypeRegistry::instance().all())
                if (d->authorable)
                    authorable.push_back(d);
            static int type_idx = 0;
            for (size_t i = 0; i < authorable.size(); ++i)
                if (m_component_form_model->descriptor().type == authorable[i]->type)
                    type_idx = static_cast<int>(i);
            std::vector<const char *> type_names;
            for (auto *d : authorable)
                type_names.push_back(d->type.c_str());
            if (ImGui::Combo("Type", &type_idx, type_names.data(),
                             static_cast<int>(type_names.size())))
                openNewComponentForm(authorable[type_idx]->type);
```

- [ ] **Step 2: Build**

Run: `cmake --build build`
Expected: compiles clean.

- [ ] **Step 3: Run authoring tests**

Run: `build/bin/test_component_authoring.exe`.
Expected: all pass (form model unchanged; combo now derived from registry `authorable` rows, same 8 types in the same order).

- [ ] **Step 4: Commit**

```bash
git add app/src/app.cpp
git commit -m "refactor: New Component form combo driven by registry authorable types"
```

---

## Phase 2 — PFBViewManager

### Task 8: PFBViewManager owns the lockstep PFB widget vectors

**Files:**
- Create: `app/include/pfb_view_manager.h`
- Create: `app/src/pfb_view_manager.cpp`
- Modify: `app/CMakeLists.txt`
- Modify: `app/include/app.h`
- Modify: `app/src/app.cpp`

**Interfaces:**
- Consumes: `ComponentRegistry::byType<PFBChannelizerEngine>()`, `SessionState`.
- Produces: `PFBViewManager` with `addFor(engine, state)`, `rebuild(components, state)`, `clear()`, `draw()`, `saveVisibility(components, state)`, `iqVisibility()`, `gridVisibility()`. Replaces the app's `m_iq_widgets`/`m_show_iq_pfbs`/`m_pfb_grid_widgets`/`m_show_pfb_grids` members and all six rebuild sites.

- [ ] **Step 1: Write the header**

`app/include/pfb_view_manager.h`:

```cpp
#pragma once

#include "iq_plot_widget.h"
#include "pfb_channelizer_engine.h"
#include "pfb_channelizer_widget.h"
#include <memory>
#include <vector>

class ComponentRegistry;
class SessionState;

// Owns the per-PFB view widgets and their visibility flags. The app's old
// four lockstep vectors (m_iq_widgets/m_show_iq_pfbs/m_pfb_grid_widgets/
// m_show_pfb_grids) were rebuilt by hand at six call sites and caused issue
// #37 (use-after-free). All lifecycle now funnels through this class.
class PFBViewManager {
  public:
    void addFor(PFBChannelizerEngine &engine, SessionState &state);
    void rebuild(const ComponentRegistry &components, SessionState &state);
    void clear();
    void draw();
    void saveVisibility(const ComponentRegistry &components, SessionState &state) const;

    std::vector<bool> &iqVisibility() { return m_show_iq_pfbs; }
    std::vector<bool> &gridVisibility() { return m_show_pfb_grids; }

  private:
    std::vector<std::unique_ptr<IQPlotWidget>> m_iq_widgets;
    std::vector<bool> m_show_iq_pfbs;
    std::vector<std::unique_ptr<PFBChannelizerWidget>> m_pfb_grid_widgets;
    std::vector<bool> m_show_pfb_grids;
};
```

- [ ] **Step 2: Write the implementation**

`app/src/pfb_view_manager.cpp`:

```cpp
#include "pfb_view_manager.h"
#include "component_registry.h"
#include "session_state.h"

void PFBViewManager::addFor(PFBChannelizerEngine &engine, SessionState &state) {
    m_iq_widgets.push_back(std::make_unique<IQPlotWidget>(engine));
    m_show_iq_pfbs.push_back(
        state.loadBool("WindowState", ("IQPlot_" + std::to_string(engine.id())).c_str(), true));
    m_pfb_grid_widgets.push_back(std::make_unique<PFBChannelizerWidget>(engine));
    m_show_pfb_grids.push_back(
        state.loadBool("WindowState", ("PFBGrid_" + std::to_string(engine.id())).c_str(), true));
}

void PFBViewManager::rebuild(const ComponentRegistry &components, SessionState &state) {
    clear();
    for (auto *pfb : components.byType<PFBChannelizerEngine>())
        addFor(*pfb, state);
}

void PFBViewManager::clear() {
    m_iq_widgets.clear();
    m_show_iq_pfbs.clear();
    m_pfb_grid_widgets.clear();
    m_show_pfb_grids.clear();
}

void PFBViewManager::draw() {
    for (size_t i = 0; i < m_iq_widgets.size(); ++i) {
        if (m_show_iq_pfbs[i]) {
            std::string label = "IQ Plot - PFB " + std::to_string(i);
            bool show = m_show_iq_pfbs[i];
            m_iq_widgets[i]->draw(label.c_str(), &show);
            m_show_iq_pfbs[i] = show;
        }
    }
    for (size_t i = 0; i < m_pfb_grid_widgets.size(); ++i) {
        if (m_show_pfb_grids[i]) {
            std::string label = "Channelizer Grid - PFB " + std::to_string(i);
            bool show = m_show_pfb_grids[i];
            m_pfb_grid_widgets[i]->draw(label.c_str(), &show);
            m_show_pfb_grids[i] = show;
        }
    }
}

void PFBViewManager::saveVisibility(const ComponentRegistry &components,
                                    SessionState &state) const {
    auto pfb_vec = components.byType<PFBChannelizerEngine>();
    for (size_t i = 0; i < m_show_iq_pfbs.size() && i < pfb_vec.size(); ++i) {
        std::string key = "IQPlot_" + std::to_string(pfb_vec[i]->id());
        state.saveBool("WindowState", key.c_str(), m_show_iq_pfbs[i]);
    }
    for (size_t i = 0; i < m_show_pfb_grids.size() && i < pfb_vec.size(); ++i) {
        std::string key = "PFBGrid_" + std::to_string(pfb_vec[i]->id());
        state.saveBool("WindowState", key.c_str(), m_show_pfb_grids[i]);
    }
}
```

- [ ] **Step 3: CMake**

`app/CMakeLists.txt` — add `src/pfb_view_manager.cpp` to the `add_library(app STATIC ...)` list.

- [ ] **Step 4: App header — swap the four vectors for the manager**

`app/include/app.h` — delete members `m_iq_widgets`, `m_show_iq_pfbs`, `m_pfb_grid_widgets`, `m_show_pfb_grids`. Add `#include "pfb_view_manager.h"` to the include block (alphabetical). Add (declared AFTER `m_components` so the manager is destroyed before the engines its widgets reference):

```cpp
    PFBViewManager m_pfb_views;
```

- [ ] **Step 5: App cpp — replace the six rebuild sites**

1. `addComponent` PFB block → replace the four pushes with:

```cpp
        m_pfb_views.addFor(*static_cast<PFBChannelizerEngine *>(comp), m_state);
```

2. `onRemoveNode` lambda — replace the `pfb_vec` + four clear/push blocks with:

```cpp
        m_pfb_views.rebuild(m_components, m_state);
```

3. `duplicateComponent` PFB block → replace the four pushes with:

```cpp
        m_pfb_views.addFor(*static_cast<PFBChannelizerEngine *>(copy), m_state);
```

4. `newProject` — replace the four `clear()` calls with `m_pfb_views.clear();`

5. `loadProject` PFB block → replace the four pushes with:

```cpp
        m_pfb_views.addFor(*static_cast<PFBChannelizerEngine *>(comp), m_state);
```

6. `~RfSimulatorApp` — replace the two `m_show_iq_pfbs`/`m_show_pfb_grids` save loops with:

```cpp
    m_pfb_views.saveVisibility(m_components, m_state);
```

Also replace the `draw_ui()` IQ-plot and channelizer-grid loops (the two `for (size_t i = 0; i < m_iq_widgets.size(); ...)` blocks) with:

```cpp
    m_pfb_views.draw();
```

And the `InspectorPanel::setPFBWindowVisibility` call site (around line 975) becomes:

```cpp
    m_inspector_panel->setPFBWindowVisibility(&m_pfb_views.iqVisibility(),
                                              &m_pfb_views.gridVisibility());
```

- [ ] **Step 6: Build**

Run: `cmake --build build`
Expected: compiles clean.

- [ ] **Step 7: Run tests**

Run: `build/bin/test_issue37_pfb_input_removal.exe`, `build/bin/test_project_file.exe`, `build/bin/test_component_dispatch.exe`, `build/bin/tests.exe`.
Expected: all pass — especially issue #37 (PFB input removal) which exercises the rebuilt widget lifecycle.

- [ ] **Step 8: Commit**

```bash
git add app/include/pfb_view_manager.h app/src/pfb_view_manager.cpp app/CMakeLists.txt app/include/app.h app/src/app.cpp
git commit -m "refactor: extract PFBViewManager from RfSimulatorApp"
```

---

## Phase 3 — ProjectSerializer

### Task 9: ProjectSerializer owns save/load/new JSON logic

**Files:**
- Create: `app/include/project_serializer.h`
- Create: `app/src/project_serializer.cpp`
- Modify: `app/CMakeLists.txt`
- Modify: `app/include/app.h`
- Modify: `app/src/app.cpp`

**Interfaces:**
- Consumes: `ComponentRegistry`, `NodeGraphEngine`, `NodeGraphWidget` (imnodes positions), `PFBViewManager`, `SessionState`, id counter.
- Produces: `ProjectSerializer` with `save(path)`, `load(path) -> bool`, `reset()`; `RfSimulatorApp::saveProject`/`loadProject`/`newProject` become thin wrappers.

- [ ] **Step 1: Write the header**

`app/include/project_serializer.h`:

```cpp
#pragma once

#include <string>

class ComponentRegistry;
class NodeGraphEngine;
class NodeGraphWidget;
class PFBViewManager;
class SessionState;

// Owns the .rfsim JSON save/load/new logic previously inlined in
// RfSimulatorApp (issue #51: 1320-line god-object).
class ProjectSerializer {
  public:
    ProjectSerializer(ComponentRegistry &components, NodeGraphEngine &graph,
                      NodeGraphWidget &graph_widget, PFBViewManager &pfb_views,
                      SessionState &state, int &next_component_id);

    void save(const std::string &path);
    bool load(const std::string &path); // false on parse/unknown-type failure (logged)
    void reset();                       // newProject: links, components, probes, counters, PFBs

  private:
    ComponentRegistry &m_components;
    NodeGraphEngine &m_graph;
    NodeGraphWidget &m_graph_widget;
    PFBViewManager &m_pfb_views;
    SessionState &m_state;
    int &m_next_component_id;
};
```

- [ ] **Step 2: Move save/load/new bodies**

`app/src/project_serializer.cpp` — move these bodies VERBATIM from `app/src/app.cpp`, with these substitutions:

- `save(const std::string &path)`: copy the current `RfSimulatorApp::saveProject` body (lines ~413-560). Replace member accesses: `m_graph_engine` → `m_graph`, `m_graph_widget->` → `m_graph_widget.`, `m_components` → `m_components`, `m_next_component_id` → `m_next_component_id`. The registry type lookup from Task 3 stays. DELETE the tail lines `m_current_project_path = path; m_dirty = false;` — those become app-wrapper responsibilities.
- `load(const std::string &path) -> bool`: copy the current `loadProject` body (lines ~565-740). Keep the `if (!in)` / catch / reset-at-start; replace the internal `newProject();` call with `reset();`. Keep the `LOG_WARN` unknown-type path. DELETE the tail `m_current_project_path = path; refreshExtensions(); m_dirty = false;` — app wrapper handles those. Return `true` after `LOG_INFO("Loaded project from %s", path.c_str());` and `false` on the two early-return error paths (open failure, parse failure).
- `reset()`: copy the current `newProject` body minus the app-owned lines. KEEP: `m_graph.removeAllLinks()`, component removal loop, `m_graph.clearProbes()`, `m_pfb_views.clear()`, `m_graph.setNextIds(...)`, `setNextGroupId(...)`, `setNextBoundaryPinId(...)`, `m_next_component_id = 100`, `m_graph_widget.clearPositionCache()`. LEAVE in the app wrapper: `m_spectrum_widget->setProbeLabels({})`, `m_current_project_path.clear()`, `refreshExtensions()`, `m_dirty = false`.

- [ ] **Step 3: CMake**

`app/CMakeLists.txt` — add `src/project_serializer.cpp` to `add_library(app STATIC ...)`.

- [ ] **Step 4: App header**

`app/include/app.h` — add `#include "project_serializer.h"`; add member (declared AFTER `m_graph_widget` and `m_pfb_views` so it is constructed after them):

```cpp
    std::unique_ptr<ProjectSerializer> m_serializer;
```

- [ ] **Step 5: App cpp — thin wrappers**

In the constructor body, right after `m_graph_widget = std::make_unique<NodeGraphWidget>(m_graph_engine);` (m_pfb_views and m_components already exist as members):

```cpp
    m_serializer = std::make_unique<ProjectSerializer>(
        m_components, m_graph_engine, *m_graph_widget, m_pfb_views, m_state, m_next_component_id);
```

Replace the bodies of `saveProject`, `loadProject`, `newProject` with:

```cpp
void RfSimulatorApp::saveProject(const std::string &path) {
    m_serializer->save(path);
    m_current_project_path = path;
    m_dirty = false;
}

void RfSimulatorApp::loadProject(const std::string &path) {
    if (!m_serializer->load(path))
        return;
    m_current_project_path = path;
    refreshExtensions();
    m_dirty = false;
}

void RfSimulatorApp::newProject() {
    m_serializer->reset();
    m_spectrum_widget->setProbeLabels({});
    m_current_project_path.clear();
    refreshExtensions();
    m_dirty = false;
}
```

- [ ] **Step 6: Build**

Run: `cmake --build build`
Expected: compiles clean.

- [ ] **Step 7: Run round-trip tests**

Run: `build/bin/test_project_file.exe`, `build/bin/test_component_dispatch.exe`, `build/bin/test_issue37_pfb_input_removal.exe`, `build/bin/tests.exe`.
Expected: all pass (round-trips, groups, positions, probes, counters, PFB lifecycle all preserved).

- [ ] **Step 8: Commit**

```bash
git add app/include/project_serializer.h app/src/project_serializer.cpp app/CMakeLists.txt app/include/app.h app/src/app.cpp
git commit -m "refactor: extract ProjectSerializer from RfSimulatorApp"
```

---

### Task 10: Round-trip + backward-compat tests in the dispatch exe

**Files:**
- Modify: `tests/test_component_dispatch.cpp`

**Interfaces:**
- Consumes: everything from Tasks 1-9.

- [ ] **Step 1: Add all-11-types round-trip + legacy file test**

Append to `tests/test_component_dispatch.cpp`:

```cpp
TEST_CASE_METHOD(ImGuiFixture, "All 11 registry types round-trip through project save/load",
                 "[dispatch]") {
    auto path = "test_dispatch_all_types.rfsim";
    std::remove(path);
    {
        RfSimulatorApp app;
        app.newProject();
        for (const auto &addable : app.testGraphWidget().addableComponents())
            addable.on_add(ImVec2(0, 0));
        REQUIRE(app.componentCount() == 11);
        app.saveProject(path);
    }
    {
        RfSimulatorApp app;
        app.loadProject(path);
        REQUIRE(app.componentCount() == 11);
    }
    std::remove(path);
}

TEST_CASE_METHOD(ImGuiFixture, "Legacy .rfsim type strings still load (backward compat)",
                 "[dispatch]") {
    auto path = "test_dispatch_legacy.rfsim";
    std::ofstream out(path);
    out << R"({
      "version": 1,
      "name": "legacy",
      "components": [
        {"type": "SignalGenerator", "params": {"tones": [{"freq_Hz": 100e6, "power_dBm": -20.0, "phase_deg": 0.0}]}},
        {"type": "Amplifier", "params": {"gain_dB": 10.0, "nf_dB": 2.0}},
        {"type": "ADC", "params": {"sample_rate_Hz": 1e9}},
        {"type": "IdealFilter", "params": {"filter_type": 1}},
        {"type": "PFBChannelizer", "params": {}},
        {"type": "CoaxCable", "params": {}}
      ],
      "links": [],
      "groups": []
    })";
    out.close();
    RfSimulatorApp app;
    app.loadProject(path);
    REQUIRE(app.componentCount() == 6);
    REQUIRE(app.testComponents().byType<PFBChannelizerEngine>().size() == 1);
    std::remove(path);
}
```

Add includes at the top: `#include "pfb_channelizer_engine.h"`, `#include <cstdio>`, `#include <fstream>`.

- [ ] **Step 2: Build + run**

Run: `cmake --build build && build/bin/test_component_dispatch.exe`
Expected: PASS — all 11 types round-trip; legacy type strings (capitalized project names) load; canonical names also accepted by `findByProjectType`.

- [ ] **Step 3: Commit**

```bash
git add tests/test_component_dispatch.cpp
git commit -m "test: all-types round-trip and legacy .rfsim backward compat"
```

---

### Task 11: DOX pass + docs + final verification

**Files:**
- Modify: `app/AGENTS.md`
- Modify: `common/AGENTS.md`
- Modify: `openwiki/testing/guidance.md`

- [ ] **Step 1: Update app/AGENTS.md**

- Ownership: add `PFBViewManager` (owns per-PFB IQ/grid widget lifecycle, replaces the app's four lockstep vectors) and `ProjectSerializer` (owns `.rfsim` save/load/new JSON logic). Update `ComponentTypeRegistry` ownership text: now the single dispatch table for add/menu/duplicate/save/load/inspector drawing, with `create()` factories and `draw_inspector` callbacks.
- Work Guidance: replace "Add new component serialization in both saveProject() and loadProject()" with "Add a new component = one ComponentTypeRegistry row (type, project_type, menu_label, label_prefix, kind, create, draw_inspector) + a NodeKind/symbol entry in node_graph".
- Keep the issue #37 / rewireInputs contract and PFB visibility note (now on `PFBViewManager::iqVisibility`/`gridVisibility`).

- [ ] **Step 2: Update common/AGENTS.md**

- Ownership/Work Guidance: note `IComponentEngine` now requires `type_name()` (pure virtual, canonical lowercase key) — new engines must implement it; update the "New fields on IComponentEngine must keep a default implementation" contract line to exempt `type_name()` (intentionally pure).

- [ ] **Step 3: Update openwiki/testing/guidance.md**

- Add `test_component_dispatch.cpp` to the standalone-executables list (alongside `test_issue37_pfb_input_removal`, `test_extensions`, `test_component_authoring`, `test_signal_domain`).

- [ ] **Step 4: Format + full suite**

Run: `scripts/format.sh` then `scripts/format.sh --check`; `cmake --build build`; `ctest --test-dir build --output-on-failure`.
Expected: zero failures.

- [ ] **Step 5: Commit**

```bash
git add app/AGENTS.md common/AGENTS.md openwiki/testing/guidance.md
git commit -m "docs: update DOX for unified registry, PFBViewManager, ProjectSerializer"
```

---

## Self-Review

**Spec coverage:** every item in the approved spec maps to a task — registration object + `type_name()` (T1-T3), canvas menu + Equalizer fix (T4), inspector (T5), node-kind data-driven (T6), form combo (T7), PFBViewManager (T8), ProjectSerializer (T9), round-trip + backward-compat tests (T10), DOX (T11).

**Placeholders:** none — all new files have full code; all modified bodies show the exact replacement.

**Type consistency:** `type_name()` canonical keys match registry `type` values exactly (`generator`, `amplifier`, `splitter`, `mixer`, `adc`, `pfb`, `coax`, `equalizer`, `filter`, `attenuator`, `combiner`). `project_type` strings match today's `.rfsim` output. `findByProjectType` accepts both. `create()` returns `IComponentEngine*`; `draw_inspector` is `std::function<void(InspectorPanel&, IComponentEngine&)>` — signatures match between T2a, T2b, T3, T5. `kindForLabel` is public (T6) so the test can call it.

**Known risks flagged for the implementer:**
- Task 2a is additive and commits green; Task 2b removes `factory` and is the compile-atomic change with `instantiate` rewire.
- Task 4's red step uses the OLD `onAddEqualizer` API; Step 7 updates the test to the new menu API in the SAME task, so no red commit lands.
- The deserialize key-parity edits (T2b Steps 2-6) are behavior-preserving only if applied exactly — they keep library JSON loading identical after `instantiate()` switches to `create()`+`deserialize()`.
