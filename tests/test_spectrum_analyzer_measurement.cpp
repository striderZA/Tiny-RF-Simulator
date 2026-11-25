#include "spectrum_analyzer_measurement.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>

// The measurement source is view state: "All visible" keeps the combined readout, while a
// chosen probe scopes the readouts to that trace. The model owns the selection identity and
// the hidden/removed fallback rules; the widget test drives the same rules through the panel.

namespace {

// Stand-in probe identities. Real ones are (node pointer, output index) pairs.
MeasurementProbeId probe(int id, int output = 0) {
    return MeasurementProbeId{reinterpret_cast<const void *>(static_cast<std::uintptr_t>(id)),
                              output};
}

} // namespace

TEST_CASE("measurement selection defaults to all visible traces") {
    MeasurementSelection selection;
    REQUIRE(selection.allVisible());
    REQUIRE_FALSE(selection.probe().has_value());

    const auto resolution = selection.resolve({probe(1), probe(2)}, {true, true});
    REQUIRE(resolution.allVisible());
    REQUIRE(resolution.probe_index == MeasurementSelection::kAllVisible);
    REQUIRE(resolution.requested_index == MeasurementSelection::kAllVisible);
    REQUIRE(resolution.fallback == MeasurementSelection::Fallback::None);
    REQUIRE_FALSE(resolution.fallbackActive());
}

TEST_CASE("a selected probe resolves to its current slot") {
    MeasurementSelection selection;
    selection.selectProbe(probe(2, 1));

    const auto resolution = selection.resolve({probe(1), probe(2, 1)}, {true, true});
    REQUIRE_FALSE(resolution.allVisible());
    REQUIRE(resolution.probe_index == 1);
    REQUIRE(resolution.requested_index == 1);
    REQUIRE(resolution.fallback == MeasurementSelection::Fallback::None);
}

TEST_CASE("probe identity survives probe-list reordering") {
    MeasurementSelection selection;
    selection.selectProbe(probe(2));

    const auto moved = selection.resolve({probe(2), probe(1)}, {true, true});
    REQUIRE(moved.probe_index == 0);
    REQUIRE(moved.requested_index == 0);
    REQUIRE(moved.fallback == MeasurementSelection::Fallback::None);
}

TEST_CASE("a hidden selected probe falls back to all visible and reports why") {
    MeasurementSelection selection;
    selection.selectProbe(probe(2));

    const auto resolution = selection.resolve({probe(1), probe(2)}, {true, false});
    REQUIRE(resolution.allVisible());
    REQUIRE(resolution.fallback == MeasurementSelection::Fallback::Hidden);
    REQUIRE(resolution.fallbackActive());
    // The requested slot is still named, so the widget can explain the fallback.
    REQUIRE(resolution.requested_index == 1);
    // The selection itself is kept: showing the trace again resumes probe measurement.
    REQUIRE(selection.probe() == probe(2));
    REQUIRE(selection.resolve({probe(1), probe(2)}, {true, true}).probe_index == 1);
}

TEST_CASE("an empty visibility list treats every probe as visible") {
    MeasurementSelection selection;
    selection.selectProbe(probe(1));

    const auto resolution = selection.resolve({probe(1)}, {});
    REQUIRE(resolution.probe_index == 0);
    REQUIRE(resolution.fallback == MeasurementSelection::Fallback::None);
}

TEST_CASE("a removed selected probe falls back to all visible until it returns") {
    MeasurementSelection selection;
    selection.selectProbe(probe(2));

    const auto removed = selection.resolve({probe(1)}, {true});
    REQUIRE(removed.allVisible());
    REQUIRE(removed.fallback == MeasurementSelection::Fallback::Removed);
    REQUIRE(removed.requested_index == MeasurementSelection::kAllVisible);
    REQUIRE(selection.probe() == probe(2));

    // Probing it again under the same identity resumes the selection.
    const auto returned = selection.resolve({probe(1), probe(2)}, {true, true});
    REQUIRE(returned.probe_index == 1);
    REQUIRE(returned.fallback == MeasurementSelection::Fallback::None);
}

TEST_CASE("selecting all visible clears a probe selection") {
    MeasurementSelection selection;
    selection.selectProbe(probe(1));
    selection.selectAllVisible();

    REQUIRE(selection.allVisible());
    const auto resolution = selection.resolve({probe(1)}, {true});
    REQUIRE(resolution.allVisible());
    REQUIRE(resolution.fallback == MeasurementSelection::Fallback::None);
}

TEST_CASE("output ports of one node are separate measurement identities") {
    MeasurementSelection selection;
    selection.selectProbe(probe(1, 1));

    const auto resolution = selection.resolve({probe(1, 0), probe(1, 1)}, {true, true});
    REQUIRE(resolution.probe_index == 1);
    REQUIRE(resolution.fallback == MeasurementSelection::Fallback::None);
}
