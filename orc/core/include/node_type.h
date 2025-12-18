// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025

#pragma once

#include <string>
#include <cstdint>
#include <vector>

namespace orc {

/**
 * @brief Node connectivity pattern
 * 
 * Defines the input/output structure of a DAG node type.
 * This determines how nodes can be connected and how the GUI renders them.
 */
enum class NodeType {
    /// Source node - no inputs, produces outputs (e.g., START nodes for TBC sources)
    SOURCE,
    
    /// Sink node - consumes inputs, no outputs (e.g., export to file, display output)
    SINK,
    
    /// Transform node - one input, one output (most common processing stages)
    TRANSFORM,
    
    /// Splitter node - one input, multiple outputs (fanout, duplicate streams)
    SPLITTER,
    
    /// Merger node - multiple inputs, one output (stacking, blending)
    MERGER,
    
    /// Complex node - multiple inputs, multiple outputs (advanced processing)
    COMPLEX
};

/**
 * @brief Metadata about a node type
 * 
 * Describes the characteristics of a processing stage for GUI rendering
 * and DAG validation.
 */
struct NodeTypeInfo {
    NodeType type;              // Connectivity pattern
    std::string stage_name;     // Type identifier (e.g., "START", "DropoutCorrect")
    std::string display_name;   // Human-readable name (e.g., "Source", "Dropout Correction")
    std::string description;    // Detailed description for tooltips
    uint32_t min_inputs;        // Minimum number of inputs (0 for SOURCE)
    uint32_t max_inputs;        // Maximum number of inputs (0 for SOURCE, UINT32_MAX for unlimited)
    uint32_t min_outputs;       // Minimum number of outputs (0 for SINK)
    uint32_t max_outputs;       // Maximum number of outputs (0 for SINK, UINT32_MAX for unlimited)
    bool user_creatable;        // Can user add this node via GUI? (false for START nodes)
};

/**
 * @brief Get node type information by stage name
 * 
 * @param stage_name Stage identifier (e.g., "START", "Passthrough")
 * @return Node type info, or nullptr if stage_name not recognized
 */
const NodeTypeInfo* get_node_type_info(const std::string& stage_name);

/**
 * @brief Get list of all available node types
 * 
 * @return Vector of all registered node type info
 */
const std::vector<NodeTypeInfo>& get_all_node_types();

/**
 * @brief Check if a connection is valid between two node types
 * 
 * @param source_stage Source node stage name
 * @param target_stage Target node stage name
 * @return true if connection is allowed
 */
bool is_connection_valid(const std::string& source_stage, const std::string& target_stage);

} // namespace orc
