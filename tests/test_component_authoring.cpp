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

#include <catch2/catch_test_macros.hpp>

#include "component_library.h"
#include "component_type_registry.h"
#include <algorithm>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <random>

// --- Task 1: ComponentTypeRegistry ---

TEST_CASE("ComponentTypeRegistry covers all 8 existing types", "[type_registry]") {
    auto all = ComponentTypeRegistry::instance().all();
    std::vector<std::string> types;
    for (auto *d : all)
        types.push_back(d->type);
    std::sort(types.begin(), types.end());
    std::vector<std::string> expected = {"adc",       "amplifier", "attenuator", "combiner",
                                         "equalizer", "filter",    "mixer",      "splitter"};
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

TEST_CASE("ComponentLibrary loadFile attaches validation issues for out-of-range value",
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
    REQUIRE(defs.size() == 1);
    REQUIRE_FALSE(defs[0]->issues.empty());
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
