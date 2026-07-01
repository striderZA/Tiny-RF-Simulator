#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "common.h"
#include "equalizer_engine.h"
#include "signal_generator_engine.h"
#include "node_graph_engine.h"

using Catch::Approx;

TEST_CASE("Equalizer identity passes tones unchanged", "[equalizer]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(100e6, -20.0);
    gen.addTone(500e6, -35.0);
    gen.update(0.0);

    EqualizerEngine eq(0, graph);
    eq.node().inputs[0] = &gen.node().outputs[0];
    eq.update(0.0);

    const auto& out = eq.node().outputs[0];
    REQUIRE(out.tones.size() == 2);
    REQUIRE(out.tones[0].freq_Hz == 100e6);
    REQUIRE(out.tones[0].power_dBm == Approx(-20.0));
    REQUIRE(out.tones[1].freq_Hz == 500e6);
    REQUIRE(out.tones[1].power_dBm == Approx(-35.0));
}
