#include <catch2/catch_test_macros.hpp>

#include "component_library.h"
#include <nlohmann/json.hpp>

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
