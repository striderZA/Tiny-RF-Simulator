// Source-vs-added noise field contract for SignalGeneratorEngine.
//
// A Spectrum distinguishes passed-through noise (noise_W: density that entered
// through the component's RF input and was carried to the output) from noise
// the component generates itself (noise_added_W). SignalGeneratorEngine has no
// RF input, so nothing can pass through: its output must report noise_W == 0
// and account its entire k*T thermal floor as self-added noise. noise_total_W
// stays the numeric sum, so the generated spectrum is unchanged.
//
// Standalone executable: adding TEST_CASEs to the main `tests` binary hits the
// MinGW-w64 registration ceiling (see tests/AGENTS.md).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "common.h"
#include "node_graph_engine.h"
#include "signal_generator_engine.h"

using Catch::Approx;

TEST_CASE("Generator noise contract: k*T floor is added noise, noise_W is zero",
          "[generator][noise]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.update(0.0);

    const auto &out = gen.node().outputs[0];
    REQUIRE(out.noise_W.size() == out.frequencies.size());
    REQUIRE(out.noise_added_W.size() == out.frequencies.size());
    REQUIRE(out.noise_total_W.size() == out.frequencies.size());
    for (size_t i = 0; i < out.frequencies.size(); ++i) {
        REQUIRE(out.noise_W[i] == 0.0);
        REQUIRE(out.noise_added_W[i] == Approx(k * T).epsilon(1e-30));
        // Total arithmetic is unaffected: 0 (passed through) + k*T (added).
        REQUIRE(out.noise_total_W[i] == Approx(k * T).epsilon(1e-30));
    }
}

TEST_CASE("Generator noise contract survives tone edits and clean re-updates",
          "[generator][noise]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(0, graph);
    gen.update(0.0);

    gen.addTone(100e6, -20.0);
    gen.update(0.0);
    gen.update(0.0); // clean re-update must not disturb the noise fields

    const auto &out = gen.node().outputs[0];
    REQUIRE(!out.tones.empty());
    for (size_t i = 0; i < out.frequencies.size(); ++i) {
        REQUIRE(out.noise_W[i] == 0.0);
        REQUIRE(out.noise_added_W[i] == Approx(k * T).epsilon(1e-30));
        REQUIRE(out.noise_total_W[i] == Approx(k * T).epsilon(1e-30));
    }
}
