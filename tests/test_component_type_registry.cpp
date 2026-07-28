// tests/test_component_type_registry.cpp
#include "component_type_registry.h"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>

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
