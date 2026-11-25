#include "spectrum_analyzer_peak_table.h"
#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <vector>

// The peak table is view state: it lists the strongest local maxima of the current
// measurement source, and its selection names the peak the selected marker moves to.
// The model is UI-free; test_ui drives the table in the panel.

namespace {

// Peaks in the order findPeaks() returns them (already strongest first), with a
// deliberately unordered list for the sorting test.
std::vector<Peak> threePeaks() {
    return {{2, 1.02e9, -20.0}, {7, 1.07e9, -5.0}, {4, 1.04e9, -40.0}};
}

} // namespace

TEST_CASE("peak table starts hidden and empty") {
    PeakTableModel table;
    REQUIRE_FALSE(table.visible());
    REQUIRE(table.count() == 0);
    REQUIRE(table.selected() == -1);
    REQUIRE_FALSE(table.step(1));
    REQUIRE_FALSE(table.step(-1));

    table.setVisible(true);
    REQUIRE(table.visible());
}

TEST_CASE("peak table sorts by descending amplitude and anchors deltas at the strongest") {
    PeakTableModel table;
    table.setPeaks(threePeaks());

    REQUIRE(table.count() == 3);
    const auto &rows = table.rows();
    REQUIRE(rows[0].frequency_Hz == Catch::Approx(1.07e9));
    REQUIRE(rows[0].amplitude_dBm == Catch::Approx(-5.0));
    REQUIRE(rows[0].delta_frequency_Hz == Catch::Approx(0.0));
    REQUIRE(rows[0].delta_amplitude_dB == Catch::Approx(0.0));

    REQUIRE(rows[1].frequency_Hz == Catch::Approx(1.02e9));
    REQUIRE(rows[1].delta_frequency_Hz == Catch::Approx(-50e6));
    REQUIRE(rows[1].delta_amplitude_dB == Catch::Approx(-15.0));

    REQUIRE(rows[2].frequency_Hz == Catch::Approx(1.04e9));
    REQUIRE(rows[2].delta_frequency_Hz == Catch::Approx(-30e6));
    REQUIRE(rows[2].delta_amplitude_dB == Catch::Approx(-35.0));
}

TEST_CASE("peak table keeps at most five peaks, strongest first") {
    std::vector<Peak> peaks;
    for (int i = 0; i < 9; ++i)
        peaks.push_back(Peak{i, 1.0e9 + static_cast<double>(i) * 10e6, -50.0 + i});
    std::reverse(peaks.begin(), peaks.end());

    PeakTableModel table;
    table.setPeaks(peaks);
    REQUIRE(table.count() == PeakTableModel::kMaxPeaks);
    for (int i = 0; i < table.count(); ++i) {
        REQUIRE(table.rows()[static_cast<size_t>(i)].amplitude_dBm == Catch::Approx(-42.0 - i));
    }
}

TEST_CASE("peak table selection clamps into the rows and steps with wrap-around") {
    PeakTableModel table;
    table.setPeaks(threePeaks());
    REQUIRE(table.selected() == 0);

    table.select(99);
    REQUIRE(table.selected() == 2);
    table.select(-3);
    REQUIRE(table.selected() == 0);

    REQUIRE(table.step(1));
    REQUIRE(table.selected() == 1);
    REQUIRE(table.step(1));
    REQUIRE(table.selected() == 2);
    REQUIRE(table.step(1));
    REQUIRE(table.selected() == 0);

    REQUIRE(table.step(-1));
    REQUIRE(table.selected() == 2);
    table.select(1);
    REQUIRE(table.selected() == 1);
}

TEST_CASE("peak table treats an empty search as no rows and no selection") {
    PeakTableModel table;
    table.setPeaks({});
    REQUIRE(table.count() == 0);
    REQUIRE(table.selected() == -1);
    table.select(0);
    REQUIRE(table.selected() == -1);
    REQUIRE_FALSE(table.step(1));
}

TEST_CASE("a smaller peak list clamps the selection instead of dropping it") {
    PeakTableModel table;
    table.setPeaks(threePeaks());
    table.select(2);
    REQUIRE(table.selected() == 2);

    table.setPeaks({{0, 1.0e9, -10.0}});
    REQUIRE(table.count() == 1);
    REQUIRE(table.selected() == 0);

    table.setPeaks({});
    REQUIRE(table.selected() == -1);
}
