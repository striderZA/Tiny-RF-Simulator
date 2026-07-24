#include "ideal_filter_engine.h"
#include "node_graph_engine.h"
#include "signal_generator_engine.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

static std::string s2p_path() {
    return std::string(PROJECT_SOURCE_DIR) + "/component_data/amplifiers/adm-3844psm/"
                                             "ADM-8344PSM_SM_A_25C_De_5V_5V_102mA.s2p";
}

TEST_CASE("IdealFilter ideal mode unchanged after S-param refactor", "[filter][sparam]") {
    NodeGraphEngine graph;
    IdealFilterEngine flt(0, graph);
    flt.setFilterType(FilterType::LPF);
    flt.setCutoff_Hz(200e6);

    SignalGeneratorEngine gen(1, graph);
    gen.addTone(100e6, -10.0); // in passband
    gen.addTone(300e6, -10.0); // in stopband
    gen.update(0.0);

    flt.node().inputs[0] = &gen.node().outputs[0];
    flt.update(0.0);

    const auto &out = flt.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    REQUIRE(out.tones[0].freq_Hz == Approx(100e6));
}

TEST_CASE("IdealFilter S-param mode loads file and applies S21", "[filter][sparam]") {
    NodeGraphEngine graph;
    IdealFilterEngine flt(0, graph);
    flt.setSParamFilepath(s2p_path());

    REQUIRE(flt.sparamLoaded());
    REQUIRE(flt.sparamMode());

    SignalGeneratorEngine gen(1, graph);
    gen.addTone(1e9, -20.0);
    gen.update(0.0);

    flt.node().inputs[0] = &gen.node().outputs[0];
    flt.update(0.0);

    const auto &out = flt.node().outputs[0];
    REQUIRE(out.tones.size() == 1);
    auto S21 = flt.sparamData().interpolate(1e9, 2);
    double expected_gain = 20.0 * std::log10(std::abs(S21));
    REQUIRE(out.tones[0].power_dBm == Approx(-20.0 + expected_gain).margin(0.5));
}
