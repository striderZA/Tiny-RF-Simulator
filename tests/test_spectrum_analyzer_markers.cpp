#include "spectrum_analyzer_markers.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <vector>

// Marker view state is UI-free: the widget owns presentation, while MarkerModel owns the
// nearest-bin resolution, delta arithmetic, per-marker peak search, and the session token.

namespace {

// 1.00 .. 1.04 GHz in 10 MHz steps with one strong bin at 1.02 GHz.
const std::vector<double> kAxis_Hz = {1.00e9, 1.01e9, 1.02e9, 1.03e9, 1.04e9};
const std::vector<double> kTrace_dBm = {-100.0, -90.0, -20.0, -90.0, -100.0};

MarkerModel enabledModel() {
    MarkerModel model;
    model.setEnabled(0, true);
    model.place(0, 1.02e9);
    return model;
}

} // namespace

TEST_CASE("marker model starts empty with M1 selected") {
    MarkerModel model;
    REQUIRE(model.selected() == 0);
    REQUIRE(model.enabledCount() == 0);
    REQUIRE_FALSE(model.anyEnabled());
    REQUIRE_FALSE(model.tableVisible());
    REQUIRE_FALSE(model.readout(0, kAxis_Hz, kTrace_dBm).valid);
    REQUIRE(model.resolveIndex(0, kAxis_Hz, kTrace_dBm) == -1);
}

TEST_CASE("marker enable and selection clamps into the four slots") {
    MarkerModel model;
    model.setEnabled(2, true);
    REQUIRE(model.enabled(2));
    REQUIRE(model.enabledCount() == 1);

    model.select(2);
    REQUIRE(model.selected() == 2);
    model.select(99);
    REQUIRE(model.selected() == MarkerModel::kMaxMarkers - 1);
    model.select(-5);
    REQUIRE(model.selected() == 0);

    model.setEnabled(MarkerModel::kMaxMarkers, true); // out of range, ignored
    REQUIRE(model.enabledCount() == 1);
}

TEST_CASE("marker readout resolves the nearest bin on the shared axis") {
    MarkerModel model = enabledModel();

    MarkerModel::Readout reading = model.readout(0, kAxis_Hz, kTrace_dBm);
    REQUIRE(reading.valid);
    REQUIRE(reading.frequency_Hz == 1.02e9);
    REQUIRE(reading.amplitude_dBm == -20.0);

    model.place(0, 1.031e9);
    reading = model.readout(0, kAxis_Hz, kTrace_dBm);
    REQUIRE(reading.frequency_Hz == 1.03e9);
    REQUIRE(reading.amplitude_dBm == -90.0);

    model.place(0, 0.5e9);
    REQUIRE(model.readout(0, kAxis_Hz, kTrace_dBm).frequency_Hz == 1.00e9);
    model.place(0, 2.0e9);
    REQUIRE(model.readout(0, kAxis_Hz, kTrace_dBm).frequency_Hz == 1.04e9);

    REQUIRE_FALSE(model.readout(0, {}, {}).valid);
}

TEST_CASE("nearest-bin ties resolve to the lower index") {
    MarkerModel model;
    const std::vector<double> axis_Hz = {0.0, 10.0, 20.0};
    const std::vector<double> trace_dBm = {-1.0, -2.0, -3.0};
    model.setEnabled(0, true);
    model.place(0, 5.0);
    REQUIRE(model.resolveIndex(0, axis_Hz, trace_dBm) == 0);
}

TEST_CASE("delta readout offsets both frequency and amplitude from the reference") {
    MarkerModel model;
    model.setEnabled(0, true);
    model.place(0, 1.02e9);
    model.setEnabled(1, true);
    model.place(1, 1.03e9);

    MarkerModel::Readout reference = model.readout(0, kAxis_Hz, kTrace_dBm);
    REQUIRE(reference.valid);
    REQUIRE_FALSE(reference.delta);

    REQUIRE_FALSE(model.readout(1, kAxis_Hz, kTrace_dBm).delta);
    model.setDelta(1, true);
    MarkerModel::Readout delta = model.readout(1, kAxis_Hz, kTrace_dBm);
    REQUIRE(delta.valid);
    REQUIRE(delta.delta);
    REQUIRE(delta.reference_valid);
    REQUIRE(delta.frequency_Hz == 1.03e9);
    REQUIRE(delta.amplitude_dBm == -90.0);
    REQUIRE(delta.delta_frequency_Hz == Catch::Approx(10e6));
    REQUIRE(delta.delta_amplitude_dB == Catch::Approx(-70.0));

    // The reference marker itself always reads absolute.
    model.setDelta(0, true);
    REQUIRE_FALSE(model.delta(0));
    REQUIRE_FALSE(model.readout(0, kAxis_Hz, kTrace_dBm).delta);
}

