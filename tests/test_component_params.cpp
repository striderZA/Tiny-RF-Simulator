#include "coax_presets.h"
#include "component_registry.h"
#include "component_type_registry.h"
#include "flow_params.h"
#include "node_graph_engine.h"
#include "view_manager.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace {
using ExpectedField = std::pair<FieldKind, std::string>;
using ExpectedTypeFields = std::map<std::string, ExpectedField>;

const std::map<std::string, ExpectedTypeFields> &expectedStateFields() {
    static const std::map<std::string, ExpectedTypeFields> fields = {
        {"amplifier",
         {{"gain_dB", {FieldKind::Number, "dB"}},
          {"nf_dB", {FieldKind::Number, "dB"}},
          {"enable_nonlinear", {FieldKind::Bool, ""}},
          {"oip2_dBm", {FieldKind::Number, "dBm"}},
          {"oip3_dBm", {FieldKind::Number, "dBm"}},
          {"p1db_dBm", {FieldKind::Number, "dBm"}},
          {"sparam_mode", {FieldKind::Bool, ""}},
          {"sparam_filepath", {FieldKind::FilePath, ""}},
          {"sparam_fwd_idx", {FieldKind::Number, ""}}}},
        {"attenuator",
         {{"atten_dB", {FieldKind::Number, "dB"}},
          {"sparam_mode", {FieldKind::Bool, ""}},
          {"sparam_filepath", {FieldKind::FilePath, ""}}}},
        {"splitter", {}},
        {"filter",
         {{"filter_type", {FieldKind::Enum, ""}},
          {"fc_low_Hz", {FieldKind::Number, "Hz"}},
          {"fc_high_Hz", {FieldKind::Number, "Hz"}},
          {"sparam_mode", {FieldKind::Bool, ""}},
          {"sparam_filepath", {FieldKind::FilePath, ""}},
          {"sparam_fwd_idx", {FieldKind::Number, ""}}}},
        {"mixer",
         {{"lo_freq_Hz", {FieldKind::Number, "Hz"}},
          {"conv_gain_dB", {FieldKind::Number, "dB"}},
          {"nf_dB", {FieldKind::Number, "dB"}}}},
        {"equalizer",
         {{"ref_gain_dB", {FieldKind::Number, "dB"}},
          {"ref_freq_Hz", {FieldKind::Number, "Hz"}},
          {"slope_dB_per_decade", {FieldKind::Number, "dB/decade"}},
          {"sparam_mode", {FieldKind::Bool, ""}},
          {"sparam_filepath", {FieldKind::FilePath, ""}},
          {"sparam_fwd_idx", {FieldKind::Number, ""}}}},
        {"combiner",
         {{"manual_mode", {FieldKind::Bool, ""}},
          {"sparam_mode", {FieldKind::Bool, ""}},
          {"sparam_filepath", {FieldKind::FilePath, ""}}}},
        {"rf_switch_spdt",
         {{"active_throw", {FieldKind::Enum, ""}},
          {"insertion_loss_dB", {FieldKind::Number, "dB"}},
          {"isolation_dB", {FieldKind::Number, "dB"}}}},
        {"rf_switch_spdt_2to1",
         {{"active_throw", {FieldKind::Enum, ""}},
          {"insertion_loss_dB", {FieldKind::Number, "dB"}},
          {"isolation_dB", {FieldKind::Number, "dB"}}}},
        {"adc",
         {{"sample_rate_Hz", {FieldKind::Number, "Hz"}},
          {"nsd_dBm_per_Hz", {FieldKind::Number, "dBm/Hz"}},
          {"decimation", {FieldKind::Number, ""}},
          {"nco_fs_fraction", {FieldKind::Number, "×Fs"}}}},
        {"generator",
         {{"tones[].freq_Hz", {FieldKind::Number, "Hz"}},
          {"tones[].power_dBm", {FieldKind::Number, "dBm"}},
          {"tones[].phase_deg", {FieldKind::Number, "deg"}},
          {"fs_Hz", {FieldKind::Number, "Hz"}}}},
        {"coax",
         {{"preset_index", {FieldKind::Enum, ""}},
          {"length_m", {FieldKind::Number, "m"}},
          {"connectors_loss_dB", {FieldKind::Number, "dB"}}}},
        {"pfb",
         {{"channel_count", {FieldKind::Number, ""}},
          {"taps_per_branch", {FieldKind::Number, ""}},
          {"kaiser_beta", {FieldKind::Number, ""}},
          {"sampling_ratio", {FieldKind::Number, ""}},
          {"active_channel", {FieldKind::Number, ""}}}},
    };
    return fields;
}

std::set<std::string> keys(const ExpectedTypeFields &fields) {
    std::set<std::string> result;
    for (const auto &[key, _] : fields)
        result.insert(key);
    return result;
}

void checkEnumIndex(const ParameterField *field, const nlohmann::json &snapshot,
                    const std::string &expected_label) {
    REQUIRE(field != nullptr);
    REQUIRE(field->kind == FieldKind::Enum);
    REQUIRE(snapshot.contains(field->key));
    const int stored_index = snapshot.at(field->key).get<int>();
    REQUIRE(stored_index >= 0);
    REQUIRE(static_cast<size_t>(stored_index) < field->enum_values.size());
    CHECK(field->enum_values[static_cast<size_t>(stored_index)] == expected_label);
}
} // namespace

