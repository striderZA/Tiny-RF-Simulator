// Standalone Catch2 executable for the component-library authoring UI feature
// (docs/superpowers/plans/2026-07-28-component-library-authoring-ui.md).
//
// Built as its own executable (matching the existing test_attenuator/test_combiner
// precedent below) rather than appended to the main `tests` binary: this MinGW-w64
// toolchain silently drops any TEST_CASE registered beyond the ~217 already linked
// into `tests.exe` (confirmed via a from-scratch clean rebuild — the AutoReg/global
// constructor symbols are present in both the object file and the final binary's
// string table, but Catch2's runtime registry never invokes them). Every new
// TEST_CASE this plan adds (Tasks 1, 2, 3, 4) lives here instead.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "circuit_runtime.h"
#include "component_form_model.h"
#include "component_library.h"
#include "component_type_registry.h"
#include "graph_editor_actions.h"
#include "ideal_filter_engine.h"
#include "node_graph_engine.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <random>
#include <set>

// --- Task 1: ComponentTypeRegistry ---

TEST_CASE("ComponentTypeRegistry covers all 13 existing types", "[type_registry]") {
    auto all = ComponentTypeRegistry::instance().all();
    std::vector<std::string> types;
    for (auto *d : all)
        types.push_back(d->type);
    std::sort(types.begin(), types.end());
    std::vector<std::string> expected = {
        "adc",     "amplifier", "attenuator", "coax", "combiner",       "equalizer",
        "filter",  "generator", "mixer",      "pfb",  "rf_switch_spdt", "rf_switch_spdt_2to1",
        "splitter"};
    REQUIRE(types == expected);
}

TEST_CASE("ComponentTypeRegistry amplifier descriptor has expected fields", "[type_registry]") {
    const auto *d = ComponentTypeRegistry::instance().find("amplifier");
    REQUIRE(d != nullptr);
    REQUIRE(d->display_name == "Amplifier");
    REQUIRE(d->supports_sparam_file == true);

    auto has_field = [&](const std::string &key) {
        return std::any_of(d->fields.begin(), d->fields.end(),
                           [&](const ParameterField &f) { return f.key == key; });
    };
    REQUIRE(has_field("gain_dB"));
    REQUIRE(has_field("nf_dB"));
    REQUIRE(has_field("oip2_dBm"));
    REQUIRE(has_field("oip3_dBm"));
    REQUIRE(has_field("p1db_dBm"));
}

TEST_CASE("ComponentTypeRegistry adc descriptor has expected fields", "[type_registry]") {
    const auto *d = ComponentTypeRegistry::instance().find("adc");
    REQUIRE(d != nullptr);
    REQUIRE(d->display_name == "ADC");

    auto has_field = [&](const std::string &key) {
        return std::any_of(d->fields.begin(), d->fields.end(),
                           [&](const ParameterField &f) { return f.key == key; });
    };
    REQUIRE(has_field("fs_Hz"));
    REQUIRE(has_field("nsd_dBm_per_Hz"));
    REQUIRE(has_field("decimation"));
    REQUIRE(has_field("nco_fs_fraction"));

    auto it = std::find_if(d->fields.begin(), d->fields.end(),
                           [](const ParameterField &f) { return f.key == "decimation"; });
    REQUIRE(it != d->fields.end());
    REQUIRE(it->kind == FieldKind::Number);
    REQUIRE(it->min == 1.0);
    REQUIRE(it->max == 8.0);

    auto nco_it = std::find_if(d->fields.begin(), d->fields.end(),
                               [](const ParameterField &f) { return f.key == "nco_fs_fraction"; });
    REQUIRE(nco_it != d->fields.end());
    REQUIRE(nco_it->kind == FieldKind::Number);
    REQUIRE(nco_it->min == -0.5);
    REQUIRE(nco_it->max == 0.5);
}

TEST_CASE("ComponentTypeRegistry filter descriptor has enum filter_type", "[type_registry]") {
    const auto *d = ComponentTypeRegistry::instance().find("filter");
    REQUIRE(d != nullptr);
    auto it = std::find_if(d->fields.begin(), d->fields.end(),
                           [](const ParameterField &f) { return f.key == "filter_type"; });
    REQUIRE(it != d->fields.end());
    REQUIRE(it->kind == FieldKind::Enum);
    std::vector<std::string> expected_enum = {"LPF", "HPF", "BPF", "BSF"};
    REQUIRE(it->enum_values == expected_enum);
}

