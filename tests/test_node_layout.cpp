#include "node_layout.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <map>
#include <vector>

namespace {

LayoutItem item(int id, float x, float y, float w, float h) {
    return LayoutItem{id, LayoutPoint{x, y}, LayoutSize{w, h}};
}

// Looks up the move for `node_id`; the test fails if the layout dropped the node.
LayoutPoint placed(const std::vector<LayoutMove> &moves, int node_id) {
    for (const LayoutMove &move : moves) {
        if (move.node_id == node_id)
            return move.position;
    }
    FAIL("no move for node " << node_id);
    return {};
}

void requireAt(const std::vector<LayoutMove> &moves, int node_id, float x, float y) {
    using Catch::Matchers::WithinAbs;
    const LayoutPoint p = placed(moves, node_id);
    REQUIRE_THAT(p.x, WithinAbs(x, 1e-4));
    REQUIRE_THAT(p.y, WithinAbs(y, 1e-4));
}

// Three nodes of different sizes, deliberately out of order in the input.
std::vector<LayoutItem> threeMixed() {
    return {
        item(12, 300.0f, 40.0f, 60.0f, 30.0f),
        item(10, 0.0f, 10.0f, 100.0f, 50.0f),
        item(11, 150.0f, 80.0f, 40.0f, 80.0f),
    };
}

} // namespace

TEST_CASE("Align left puts every node's leading edge on the lowest x", "[node_layout]") {
    const auto two = std::vector<LayoutItem>{item(1, 20.0f, 0.0f, 50.0f, 40.0f),
                                             item(2, 90.0f, 60.0f, 30.0f, 40.0f)};
    const auto moves = alignNodes(two, AlignMode::Left);
    requireAt(moves, 1, 20.0f, 0.0f);
    requireAt(moves, 2, 20.0f, 60.0f);

    const auto three = alignNodes(threeMixed(), AlignMode::Left);
    requireAt(three, 10, 0.0f, 10.0f);
    requireAt(three, 11, 0.0f, 80.0f);
    requireAt(three, 12, 0.0f, 40.0f);
}

TEST_CASE("Align right puts every node's trailing edge on the highest right edge",
          "[node_layout]") {
    // Right edges are 70 and 120 for the two-node case, so the right edge of both lands on 120.
    const auto two = std::vector<LayoutItem>{item(1, 20.0f, 0.0f, 50.0f, 40.0f),
                                             item(2, 90.0f, 60.0f, 30.0f, 40.0f)};
    const auto moves = alignNodes(two, AlignMode::Right);
    requireAt(moves, 1, 70.0f, 0.0f);
    requireAt(moves, 2, 90.0f, 60.0f);

    // Three nodes: right edges are 100, 360 and 190, so the bounding right edge is 360.
    const auto three = alignNodes(threeMixed(), AlignMode::Right);
    requireAt(three, 10, 260.0f, 10.0f);
    requireAt(three, 11, 320.0f, 80.0f);
    requireAt(three, 12, 300.0f, 40.0f);
}

TEST_CASE("Align top puts every node's top edge on the lowest y", "[node_layout]") {
    const auto two = std::vector<LayoutItem>{item(1, 0.0f, 20.0f, 50.0f, 40.0f),
                                             item(2, 60.0f, 90.0f, 30.0f, 40.0f)};
    const auto moves = alignNodes(two, AlignMode::Top);
    requireAt(moves, 1, 0.0f, 20.0f);
    requireAt(moves, 2, 60.0f, 20.0f);

    const auto three = alignNodes(threeMixed(), AlignMode::Top);
    requireAt(three, 10, 0.0f, 10.0f);
    requireAt(three, 11, 150.0f, 10.0f);
    requireAt(three, 12, 300.0f, 10.0f);
}

TEST_CASE("Align bottom puts every node's bottom edge on the highest bottom edge",
          "[node_layout]") {
    // Bottom edges are 60 and 130 in the two-node case, so both bottoms land on 130.
    const auto two = std::vector<LayoutItem>{item(1, 0.0f, 20.0f, 50.0f, 40.0f),
                                             item(2, 60.0f, 90.0f, 30.0f, 40.0f)};
    const auto moves = alignNodes(two, AlignMode::Bottom);
    requireAt(moves, 1, 0.0f, 90.0f);
    requireAt(moves, 2, 60.0f, 90.0f);

    // Three nodes: bottom edges are 60, 160 and 70, so the bounding bottom is 160.
    const auto three = alignNodes(threeMixed(), AlignMode::Bottom);
    requireAt(three, 10, 0.0f, 110.0f);
    requireAt(three, 11, 150.0f, 80.0f);
    requireAt(three, 12, 300.0f, 130.0f);
}

