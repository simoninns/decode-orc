// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025

#include "node_type_helper.h"
#include <algorithm>
#include <iostream>

namespace NodeTypeHelper {

NodeVisualInfo getVisualInfo(const std::string& stage_name)
{
    const orc::NodeTypeInfo* info = orc::get_node_type_info(stage_name);
    
    if (!info) {
        // ERROR: Stage not registered - this should not happen!
        std::cerr << "ERROR: getVisualInfo() called with unknown stage '" << stage_name << "'" << std::endl;
        std::cerr << "  Falling back to default TRANSFORM (1 in, 1 out) - node will render incorrectly!" << std::endl;
        return NodeVisualInfo{true, true, false, false};
    }
    
    NodeVisualInfo visual;
    visual.has_input = (info->max_inputs > 0);
    visual.has_output = (info->max_outputs > 0);
    visual.input_is_many = (info->max_inputs > 1);
    visual.output_is_many = (info->max_outputs > 1);
    
    return visual;
}

QPointF getInputPortPosition(double node_width, double node_height)
{
    return QPointF(0, node_height / 2);
}

QPointF getOutputPortPosition(double node_width, double node_height)
{
    return QPointF(node_width, node_height / 2);
}

bool canConnect(
    const std::string& source_stage,
    const std::string& target_stage,
    uint32_t existing_input_count,
    uint32_t existing_output_count)
{
    // Check basic connection validity (source must have outputs, target must have inputs)
    if (!orc::is_connection_valid(source_stage, target_stage)) {
        return false;
    }
    
    // Get node type info
    const orc::NodeTypeInfo* source_info = orc::get_node_type_info(source_stage);
    const orc::NodeTypeInfo* target_info = orc::get_node_type_info(target_stage);
    
    if (!source_info || !target_info) {
        return false;
    }
    
    // Check if source has reached max outputs
    if (existing_output_count >= source_info->max_outputs) {
        return false;
    }
    
    // Check if target has reached max inputs
    if (existing_input_count >= target_info->max_inputs) {
        return false;
    }
    
    return true;
}

} // namespace NodeTypeHelper