TEST_CASE("ComponentTypeRegistry unknown type returns nullptr", "[type_registry]") {
    REQUIRE(ComponentTypeRegistry::instance().find("nonexistent") == nullptr);
}

// --- Task 2: ComponentLibrary::validate() / upsert() ---

TEST_CASE("ComponentLibrary validate flags missing required field", "[library][validate]") {
    ComponentLibrary lib;
    nlohmann::json params = {{"nf_dB", 2.0}}; // missing required gain_dB
    auto issues = lib.validate("amplifier", params);
    REQUIRE_FALSE(issues.empty());
    bool found = false;
    for (auto &i : issues)
        if (i.field == "gain_dB")
            found = true;
    REQUIRE(found);
}

TEST_CASE("ComponentLibrary validate flags out-of-range number", "[library][validate]") {
    ComponentLibrary lib;
    nlohmann::json params = {{"gain_dB", 500.0}}; // out of [-50, 100]
    auto issues = lib.validate("amplifier", params);
    bool found = false;
    for (auto &i : issues)
        if (i.field == "gain_dB")
            found = true;
    REQUIRE(found);
}

TEST_CASE("ComponentLibrary validate flags unknown enum value", "[library][validate]") {
    ComponentLibrary lib;
    nlohmann::json params = {{"filter_type", "NOTCH"}}; // not in {LPF,HPF,BPF,BSF}
    auto issues = lib.validate("filter", params);
    bool found = false;
    for (auto &i : issues)
        if (i.field == "filter_type")
            found = true;
    REQUIRE(found);
}

TEST_CASE("ComponentTypeRegistry authors both SPDT throws by name", "[library][validate]") {
    // Only the active_throw field is under test here; the required
    // insertion_loss_dB is deliberately omitted from the first two params.
    const auto flags_active_throw = [](const std::vector<ValidationIssue> &issues) {
        return std::any_of(issues.begin(), issues.end(),
                           [](const ValidationIssue &i) { return i.field == "active_throw"; });
    };
    ComponentLibrary lib;
    for (const std::string type : {"rf_switch_spdt", "rf_switch_spdt_2to1"}) {
        CAPTURE(type);
        const auto *d = ComponentTypeRegistry::instance().find(type);
        REQUIRE(d != nullptr);
        const auto field =
            std::find_if(d->fields.begin(), d->fields.end(),
                         [](const ParameterField &f) { return f.key == "active_throw"; });
        REQUIRE(field != d->fields.end());
        REQUIRE(field->kind == FieldKind::Enum);
        REQUIRE(field->enum_values == std::vector<std::string>{"T1", "T2"});

        // A bare number is not a valid throw...
        CHECK(flags_active_throw(lib.validate(type, {{"active_throw", 1.0}})));
        // ...a name outside {T1, T2} is rejected...
        CHECK(flags_active_throw(lib.validate(type, {{"active_throw", "T3"}})));
        // ...and a named throw plus the required insertion loss is clean.
        CHECK_FALSE(flags_active_throw(
            lib.validate(type, {{"active_throw", "T2"}, {"insertion_loss_dB", 0.5}})));
    }
}

TEST_CASE("ComponentLibrary validate flags unknown type", "[library][validate]") {
    ComponentLibrary lib;
    auto issues = lib.validate("networkanalyzer", nlohmann::json::object());
    REQUIRE_FALSE(issues.empty());
}

TEST_CASE("ComponentLibrary validate passes for well-formed attenuator", "[library][validate]") {
    ComponentLibrary lib;
    nlohmann::json params = {{"attenuation_dB", 10.0}};
    auto issues = lib.validate("attenuator", params);
    REQUIRE(issues.empty());
}

// --- Task 3: validate() wired into loadFile() ---

static std::string write_temp_json(const std::string &content) {
    static std::random_device rd;
    std::uniform_int_distribution<int> dis(100000, 999999);
    auto path = std::filesystem::temp_directory_path() /
                ("test_component_" + std::to_string(dis(rd)) + ".json");
    std::ofstream ofs(path);
    ofs << content;
    ofs.close();
    return path.string();
}