TEST_CASE("Centre horizontally gives every node the bounding-box centre x", "[node_layout]") {
    // Bounding span is x in [20, 120] for the two-node case, so the centre is x = 70.
    const auto two = std::vector<LayoutItem>{item(1, 20.0f, 0.0f, 50.0f, 40.0f),
                                             item(2, 90.0f, 60.0f, 30.0f, 40.0f)};
    const auto moves = alignNodes(two, AlignMode::CentreHorizontally);
    requireAt(moves, 1, 45.0f, 0.0f);  // centre 70 - width/2 25
    requireAt(moves, 2, 55.0f, 60.0f); // centre 70 - width/2 15

    // Three nodes span x in [0, 360], so the centre is 180.
    const auto three = alignNodes(threeMixed(), AlignMode::CentreHorizontally);
    requireAt(three, 10, 130.0f, 10.0f);
    requireAt(three, 11, 160.0f, 80.0f);
    requireAt(three, 12, 150.0f, 40.0f);
}

TEST_CASE("Centre vertically gives every node the bounding-box centre y", "[node_layout]") {
    // Span y in [20, 130], centre 75.
    const auto two = std::vector<LayoutItem>{item(1, 0.0f, 20.0f, 50.0f, 40.0f),
                                             item(2, 60.0f, 90.0f, 30.0f, 40.0f)};
    const auto moves = alignNodes(two, AlignMode::CentreVertically);
    requireAt(moves, 1, 0.0f, 55.0f); // 75 - 20
    requireAt(moves, 2, 60.0f, 55.0f);

    // Three nodes span y in [10, 160], centre 85.
    const auto three = alignNodes(threeMixed(), AlignMode::CentreVertically);
    requireAt(three, 10, 0.0f, 60.0f);   // 85 - 25
    requireAt(three, 11, 150.0f, 45.0f); // 85 - 40
    requireAt(three, 12, 300.0f, 70.0f); // 85 - 15
}

TEST_CASE("Align leaves the other axis alone", "[node_layout]") {
    const auto moves = alignNodes(threeMixed(), AlignMode::Left);
    requireAt(moves, 11, 0.0f, 80.0f); // y is untouched by a horizontal align
}

TEST_CASE("Align with fewer than two nodes changes nothing", "[node_layout]") {
    const auto single = std::vector<LayoutItem>{item(7, 33.0f, 44.0f, 50.0f, 40.0f)};
    requireAt(alignNodes(single, AlignMode::Left), 7, 33.0f, 44.0f);
    requireAt(alignNodes(single, AlignMode::CentreVertically), 7, 33.0f, 44.0f);

    REQUIRE(alignNodes({}, AlignMode::Top).empty());
}

TEST_CASE("Align on nodes already in line returns their positions unchanged", "[node_layout]") {
    const auto aligned = std::vector<LayoutItem>{item(1, 40.0f, 10.0f, 50.0f, 40.0f),
                                                 item(2, 40.0f, 90.0f, 30.0f, 20.0f)};
    const auto moves = alignNodes(aligned, AlignMode::Left);
    requireAt(moves, 1, 40.0f, 10.0f);
    requireAt(moves, 2, 40.0f, 90.0f);
}

TEST_CASE("Distribute horizontally spaces three nodes with equal gaps", "[node_layout]") {
    // Widths 100, 40, 60. Outer nodes stay at x=0 and x=300. Free space between their edges is
    // 300 - 100 - 40 = 160, so each of the two gaps is 80.
    const auto moves = distributeNodes(threeMixed(), DistributeAxis::Horizontal);
    requireAt(moves, 10, 0.0f, 10.0f);   // outer left, unchanged
    requireAt(moves, 11, 180.0f, 80.0f); // 0 + 100 + 80
    requireAt(moves, 12, 300.0f, 40.0f); // outer right, unchanged

    // Gap check from the placed positions: 180 - 100 = 80 and 300 - (180 + 40) = 80.
    const float gap1 = placed(moves, 11).x - (placed(moves, 10).x + 100.0f);
    const float gap2 = placed(moves, 12).x - (placed(moves, 11).x + 40.0f);
    REQUIRE_THAT(gap1, Catch::Matchers::WithinAbs(gap2, 1e-4));
}

