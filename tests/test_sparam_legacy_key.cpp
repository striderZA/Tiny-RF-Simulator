// Legacy `sparam_path` key for the amplifier, equalizer, and ideal filter.
// Projects and libraries saved before the `sparam_filepath` rename carry only
// `sparam_path`. The loaders resolve that key in place, so each engine's
// deserializer must read it as a fallback, the way the attenuator and combiner
// already do. Standalone rather than part of the main `tests` binary, because
// the MinGW-w64 toolchain silently drops TEST_CASEs past its registration
// ceiling (see tests/AGENTS.md).
#include "amplifier_engine.h"
#include "equalizer_engine.h"
#include "ideal_filter_engine.h"
#include "node_graph_engine.h"
#include "signal_generator_engine.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <nlohmann/json.hpp>
#include <string>

using Catch::Approx;

namespace {

std::string s2p_path() {
    return std::string(PROJECT_SOURCE_DIR) + "/component_data/amplifiers/adm-3844psm/"
                                             "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
}

// The three engines share one deserialize/serialize surface, so each check is
// written once and instantiated per engine.
template <typename Engine> void requireLegacyKeyLoads() {
    NodeGraphEngine graph;
    Engine engine(0, graph);
    engine.deserialize(nlohmann::json{{"sparam_mode", true}, {"sparam_path", s2p_path()}});
    REQUIRE(engine.sparamLoaded());
    REQUIRE(engine.sparamMode());
    // The next save writes the canonical key.
    REQUIRE(engine.serialize()["sparam_filepath"] == s2p_path());
}

// A legacy-only load selects S21 (index 2 of the 2-port matrix), as a fresh
// setSParamFilepath() does, rather than the S11 index a missing key used to give.
template <typename Engine> void requireLegacyKeyAppliesS21() {
    NodeGraphEngine graph;
    Engine engine(0, graph);
    engine.deserialize(nlohmann::json{{"sparam_mode", true}, {"sparam_path", s2p_path()}});
    REQUIRE(engine.sparamLoaded()); // the expected gain below reads the loaded file's S21

    SignalGeneratorEngine gen(1, graph);
    gen.addTone(1e9, -20.0);
    gen.update(0.0);

    engine.node().inputs[0] = &gen.node().outputs[0];
    engine.update(0.0);

    const auto &out = engine.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    const auto S21 = engine.sparamData().interpolate(1e9, 2);
    const double expected_dBm = -20.0 + 20.0 * std::log10(std::abs(S21));
    REQUIRE(out.tones[0].power_dBm == Approx(expected_dBm).margin(0.5));
}

// An explicit forward index is kept. Index 0 is S11, so the output follows S11,
// not the S21 default that applies when the key is absent.
template <typename Engine> void requireExplicitFwdIdxKept() {
    NodeGraphEngine graph;
    Engine engine(0, graph);
    engine.deserialize(
        nlohmann::json{{"sparam_mode", true}, {"sparam_path", s2p_path()}, {"sparam_fwd_idx", 0}});
    REQUIRE(engine.sparamLoaded());
    REQUIRE(engine.serialize()["sparam_fwd_idx"] == 0);

    SignalGeneratorEngine gen(1, graph);
    gen.addTone(1e9, -20.0);
    gen.update(0.0);

    engine.node().inputs[0] = &gen.node().outputs[0];
    engine.update(0.0);

    const auto &out = engine.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    const auto S11 = engine.sparamData().interpolate(1e9, 0);
    const double expected_dBm = -20.0 + 20.0 * std::log10(std::abs(S11));
    REQUIRE(out.tones[0].power_dBm == Approx(expected_dBm).margin(0.5));
}

template <typename Engine> void requireCanonicalKeyWins() {
    NodeGraphEngine graph;
    Engine engine(0, graph);
    engine.deserialize(nlohmann::json{{"sparam_mode", true},
                                      {"sparam_filepath", s2p_path()},
                                      {"sparam_path", "/nonexistent/legacy.s2p"}});
    REQUIRE(engine.sparamLoaded());
    REQUIRE(engine.serialize()["sparam_filepath"] == s2p_path());
}

template <typename Engine> void requireNoKeyStaysManual() {
    NodeGraphEngine graph;
    Engine engine(0, graph);
    engine.deserialize(nlohmann::json{{"sparam_mode", true}});
    REQUIRE_FALSE(engine.sparamLoaded());
    REQUIRE_FALSE(engine.sparamMode());
}

} // namespace

TEST_CASE("Legacy sparam_path alone loads the S-parameter file", "[sparam][legacy]") {
    SECTION("amplifier") { requireLegacyKeyLoads<AmplifierEngine>(); }
    SECTION("equalizer") { requireLegacyKeyLoads<EqualizerEngine>(); }
    SECTION("ideal filter") { requireLegacyKeyLoads<IdealFilterEngine>(); }
}

TEST_CASE("Legacy sparam_path alone applies the file's S21 gain", "[sparam][legacy]") {
    SECTION("amplifier") { requireLegacyKeyAppliesS21<AmplifierEngine>(); }
    SECTION("equalizer") { requireLegacyKeyAppliesS21<EqualizerEngine>(); }
    SECTION("ideal filter") { requireLegacyKeyAppliesS21<IdealFilterEngine>(); }
}

TEST_CASE("An explicit sparam_fwd_idx overrides the S21 default", "[sparam][legacy]") {
    SECTION("amplifier") { requireExplicitFwdIdxKept<AmplifierEngine>(); }
    SECTION("equalizer") { requireExplicitFwdIdxKept<EqualizerEngine>(); }
    SECTION("ideal filter") { requireExplicitFwdIdxKept<IdealFilterEngine>(); }
}

TEST_CASE("sparam_filepath takes precedence over a legacy sparam_path", "[sparam][legacy]") {
    SECTION("amplifier") { requireCanonicalKeyWins<AmplifierEngine>(); }
    SECTION("equalizer") { requireCanonicalKeyWins<EqualizerEngine>(); }
    SECTION("ideal filter") { requireCanonicalKeyWins<IdealFilterEngine>(); }
}

TEST_CASE("No S-parameter key leaves the engine on its manual model", "[sparam][legacy]") {
    SECTION("amplifier") { requireNoKeyStaysManual<AmplifierEngine>(); }
    SECTION("equalizer") { requireNoKeyStaysManual<EqualizerEngine>(); }
    SECTION("ideal filter") { requireNoKeyStaysManual<IdealFilterEngine>(); }
}