TEST_CASE("State metadata describes every scalar engine serialization leaf", "[component_params]") {
    const auto descriptors = ComponentTypeRegistry::instance().all();
    REQUIRE(descriptors.size() == expectedStateFields().size());

    std::set<std::string> actual_types;
    for (const auto *descriptor : descriptors)
        actual_types.insert(descriptor->type);
    std::set<std::string> expected_types;
    for (const auto &[type, _] : expectedStateFields())
        expected_types.insert(type);
    CHECK(actual_types == expected_types);

    int id = 1;
    for (const auto *descriptor : descriptors) {
        CAPTURE(descriptor->type);
        REQUIRE(descriptor->create != nullptr);
        const auto expected_type = expectedStateFields().find(descriptor->type);
        REQUIRE(expected_type != expectedStateFields().end());

        NodeGraphEngine graph;
        ViewManager view;
        ComponentRegistry components(graph, view);
        auto *engine = descriptor->create(components, graph, id++);
        REQUIRE(engine != nullptr);
        if (descriptor->type == "generator") {
            engine->deserialize(
                {{"tones", {{{"freq_Hz", 1e8}, {"power_dBm", -20.0}, {"phase_deg", 0.0}}}}});
        }

        const nlohmann::json snapshot = engine->serialize();
        if (descriptor->type == "generator")
            REQUIRE(snapshot.at("tones").size() == 1);

        std::set<std::string> serialized_keys;
        for (const auto &path : describeConditionPaths(snapshot)) {
            const std::string key = normalizeStatePath(path.path);
            serialized_keys.insert(key);
            const ParameterField *field = findStateField(*descriptor, path.path);
            REQUIRE(field != nullptr);
            CHECK(field->key == key);
        }

        std::set<std::string> metadata_keys;
        for (const auto &field : descriptor->state_fields) {
            CAPTURE(field.key);
            metadata_keys.insert(field.key);
            CHECK(field.kind == expected_type->second.at(field.key).first);
            CHECK(field.unit == expected_type->second.at(field.key).second);
            CHECK_FALSE(field.help.empty());
            CHECK(field.help.back() == '.');
            CHECK((std::isinf(field.min) && field.min < 0.0));
            CHECK((std::isinf(field.max) && field.max > 0.0));
            CHECK(field.read_only == (field.key == "sparam_filepath"));
            CHECK((field.kind == FieldKind::FilePath) == (field.key == "sparam_filepath"));
        }

        CHECK(metadata_keys.size() == descriptor->state_fields.size());
        CHECK(serialized_keys == metadata_keys);
        CHECK(metadata_keys == keys(expected_type->second));
        CHECK(std::includes(metadata_keys.begin(), metadata_keys.end(), serialized_keys.begin(),
                            serialized_keys.end()));
        CHECK(std::includes(serialized_keys.begin(), serialized_keys.end(), metadata_keys.begin(),
                            metadata_keys.end()));
    }
}

TEST_CASE("State metadata enum labels index the engine's stored integers", "[component_params]") {
    CHECK(normalizeStatePath("tones[12].freq_Hz") == "tones[].freq_Hz");

    const auto *filter = ComponentTypeRegistry::instance().find("filter");
    REQUIRE(filter != nullptr);
    const ParameterField *filter_type = findStateField(*filter, "filter_type");
    REQUIRE(filter_type != nullptr);
    CHECK(filter_type->enum_values == std::vector<std::string>{"LPF", "HPF", "BPF", "BSF"});

    const auto *spdt = ComponentTypeRegistry::instance().find("rf_switch_spdt");
    REQUIRE(spdt != nullptr);
    const ParameterField *spdt_throw = findStateField(*spdt, "active_throw");
    REQUIRE(spdt_throw != nullptr);
    CHECK(spdt_throw->enum_values == std::vector<std::string>{"T1", "T2"});

    const auto *spdt_2to1 = ComponentTypeRegistry::instance().find("rf_switch_spdt_2to1");
    REQUIRE(spdt_2to1 != nullptr);
    const ParameterField *spdt_2to1_throw = findStateField(*spdt_2to1, "active_throw");
    REQUIRE(spdt_2to1_throw != nullptr);
    CHECK(spdt_2to1_throw->enum_values == std::vector<std::string>{"T1", "T2"});

    const auto *coax = ComponentTypeRegistry::instance().find("coax");
    REQUIRE(coax != nullptr);
    const ParameterField *preset_index = findStateField(*coax, "preset_index");
    REQUIRE(preset_index != nullptr);
    REQUIRE(preset_index->enum_values.size() == kCoaxCablePresets.size());
    for (size_t index = 0; index < kCoaxCablePresets.size(); ++index)
        CHECK(preset_index->enum_values[index] == kCoaxCablePresets[index].name);

    int id = 1;
    for (const auto &[descriptor, field, expected_label] : std::vector<
             std::tuple<const ComponentTypeDescriptor *, const ParameterField *, std::string>>{
             {filter, filter_type, "LPF"},
             {spdt, spdt_throw, "T1"},
             {spdt_2to1, spdt_2to1_throw, "T1"}}) {
        CAPTURE(descriptor->type);
        NodeGraphEngine graph;
        ViewManager view;
        ComponentRegistry components(graph, view);
        auto *engine = descriptor->create(components, graph, id++);
        REQUIRE(engine != nullptr);
        checkEnumIndex(field, engine->serialize(), expected_label);
    }

    NodeGraphEngine graph;
    ViewManager view;
    ComponentRegistry components(graph, view);
    auto *coax_engine = coax->create(components, graph, id);
    REQUIRE(coax_engine != nullptr);
    const nlohmann::json coax_snapshot = coax_engine->serialize();
    const int stored_preset = coax_snapshot.at("preset_index").get<int>();
    REQUIRE(stored_preset >= 0);
    REQUIRE(static_cast<size_t>(stored_preset) < kCoaxCablePresets.size());
    checkEnumIndex(preset_index, coax_snapshot, kCoaxCablePresets[stored_preset].name);
}