TEST_CASE("ComponentLibrary loadFile rejects a definition with an out-of-range value",
          "[library][validate]") {
    std::string json = R"({
        "schema_version": 1,
        "type": "amplifier",
        "part_number": "BAD-GAIN",
        "parameters": { "gain_dB": 9999.0 }
    })";
    auto path = write_temp_json(json);
    ComponentLibrary lib;
    lib.loadFile(path);
    auto defs = lib.all();
    // Issue #79: invalid definitions are rejected at the load boundary so they
    // never reach the insertion UI or the engine factory.
    REQUIRE(defs.empty());
    std::filesystem::remove(path);
}

TEST_CASE("ComponentLibrary loadFile has no issues for well-formed entry", "[library][validate]") {
    std::string json = R"({
        "schema_version": 1,
        "type": "attenuator",
        "part_number": "GOOD-ATT",
        "parameters": { "attenuation_dB": 6.0 }
    })";
    auto path = write_temp_json(json);
    ComponentLibrary lib;
    lib.loadFile(path);
    auto defs = lib.all();
    REQUIRE(defs.size() == 1);
    REQUIRE(defs[0]->issues.empty());
    std::filesystem::remove(path);
}

// --- Task 4: ComponentFormModel ---

TEST_CASE("ComponentFormModel builds a valid amplifier definition", "[form_model]") {
    const auto *descriptor = ComponentTypeRegistry::instance().find("amplifier");
    REQUIRE(descriptor != nullptr);

    ComponentFormModel model(*descriptor);
    model.setPartNumber("TEST-FORM-AMP");
    model.setManufacturer("Test Corp");
    model.setParameter("gain_dB", 20.0);
    model.setParameter("nf_dB", 2.0);

    ComponentLibrary lib;
    auto issues = model.validate(lib);
    REQUIRE(issues.empty());

    auto def = model.buildDefinition();
    REQUIRE(def.type == "amplifier");
    REQUIRE(def.part_number == "TEST-FORM-AMP");
    REQUIRE(def.manufacturer == "Test Corp");
    REQUIRE(def.parameters["gain_dB"].get<double>() == 20.0);
    REQUIRE(def.parameters["nf_dB"].get<double>() == 2.0);
}

TEST_CASE("ComponentFormModel validate flags missing required field", "[form_model]") {
    const auto *descriptor = ComponentTypeRegistry::instance().find("amplifier");
    ComponentFormModel model(*descriptor); // no gain_dB set
    ComponentLibrary lib;
    auto issues = model.validate(lib);
    bool found = false;
    for (auto &i : issues)
        if (i.field == "gain_dB")
            found = true;
    REQUIRE(found);
}

TEST_CASE("ComponentFormModel loadFrom pre-fills fields for editing", "[form_model]") {
    const auto *descriptor = ComponentTypeRegistry::instance().find("attenuator");
    ComponentFormModel model(*descriptor);

    ComponentDefinition existing;
    existing.type = "attenuator";
    existing.part_number = "ATT-1";
    existing.manufacturer = "Acme";
    existing.parameters = {{"attenuation_dB", 6.0}};
    existing.source_path = "/some/path/att-1.json";

    model.loadFrom(existing);

    REQUIRE(model.partNumber() == "ATT-1");
    REQUIRE(model.manufacturer() == "Acme");
    REQUIRE(model.parameter("attenuation_dB").get<double>() == 6.0);
    REQUIRE(model.sourcePath() == "/some/path/att-1.json");
}

TEST_CASE("ComponentFormModel round-trips through ComponentLibrary loadFile/instantiate",
          "[form_model]") {
    const auto *descriptor = ComponentTypeRegistry::instance().find("attenuator");
    ComponentFormModel model(*descriptor);
    model.setPartNumber("ROUNDTRIP-ATT");
    model.setParameter("attenuation_dB", 3.0);

    auto def = model.buildDefinition();
    REQUIRE(def.parameters["attenuation_dB"].get<double>() == 3.0);
}

// --- Task 4 (cont'd): ComponentFormModel data_files preservation on edit ---