TEST_CASE("Distribute horizontally with four nodes gives equal gaps regardless of input order",
          "[node_layout]") {
    // Same geometry as the three-node case, with a fourth node of width 20 at x=10.
    const auto nodes = std::vector<LayoutItem>{
        item(3, 300.0f, 0.0f, 60.0f, 30.0f),
        item(1, 0.0f, 0.0f, 100.0f, 30.0f),
        item(4, 10.0f, 0.0f, 20.0f, 30.0f),
        item(2, 150.0f, 0.0f, 40.0f, 30.0f),
    };
    const auto moves = distributeNodes(nodes, DistributeAxis::Horizontal);
    // Leading edge order is 1 (0), 4 (10), 2 (150), 3 (300). Free space = 300 - 100 - 40 - 20 =
    // 140, so each of the three gaps is 46.667.
    requireAt(moves, 1, 0.0f, 0.0f);
    requireAt(moves, 3, 300.0f, 0.0f);
    REQUIRE_THAT(placed(moves, 4).x, Catch::Matchers::WithinAbs(146.6667f, 1e-3));
    REQUIRE_THAT(placed(moves, 2).x, Catch::Matchers::WithinAbs(213.3333f, 1e-3));
}

TEST_CASE("Distribute vertically spaces three nodes with equal gaps", "[node_layout]") {
    // Heights 30, 10, 40 at y = 0, 100, 200. Free space between the outer edges is
    // 200 - (0 + 30) - 10 = 160, so each gap is 80 and the middle node moves to y = 110.
    const auto nodes = std::vector<LayoutItem>{
        item(1, 0.0f, 0.0f, 50.0f, 30.0f),
        item(2, 200.0f, 100.0f, 50.0f, 10.0f),
        item(3, 400.0f, 200.0f, 50.0f, 40.0f),
    };
    const auto moves = distributeNodes(nodes, DistributeAxis::Vertical);
    requireAt(moves, 1, 0.0f, 0.0f);
    requireAt(moves, 2, 200.0f, 110.0f);
    requireAt(moves, 3, 400.0f, 200.0f);
}

TEST_CASE("Distribute vertically with separated outer nodes spaces the middle", "[node_layout]") {
    const auto nodes = std::vector<LayoutItem>{
        item(1, 0.0f, 0.0f, 50.0f, 20.0f),
        item(2, 0.0f, 100.0f, 50.0f, 20.0f),
        item(3, 0.0f, 220.0f, 50.0f, 20.0f),
    };
    const auto moves = distributeNodes(nodes, DistributeAxis::Vertical);
    // Free space = 220 - 20 - 20 = 180, gap 90, so the middle node goes to y = 110.
    requireAt(moves, 2, 0.0f, 110.0f);
    requireAt(moves, 3, 0.0f, 220.0f);
}

TEST_CASE("Distribute with fewer than three nodes changes nothing", "[node_layout]") {
    const auto two = std::vector<LayoutItem>{item(1, 0.0f, 0.0f, 50.0f, 20.0f),
                                             item(2, 300.0f, 70.0f, 50.0f, 20.0f)};
    const auto horizontal = distributeNodes(two, DistributeAxis::Horizontal);
    requireAt(horizontal, 1, 0.0f, 0.0f);
    requireAt(horizontal, 2, 300.0f, 70.0f);

    const auto vertical = distributeNodes(two, DistributeAxis::Vertical);
    requireAt(vertical, 2, 300.0f, 70.0f);
}

TEST_CASE("Distribute with already equal gaps returns the same positions", "[node_layout]") {
    // Widths 20 each at x = 0, 30, 60: gaps are 10 and 10 already.
    const auto nodes = std::vector<LayoutItem>{
        item(1, 0.0f, 0.0f, 20.0f, 20.0f),
        item(2, 30.0f, 0.0f, 20.0f, 20.0f),
        item(3, 60.0f, 0.0f, 20.0f, 20.0f),
    };
    const auto moves = distributeNodes(nodes, DistributeAxis::Horizontal);
    requireAt(moves, 2, 30.0f, 0.0f);
}
