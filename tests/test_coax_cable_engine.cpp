#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "coax_cable_engine.h"
#include "signal_generator_engine.h"
#include "node_graph_engine.h"

using Catch::Approx;

namespace {
// Drive a single tone through the cable; return the output tone power.
double runOneTone(double freq_Hz, double power_dBm, double length_m,
                  int preset_index, double conn_loss_dB = 0.0) {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.addTone(freq_Hz, power_dBm);
    gen.update(0.0);

    CoaxCableEngine cable(0, graph);
    cable.setPresetIndex(preset_index);
    cable.setLengthM(length_m);
    cable.setConnectorsLossDB(conn_loss_dB);
    cable.node().inputs[0] = &gen.node().outputs[0];
    cable.update(0.0);

    if (cable.node().outputs[0].tones.empty()) return -1e9;
    return cable.node().outputs[0].tones[0].power_dBm;
}
} // namespace

// Note: the K1/K2 formula is a best-fit model. The table values in the
// datasheet are measured loss and diverge from the formula at higher
// frequencies (e.g. 6 GHz table=39.6, formula=38.88). We verify the
// formula output here, not the table.
TEST_CASE("Coax cable loss matches K1/K2 formula at 100 m", "[coax][datasheet]") {
    const int MT340 = 4;
    const double L = 100.0;

    REQUIRE(runOneTone(500e6,  0.0, L, MT340) == Approx(-10.73).margin(0.1));
    REQUIRE(runOneTone(2e9,    0.0, L, MT340) == Approx(-21.86).margin(0.1));
    REQUIRE(runOneTone(6e9,    0.0, L, MT340) == Approx(-38.88).margin(0.1));
    REQUIRE(runOneTone(10e9,   0.0, L, MT340) == Approx(-51.10).margin(0.1));
    REQUIRE(runOneTone(18e9,   0.0, L, MT340) == Approx(-70.39).margin(0.1));
}