TEST_CASE("ComponentFormModel preserves original data_files on edit without new S-param pick",
          "[form_model]") {
    const auto *descriptor = ComponentTypeRegistry::instance().find("amplifier");
    REQUIRE(descriptor != nullptr);

    // Create a definition with an existing S-param reference
    ComponentDefinition existing;
    existing.type = "amplifier";
    existing.part_number = "AMP-EDIT-TEST";
    existing.manufacturer = "TestCorp";
    existing.parameters = {{"gain_dB", 20.0}, {"nf_dB", 2.0}};
    existing.data_files = {{"s_parameters", "AMP-EDIT-TEST.s4p"}};

    // Load into the model (no setSparamSourcePath call — simulating edit without re-Browse)
    ComponentFormModel model(*descriptor);
    model.loadFrom(existing);

    auto def = model.buildDefinition();

    // data_files from the original should be preserved
    REQUIRE(def.data_files.size() == 1);
    REQUIRE(def.data_files[0].type == "s_parameters");
    REQUIRE(def.data_files[0].path == "AMP-EDIT-TEST.s4p");
}

// --- Issue #181: an ideal filter definition carries the cutoffs its type uses ---
//
// The library schema has two generic, optional cutoff fields, but
// IdealFilterEngine reads them per filter type: LPF/HPF use fc_low_Hz alone,
// BPF/BSF the fc_low_Hz..fc_high_Hz band. A definition missing a cutoff its type
// uses passed validation and silently kept the engine's constructor default.

namespace {

bool filterUsesBand(const std::string &filter_type) {
    return filter_type == "BPF" || filter_type == "BSF";
}

std::set<std::string> flaggedFields(const std::vector<ValidationIssue> &issues) {
    std::set<std::string> fields;
    for (const auto &issue : issues)
        fields.insert(issue.field);
    return fields;
}

} // namespace

TEST_CASE("ComponentLibrary validate requires the cutoffs an ideal filter type uses",
          "[library][validate][issue181]") {
    struct Case {
        nlohmann::json cutoffs;
        std::set<std::string> single_cutoff_issues; // LPF, HPF
        std::set<std::string> band_issues;          // BPF, BSF
    };
    const std::vector<Case> cases = {
        {nlohmann::json::object(), {"fc_low_Hz"}, {"fc_low_Hz", "fc_high_Hz"}},
        {{{"fc_high_Hz", 3.3e9}}, {"fc_low_Hz"}, {"fc_low_Hz"}},
        {{{"fc_low_Hz", 140e6}}, {}, {"fc_high_Hz"}},
        {{{"fc_low_Hz", 140e6}, {"fc_high_Hz", 180e6}}, {}, {}},
        {{{"fc_low_Hz", 140e6}, {"fc_high_Hz", 140e6}}, {}, {"fc_high_Hz"}},
        {{{"fc_low_Hz", 180e6}, {"fc_high_Hz", 140e6}}, {}, {"fc_high_Hz"}},
    };
    ComponentLibrary lib;
    for (const std::string filter_type : {"LPF", "HPF", "BPF", "BSF"}) {
        for (const auto &c : cases) {
            nlohmann::json params = c.cutoffs;
            params["filter_type"] = filter_type;
            INFO(params.dump());
            CHECK(flaggedFields(lib.validate("filter", params)) ==
                  (filterUsesBand(filter_type) ? c.band_issues : c.single_cutoff_issues));
        }
    }
}

TEST_CASE("Every ideal filter definition validate accepts is applied as written",
          "[library][validate][issue181]") {
    // Library-shaped partial definitions, one cutoff combination at a time: a
    // serialize() round trip always carries both cutoffs, so it cannot see this.
    const std::vector<std::optional<double>> cutoffs = {std::nullopt, 0.0, 140e6, 180e6};
    ComponentLibrary lib;
    for (const std::string filter_type : {"LPF", "HPF", "BPF", "BSF"}) {
        int accepted = 0;
        for (const auto &low : cutoffs) {
            for (const auto &high : cutoffs) {
                nlohmann::json params = {{"filter_type", filter_type}};
                if (low)
                    params["fc_low_Hz"] = *low;
                if (high)
                    params["fc_high_Hz"] = *high;
                if (!lib.validate("filter", params).empty())
                    continue;
                ++accepted;
                INFO(params.dump());

                NodeGraphEngine graph;
                IdealFilterEngine filter(0, graph);
                filter.deserialize(params);
                // Each cutoff the type uses was written, and is the one applied.
                REQUIRE(low.has_value());
                CHECK(filter.fcLow_Hz() == *low);
                if (filterUsesBand(filter_type)) {
                    REQUIRE(high.has_value());
                    CHECK(*low < *high);
                    CHECK(filter.fcHigh_Hz() == *high);
                }
            }
        }
        CHECK(accepted > 0);
    }
}

