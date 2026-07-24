#include "nonlinear_model.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

TEST_CASE("NonlinearModel P1dB defaults to 100 dBm", "[nonlinear][p1db]") {
    NonlinearModel model;
    REQUIRE(model.p1db_dBm() == Approx(100.0));
}

TEST_CASE("NonlinearModel P1dB setter updates value", "[nonlinear][p1db]") {
    NonlinearModel model;
    model.setP1dB_dBm(20.0);
    REQUIRE(model.p1db_dBm() == Approx(20.0));
}

TEST_CASE("NonlinearModel derives OIP3 from P1dB when OIP3 is default", "[nonlinear][p1db]") {
    NonlinearModel model;
    REQUIRE(model.oip3_dBm() == Approx(100.0)); // default

    model.setP1dB_dBm(20.0);
    REQUIRE(model.oip3_dBm() == Approx(29.6).margin(0.01)); // 20 + 9.6
}

TEST_CASE("NonlinearModel does not override explicit OIP3 when setting P1dB", "[nonlinear][p1db]") {
    NonlinearModel model;
    model.setOIP3_dBm(35.0);
    model.setP1dB_dBm(20.0);
    REQUIRE(model.oip3_dBm() == Approx(35.0)); // unchanged
    REQUIRE(model.p1db_dBm() == Approx(20.0));
}
