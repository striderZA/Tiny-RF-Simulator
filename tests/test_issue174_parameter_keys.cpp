#include "component_library.h"
#include "component_registry.h"
#include "component_type_registry.h"
#include "node_graph_engine.h"
#include "view_manager.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

namespace {
using Json = nlohmann::json;

struct FieldSerializationCase {
    std::string type;
    std::string library_key;
    std::string serialized_key;
    Json library_value;
    Json serialized_value;
};

const std::vector<FieldSerializationCase> &fieldSerializationCases() {
    static const std::vector<FieldSerializationCase> cases = {
        {"amplifier", "gain_dB", "gain_dB", 17.5, 17.5},
        {"amplifier", "nf_dB", "nf_dB", 4.25, 4.25},
        {"amplifier", "oip2_dBm", "oip2_dBm", 68.5, 68.5},
        {"amplifier", "oip3_dBm", "oip3_dBm", 51.25, 51.25},
        {"amplifier", "p1db_dBm", "p1db_dBm", 31.75, 31.75},
        {"attenuator", "attenuation_dB", "atten_dB", 7.5, 7.5},
        {"filter", "filter_type", "filter_type", "BSF", 3},
        {"filter", "fc_low_Hz", "fc_low_Hz", 1.25e9, 1.25e9},
        {"filter", "fc_high_Hz", "fc_high_Hz", 2.75e9, 2.75e9},
        {"mixer", "lo_freq_Hz", "lo_freq_Hz", 2.5e9, 2.5e9},
        {"mixer", "conversion_gain_dB", "conv_gain_dB", -3.25, -3.25},
        {"mixer", "nf_dB", "nf_dB", 5.5, 5.5},
        {"equalizer", "ref_gain_dB", "ref_gain_dB", 4.5, 4.5},
        {"equalizer", "ref_freq_Hz", "ref_freq_Hz", 2.3e9, 2.3e9},
        {"equalizer", "slope_dB_per_decade", "slope_dB_per_decade", -6.25, -6.25},
        {"combiner", "manual_mode", "manual_mode", true, true},
        {"rf_switch_spdt", "active_throw", "active_throw", "T2", 1},
        {"rf_switch_spdt", "insertion_loss_dB", "insertion_loss_dB", 1.25, 1.25},
        {"rf_switch_spdt", "isolation_dB", "isolation_dB", 57.0, 57.0},
        {"rf_switch_spdt_2to1", "active_throw", "active_throw", "T2", 1},
        {"rf_switch_spdt_2to1", "insertion_loss_dB", "insertion_loss_dB", 1.75, 1.75},
        {"rf_switch_spdt_2to1", "isolation_dB", "isolation_dB", 63.0, 63.0},
        {"adc", "fs_Hz", "sample_rate_Hz", 2.5e9, 2.5e9},
        {"adc", "nsd_dBm_per_Hz", "nsd_dBm_per_Hz", -132.5, -132.5},
        {"adc", "decimation", "decimation", 4, 4},
        {"adc", "nco_fs_fraction", "nco_fs_fraction", 0.125, 0.125},
    };
    return cases;
}

Json validLibraryParameters(const std::string &type) {
    if (type == "amplifier")
        return {{"gain_dB", 12.0},
                {"nf_dB", 2.5},
                {"oip2_dBm", 60.0},
                {"oip3_dBm", 45.0},
                {"p1db_dBm", 25.0}};
    if (type == "attenuator")
        return {{"attenuation_dB", 10.0}};
    if (type == "filter")
        return {{"filter_type", "BSF"}, {"fc_low_Hz", 1.0e9}, {"fc_high_Hz", 2.0e9}};
    if (type == "mixer")
        return {{"lo_freq_Hz", 1.0e9}, {"conversion_gain_dB", -6.0}, {"nf_dB", 5.0}};
    if (type == "equalizer")
        return {{"ref_gain_dB", 1.0}, {"ref_freq_Hz", 1.0e9}, {"slope_dB_per_decade", 3.0}};
    if (type == "combiner")
        return {{"manual_mode", false}};
    if (type == "rf_switch_spdt" || type == "rf_switch_spdt_2to1")
        return {{"active_throw", "T1"}, {"insertion_loss_dB", 0.5}, {"isolation_dB", 40.0}};
    if (type == "adc")
        return {{"fs_Hz", 1.0e9},
                {"nsd_dBm_per_Hz", -150.0},
                {"decimation", 2},
                {"nco_fs_fraction", 0.25}};
    return Json::object();
}
} // namespace

TEST_CASE("Authorable registry fields deserialize to their persisted engine keys", "[issue174]") {
    auto descriptors = ComponentTypeRegistry::instance().all();
    std::map<std::string, std::set<std::string>> expected_fields;
    for (const auto &mapping : fieldSerializationCases())
        expected_fields[mapping.type].insert(mapping.library_key);

    for (const auto *descriptor : descriptors) {
        if (!descriptor->authorable)
            continue;
        std::set<std::string> actual_fields;
        for (const auto &field : descriptor->fields)
            actual_fields.insert(field.key);
        CAPTURE(descriptor->type);
        CHECK(actual_fields == expected_fields[descriptor->type]);
    }

    ComponentLibrary library;
    for (const auto &mapping : fieldSerializationCases()) {
        CAPTURE(mapping.type, mapping.library_key, mapping.serialized_key);
        const auto *descriptor = ComponentTypeRegistry::instance().find(mapping.type);
        REQUIRE(descriptor != nullptr);
        REQUIRE(descriptor->authorable);
        REQUIRE(descriptor->create != nullptr);

        const auto field = std::find_if(
            descriptor->fields.begin(), descriptor->fields.end(),
            [&](const ParameterField &candidate) { return candidate.key == mapping.library_key; });
        REQUIRE(field != descriptor->fields.end());

        Json parameters = validLibraryParameters(mapping.type);
        parameters[mapping.library_key] = mapping.library_value;
        REQUIRE(library.validate(mapping.type, parameters).empty());

        NodeGraphEngine graph;
        ViewManager view;
        ComponentRegistry components(graph, view);
        auto *engine = descriptor->create(components, graph, 1);
        REQUIRE(engine != nullptr);
        engine->deserialize(parameters);

        const Json serialized = engine->serialize();
        REQUIRE(serialized.contains(mapping.serialized_key));
        CHECK(serialized.at(mapping.serialized_key) == mapping.serialized_value);
        engine->deserialize(serialized);
        CHECK(engine->serialize() == serialized);
    }
}

TEST_CASE("Every registered component engine has a serialize-deserialize fixed point",
          "[issue174]") {
    const auto descriptors = ComponentTypeRegistry::instance().all();
    REQUIRE_FALSE(descriptors.empty());

    int id = 1;
    for (const auto *descriptor : descriptors) {
        CAPTURE(descriptor->type);
        REQUIRE(descriptor->create != nullptr);

        NodeGraphEngine graph;
        ViewManager view;
        ComponentRegistry components(graph, view);
        auto *engine = descriptor->create(components, graph, id++);
        REQUIRE(engine != nullptr);

        const Json serialized = engine->serialize();
        engine->deserialize(serialized);
        CHECK(engine->serialize() == serialized);
    }
}
