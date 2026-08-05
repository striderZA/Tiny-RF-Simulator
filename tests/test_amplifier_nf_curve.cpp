#include "amplifier_engine.h"
#include "node_graph_engine.h"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>

TEST_CASE("Amplifier S-parameter mode uses nf_db_vs_freq per bin", "[amp][sparam][nf_curve]") {
    NodeGraphEngine graph;
    AmplifierEngine amp(100, graph);
    amp.setSParamMode(true);
    amp.setNfCurve(nlohmann::json::array({{1.0e8, 1.0}, {2.0e8, 6.0}, {3.0e8, 12.0}}));

    const std::filesystem::path s2p =
        std::filesystem::temp_directory_path() / "amp_nf_curve_test.s2p";
    {
        std::ofstream out(s2p);
        out << "# Hz S RI R 50\n";
        out << "100000000 0 0 1 0 0 0 0 0\n";
        out << "200000000 0 0 1 0 0 0 0 0\n";
        out << "300000000 0 0 1 0 0 0 0 0\n";
    }
    amp.setSParamFilepath(s2p.string());
    REQUIRE(amp.sparamLoaded());

    Spectrum in;
    in.frequencies = {1.0e8, 2.0e8, 3.0e8};
    in.noise_W = {0.0, 0.0, 0.0};
    in.noise_added_W = {0.0, 0.0, 0.0};
    in.noise_total_W = {0.0, 0.0, 0.0};
    in.bumpGeneration();

    amp.node().inputs[0] = &in;
    amp.update(0.0);

    const auto &out = amp.node().outputs[0];
    REQUIRE(out.noise_added_W.size() == 3);
    REQUIRE(out.noise_added_W[0] < out.noise_added_W[1]);
    REQUIRE(out.noise_added_W[1] < out.noise_added_W[2]);

    std::filesystem::remove(s2p);
}

TEST_CASE("Amplifier serialize/deserialize preserves nf_db_vs_freq", "[amp][sparam][nf_curve]") {
    NodeGraphEngine graph;
    AmplifierEngine original(100, graph);
    original.setNfCurve(nlohmann::json::array({{1.0e8, 1.0}, {2.0e8, 6.0}, {3.0e8, 12.0}}));

    NodeGraphEngine other_graph;
    AmplifierEngine restored(101, other_graph);
    restored.deserialize(original.serialize());

    REQUIRE(restored.hasNfCurve());
}