TEST_CASE("a delta marker reports an unavailable reference when M1 is off") {
    MarkerModel model;
    model.setEnabled(1, true);
    model.place(1, 1.03e9);
    model.setDelta(1, true);

    MarkerModel::Readout delta = model.readout(1, kAxis_Hz, kTrace_dBm);
    REQUIRE(delta.valid);
    REQUIRE(delta.delta);
    REQUIRE_FALSE(delta.reference_valid);
    REQUIRE(delta.frequency_Hz == 1.03e9);
}

TEST_CASE("snap to peak places the marker on the strongest cached peak") {
    MarkerModel model = enabledModel();
    const std::vector<Peak> peaks = {{3, 1.03e9, -10.0}, {2, 1.02e9, -20.0}, {1, 1.01e9, -30.0}};
    model.snapToPeak(0, peaks);
    REQUIRE(model.frequency(0) == 1.03e9);

    REQUIRE(model.nextPeak(0));
    REQUIRE(model.frequency(0) == 1.02e9);
    REQUIRE(model.nextPeak(0));
    REQUIRE(model.frequency(0) == 1.01e9);
    REQUIRE(model.nextPeak(0));
    REQUIRE(model.frequency(0) == 1.03e9);

    // An empty snap leaves the position alone.
    model.snapToPeak(0, {});
    REQUIRE(model.frequency(0) == 1.03e9);
}

TEST_CASE("next peak adopts a fresh peak list when the cache is empty") {
    MarkerModel model = enabledModel();
    const std::vector<Peak> peaks = {{4, 1.04e9, -5.0}, {1, 1.01e9, -30.0}};
    REQUIRE(model.nextPeak(0, peaks));
    REQUIRE(model.frequency(0) == 1.04e9);
    REQUIRE(model.nextPeak(0));
    REQUIRE(model.frequency(0) == 1.01e9);

    // With no cache and no supplied list there is nothing to cycle.
    MarkerModel empty;
    empty.setEnabled(0, true);
    REQUIRE_FALSE(empty.nextPeak(0, {}));
}

TEST_CASE("clearing peak caches makes Next Peak adopt the current source") {
    MarkerModel model = enabledModel();
    model.snapToPeak(0, {{2, 1.02e9, -20.0}, {1, 1.01e9, -30.0}});
    REQUIRE(model.nextPeak(0));
    REQUIRE(model.frequency(0) == 1.01e9);

    // A readout-source change drops the cached list but keeps the marker position.
    model.clearPeakCaches();
    REQUIRE(model.peaks(0).empty());
    REQUIRE(model.frequency(0) == 1.01e9);

    // Next Peak now adopts the new source's list instead of cycling the old one.
    REQUIRE(model.nextPeak(0, {{0, 1.05e9, -5.0}}));
    REQUIRE(model.frequency(0) == 1.05e9);
}

TEST_CASE("peak search caches are per marker") {
    MarkerModel model;
    model.setEnabled(0, true);
    model.place(0, 1.02e9);
    model.setEnabled(1, true);
    model.place(1, 1.02e9);

    model.snapToPeak(0, {{2, 1.02e9, -20.0}, {1, 1.01e9, -30.0}});
    model.snapToPeak(1, {{4, 1.04e9, -5.0}, {3, 1.03e9, -10.0}});
    REQUIRE(model.frequency(0) == 1.02e9);
    REQUIRE(model.frequency(1) == 1.04e9);

    REQUIRE(model.nextPeak(0));
    REQUIRE(model.frequency(0) == 1.01e9);
    REQUIRE(model.frequency(1) == 1.04e9);
    REQUIRE(model.nextPeak(1));
    REQUIRE(model.frequency(1) == 1.03e9);
}

TEST_CASE("a manual move invalidates the cached peak list") {
    MarkerModel model = enabledModel();
    model.snapToPeak(0, {{2, 1.02e9, -20.0}, {1, 1.01e9, -30.0}});
    REQUIRE(model.nextPeak(0));
    REQUIRE(model.frequency(0) == 1.01e9);

    // Re-placing the current value (the widget's per-frame drag line update)
    // keeps the cache; moving elsewhere clears it.
    model.place(0, 1.01e9);
    REQUIRE(model.nextPeak(0));
    REQUIRE(model.frequency(0) == 1.02e9);

    model.place(0, 1.04e9);
    REQUIRE_FALSE(model.nextPeak(0, {}));
}
