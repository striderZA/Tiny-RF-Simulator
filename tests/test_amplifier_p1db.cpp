#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "amplifier_engine.h"
#include "node_graph_engine.h"
#include <nlohmann/json.hpp>

using Catch::Approx;

TEST_CASE("AmplifierEngine P1dB defaults to 100 dBm", "[amplifier][p1db]") {
    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    REQUIRE(amp.p1db_dBm() == Approx(100.0));
}

TEST_CASE("AmplifierEngine P1dB setter updates value", "[amplifier][p1db]") {
    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setP1dB_dBm(20.0);
    REQUIRE(amp.p1db_dBm() == Approx(20.0));
}

TEST_CASE("AmplifierEngine serializes P1dB", "[amplifier][p1db]") {
    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    amp.setP1dB_dBm(25.0);
    
    auto j = amp.serialize();
    REQUIRE(j.contains("p1db_dBm"));
    REQUIRE(j["p1db_dBm"].get<double>() == Approx(25.0));
}

TEST_CASE("AmplifierEngine deserializes P1dB", "[amplifier][p1db]") {
    NodeGraphEngine graph;
    AmplifierEngine amp(0, graph);
    
    nlohmann::json j = {{"p1db_dBm", 22.0}};
    amp.deserialize(j);
    REQUIRE(amp.p1db_dBm() == Approx(22.0));
}