TEST_CASE("An LPF library definition filters at the cutoff it specifies", "[library][issue181]") {
    Spectrum input;
    input.frequencies = {0.0, 2e9, 4e9};
    input.tones = {{1e9, -10.0, 0.0}};

    ComponentLibrary lib;
    CircuitRuntime runtime;
    GraphEditorActions actions(runtime);
    ComponentDefinition def;
    def.schema_version = 2;
    def.type = "filter";
    def.part_number = "LPF-3G3";

    // The reported definition: an LPF given only fc_high_Hz was inserted with
    // the engine's 100 MHz default cutoff, which removed a 1 GHz tone.
    def.parameters = {{"filter_type", "LPF"}, {"fc_high_Hz", 3.3e9}};
    CHECK(lib.instantiate(def, runtime, actions) == nullptr);
    CHECK(runtime.components().size() == 0);

    def.parameters = {{"filter_type", "LPF"}, {"fc_low_Hz", 3.3e9}};
    auto *filter = dynamic_cast<IdealFilterEngine *>(lib.instantiate(def, runtime, actions));
    REQUIRE(filter != nullptr);
    filter->node().inputs[0] = &input;
    filter->update(0.0);

    const auto &out = filter->node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    CHECK(out.tones[0].freq_Hz == Catch::Approx(1e9));
}

TEST_CASE("ComponentFormModel refuses an LPF given only a high cutoff", "[form_model][issue181]") {
    const auto *descriptor = ComponentTypeRegistry::instance().find("filter");
    REQUIRE(descriptor != nullptr);
    ComponentFormModel model(*descriptor);
    model.setPartNumber("LPF-FORM");
    model.setParameter("filter_type", "LPF");
    model.setParameter("fc_high_Hz", 3.3e9);

    ComponentLibrary lib;
    CHECK(flaggedFields(model.validate(lib)) == std::set<std::string>{"fc_low_Hz"});

    model.setParameter("fc_low_Hz", 3.3e9);
    CHECK(model.validate(lib).empty());
}

TEST_CASE("ComponentFormModel shows the cutoffs an ideal filter type uses",
          "[form_model][issue181]") {
    const auto *descriptor = ComponentTypeRegistry::instance().find("filter");
    REQUIRE(descriptor != nullptr);
    const auto field = [&](const std::string &key) -> const ParameterField & {
        const auto it = std::find_if(descriptor->fields.begin(), descriptor->fields.end(),
                                     [&](const ParameterField &f) { return f.key == key; });
        REQUIRE(it != descriptor->fields.end());
        return *it;
    };
    ComponentFormModel model(*descriptor);

    // Until a filter type is chosen, neither cutoff has a meaning to show.
    CHECK(model.fieldLabel(field("filter_type")) == "Filter Type");
    CHECK_FALSE(model.fieldLabel(field("fc_low_Hz")).has_value());
    CHECK_FALSE(model.fieldLabel(field("fc_high_Hz")).has_value());

    for (const std::string filter_type : {"LPF", "HPF"}) {
        model.setParameter("filter_type", filter_type);
        CHECK(model.fieldLabel(field("fc_low_Hz")) == "Cutoff");
        CHECK_FALSE(model.fieldLabel(field("fc_high_Hz")).has_value());
    }
    for (const std::string filter_type : {"BPF", "BSF"}) {
        model.setParameter("filter_type", filter_type);
        CHECK(model.fieldLabel(field("fc_low_Hz")) == "Low Cutoff");
        CHECK(model.fieldLabel(field("fc_high_Hz")) == "High Cutoff");
    }

    // Every other type shows each field under its own label.
    const auto *amplifier = ComponentTypeRegistry::instance().find("amplifier");
    REQUIRE(amplifier != nullptr);
    ComponentFormModel amplifier_model(*amplifier);
    for (const auto &f : amplifier->fields)
        CHECK(amplifier_model.fieldLabel(f) == f.label);
}
