/*
 * File:        node_type_helper.h
 * Module:      orc-gui
 * Purpose:     Node type helper utilities
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */


#pragma once

#include <node_type.h>
#include <QString>
#include <QPointF>
#include <vector>

/**
 * @brief Helper functions for node type visualization
 */
namespace NodeTypeHelper {

/**
 * @brief Get visual representation info for a node type
 */
struct NodeVisualInfo {
    bool has_input;         // Whether node has input port
    bool has_output;        // Whether node has output port
    bool input_is_many;     // True if max_inputs > 1 (renders as concentric circles)
    bool output_is_many;    // True if max_outputs > 1 (renders as concentric circles)
};

/**
 * @brief Get visual info for a stage name
 * @param stage_name Stage identifier (e.g., "Source", "DropoutCorrect")
 * @return Visual info for rendering, or default if stage not recognized
 */
NodeVisualInfo getVisualInfo(const std::string& stage_name);

/**
 * @brief Get input connection point position (center-left)
 * @param node_width Width of the node rectangle
 * @param node_height Height of the node rectangle
 * @return Position relative to node origin
 */
QPointF getInputPortPosition(double node_width, double node_height);

/**
 * @brief Get output connection point position (center-right)
 * @param node_width Width of the node rectangle
 * @param node_height Height of the node rectangle
 * @return Position relative to node origin
 */
QPointF getOutputPortPosition(double node_width, double node_height);

/**
 * @brief Check if a connection is allowed
 * @param source_stage Source node stage name
 * @param target_stage Target node stage name
 * @param existing_input_count Number of inputs already connected to target
 * @param existing_output_count Number of outputs already connected from source
 * @return true if connection is allowed
 */
bool canConnect(
    const std::string& source_stage,
    const std::string& target_stage,
    uint32_t existing_input_count,
    uint32_t existing_output_count);

} // namespace NodeTypeHelper
