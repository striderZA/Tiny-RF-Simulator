#include "amplifier_digitizer_model.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

TEST_CASE("AmplifierDigitizerModel maps calibrated linear pixels to points",
          "[amp_digitizer_model]") {
    AmplifierDigitizerModel model;
    model.setAxisMode(DigitizerCurveKind::Gain, false);
    REQUIRE(model.setCalibration(DigitizerCurveKind::Gain, {{100.0, 1.0e8}, {300.0, 3.0e8}},
                                 {{400.0, 10.0}, {200.0, 30.0}}));

    REQUIRE(model.addPoint(DigitizerCurveKind::Gain, 200.0, 300.0));
    const auto curve = model.curve(DigitizerCurveKind::Gain);
    REQUIRE(curve.size() == 1);
    REQUIRE(curve[0].first == Approx(2.0e8));
    REQUIRE(curve[0].second == Approx(20.0));
}

TEST_CASE("AmplifierDigitizerModel keeps points sorted by frequency", "[amp_digitizer_model]") {
    AmplifierDigitizerModel model;
    model.setAxisMode(DigitizerCurveKind::NoiseFigure, false);
    REQUIRE(model.setCalibration(DigitizerCurveKind::NoiseFigure, {{0.0, 1.0e8}, {100.0, 2.0e8}},
                                 {{100.0, 0.0}, {0.0, 10.0}}));

    REQUIRE(model.addPoint(DigitizerCurveKind::NoiseFigure, 80.0, 50.0));
    REQUIRE(model.addPoint(DigitizerCurveKind::NoiseFigure, 20.0, 50.0));

    const auto curve = model.curve(DigitizerCurveKind::NoiseFigure);
    REQUIRE(curve.size() == 2);
    REQUIRE(curve[0].first < curve[1].first);
}
