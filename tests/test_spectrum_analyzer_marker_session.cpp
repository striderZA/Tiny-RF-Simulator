#include "spectrum_analyzer_markers.h"
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

// The marker session token is the view-state persistence format: a malformed token must be
// refused without changing any marker, and a valid one must round-trip every field.

TEST_CASE("marker session token round-trips the view state") {
    MarkerModel model;
    model.setEnabled(0, true);
    model.place(0, 1.02e9);
    model.setEnabled(2, true);
    model.place(2, 1.035e9);
    model.setDelta(2, true);
    model.select(2);
    model.setTableVisible(true);

    const std::string token = model.serialize();
    REQUIRE_FALSE(token.empty());

    MarkerModel restored;
    REQUIRE(restored.restore(token));
    REQUIRE(restored.selected() == 2);
    REQUIRE(restored.tableVisible());
    REQUIRE(restored.enabled(0));
    REQUIRE(restored.frequency(0) == 1.02e9);
    REQUIRE(restored.enabled(2));
    REQUIRE(restored.delta(2));
    REQUIRE(restored.frequency(2) == 1.035e9);
    REQUIRE_FALSE(restored.enabled(1));
    REQUIRE_FALSE(restored.delta(0));
}

TEST_CASE("malformed marker session tokens are refused without changing state") {
    MarkerModel model = enabledModel();
    model.select(1);
    const std::string before = model.serialize();

    REQUIRE_FALSE(model.restore("garbage"));
    REQUIRE_FALSE(model.restore("rfsim-sa-markers:1|sel:0|table:0|m:0,0,0"));
    REQUIRE_FALSE(
        model.restore("rfsim-sa-markers:1|sel:9|table:0|m:0,0,0|m:0,0,0|m:0,0,0|m:0,0,0"));
    REQUIRE_FALSE(
        model.restore("rfsim-sa-markers:1|sel:0|table:2|m:0,0,0|m:0,0,0|m:0,0,0|m:0,0,0"));
    REQUIRE_FALSE(
        model.restore("rfsim-sa-markers:1|sel:0|table:0|m:2,0,0|m:0,0,0|m:0,0,0|m:0,0,0"));
    REQUIRE_FALSE(
        model.restore("rfsim-sa-markers:1|sel:0|table:0|m:0,0,nan|m:0,0,0|m:0,0,0|m:0,0,0"));
    REQUIRE(model.serialize() == before);
}
