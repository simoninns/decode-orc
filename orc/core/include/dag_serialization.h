/*
 * File:        dag_serialization.h
 * Module:      orc-core
 * Purpose:     DAG serialization to/from formats
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include "stage_parameter.h"
#include <node_type.h>
#include <node_id.h>

namespace orc {

/**
 * @brief Node in a GUI DAG representation
 * 
 * Contains all information needed to display and execute a DAG node,
 * including position, parameters, and type information.
 */
struct GUIDAGNode {
    NodeID node_id;             ///< Unique identifier for this node
    std::string stage_name;     ///< Name of the stage type (e.g., "TBCSource")
    NodeType node_type;         ///< Node type (SOURCE, SINK, TRANSFORM, etc.)
    std::string display_name;   ///< Display name for GUI
    std::string user_label;     ///< User-editable label
    double x_position;          ///< X position for GUI layout
    double y_position;          ///< Y position for GUI layout
    std::map<std::string, ParameterValue> parameters;  ///< Stage parameters
};

/**
 * @brief Edge in a GUI DAG representation
 * 
 * Represents a data flow connection from one node to another.
 */
struct GUIDAGEdge {
    NodeID source_node_id;  ///< Source node ID
    NodeID target_node_id;  ///< Target node ID
};

/**
 * @brief Complete GUI DAG representation
 * 
 * Contains all nodes and edges for a complete processing graph,
 * suitable for serialization and GUI display.
 */
struct GUIDAG {
    std::string name;                    ///< DAG name
    std::string version;                 ///< Format version
    std::vector<GUIDAGNode> nodes;       ///< All nodes in the DAG
    std::vector<GUIDAGEdge> edges;       ///< All edges in the DAG
};

/**
 * @brief DAG serialization functions
 * 
 * Provides utilities to load and save DAG representations to/from YAML files.
 */
namespace dag_serialization {
    /**
     * @brief Load a GUI DAG from YAML file
     * 
     * @param filename Path to the YAML file
     * @return Loaded DAG structure
     * @throws std::runtime_error if file cannot be loaded or parsed
     */
    GUIDAG load_dag_from_yaml(const std::string& filename);
    
    /**
     * @brief Save a GUI DAG to YAML file
     * 
     * @param dag The DAG to save
     * @param filename Path to the output YAML file
     * @throws std::runtime_error if file cannot be written
     */
    void save_dag_to_yaml(const GUIDAG& dag, const std::string& filename);
}

} // namespace orc
