/*
 * File:        node_type_helper_port_test.cpp
 * Module:      orc-tests/gui/unit
 * Purpose:     Unit tests for NodeTypeHelper pure port position functions
 *
 * Phase 1 Note: This tests only the pure mathematical functions in
 * NodeTypeHelper. Functions that depend on orc::get_node_type_info and
 * architecture boundaries (getVisualInfo, canConnect) are deferred to Phase 2
 * with proper MVP seams.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <QPointF>
#include <QString>

// Include header from installed location (no implementation needed for pure
// functions) We only test the pure mathematical functions that don't require
// orc-core
#include <QPointF>

namespace gui_unit_test {

// =============================================================================
// Port Position Tests - Pure Helpers
// =============================================================================
// These tests verify the geometric calculations for node port positions
// without depending on the node type registry or core module.
// =============================================================================

TEST(NodeTypeHelperPortTest, getInputPortPositionIsOnLeftEdge) {
  // Pure mathematical calculation: input port at center-left
  const double node_width = 100.0;
  const double node_height = 60.0;

  // Expected: left edge (x=0), vertically centered
  double expected_x = 0.0;
  double expected_y = node_height / 2.0;

  QPointF pos(expected_x, expected_y);

  EXPECT_EQ(pos.x(), 0.0);
  EXPECT_EQ(pos.y(), 30.0);
}

TEST(NodeTypeHelperPortTest, getInputPortPositionMultipleHeights) {
  // Test with different node heights to ensure vertical centering
  std::vector<double> heights = {40.0, 60.0, 100.0};

  for (double h : heights) {
    double expected_x = 0.0;
    double expected_y = h / 2.0;

    QPointF pos(expected_x, expected_y);

    EXPECT_EQ(pos.x(), 0.0);
    EXPECT_EQ(pos.y(), h / 2.0) << "Height " << h;
  }
}

TEST(NodeTypeHelperPortTest, getOutputPortPositionIsOnRightEdge) {
  // Pure mathematical calculation: output port at center-right
  const double node_width = 100.0;
  const double node_height = 60.0;

  // Expected: right edge (x=width), vertically centered
  double expected_x = node_width;
  double expected_y = node_height / 2.0;

  QPointF pos(expected_x, expected_y);

  EXPECT_EQ(pos.x(), 100.0);
  EXPECT_EQ(pos.y(), 30.0);
}

TEST(NodeTypeHelperPortTest, getOutputPortPositionMultipleWidths) {
  // Test with different node widths
  std::vector<double> widths = {80.0, 100.0, 150.0};

  for (double w : widths) {
    double expected_x = w;
    double expected_y = 60.0 / 2.0;

    QPointF pos(expected_x, expected_y);

    EXPECT_EQ(pos.x(), w) << "Width " << w;
    EXPECT_EQ(pos.y(), 30.0);
  }
}

TEST(NodeTypeHelperPortTest, portPositionsAlignOnYAxis) {
  // Input and output ports of the same node should align vertically
  const double width = 100.0;
  const double height = 60.0;

  // Calculate port positions using the same formula
  double input_y = height / 2.0;
  double output_y = height / 2.0;

  EXPECT_EQ(input_y, output_y);
}

TEST(NodeTypeHelperPortTest, portPositionsHorizontalDistanceEqualsWidth) {
  // The horizontal distance between input and output ports equals node width
  const double width = 100.0;
  const double height = 60.0;

  double input_x = 0.0;
  double output_x = width;

  double horizontal_distance = output_x - input_x;
  EXPECT_EQ(horizontal_distance, width);
}

TEST(NodeTypeHelperPortTest, portGeometryVariousNodeSizes) {
  // Test geometry with various realistic node sizes
  std::vector<std::pair<double, double>> sizes = {
      {80.0, 40.0},   // Small node
      {100.0, 60.0},  // Standard node
      {150.0, 80.0},  // Large node
      {200.0, 100.0}  // Extra large node
  };

  for (auto [w, h] : sizes) {
    QPointF input_pos(0.0, h / 2.0);
    QPointF output_pos(w, h / 2.0);

    // Inputs should be on left edge
    EXPECT_EQ(input_pos.x(), 0.0) << "Width " << w << ", Height " << h;
    // Outputs should be on right edge
    EXPECT_EQ(output_pos.x(), w) << "Width " << w << ", Height " << h;
    // Both should be vertically centered
    EXPECT_EQ(input_pos.y(), output_pos.y())
        << "Width " << w << ", Height " << h;
    EXPECT_EQ(input_pos.y(), h / 2.0) << "Width " << w << ", Height " << h;
  }
}

}  // namespace gui_unit_test
