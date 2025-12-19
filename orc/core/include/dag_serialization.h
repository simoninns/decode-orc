/*
 * File:        dag_serialization.h
 * Module:      orc-core
 * Purpose:     DAG serialization to/from formats
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include "stage_parameter.h"
#include "node_type.h"

namespace orc {

/// Node in a GUI DAG representation
struct GUIDAGNode {
    std::string node_id;
    std::string stage_name;
    NodeType node_type;        // Node type (SOURCE, SINK, TRANSFORM, etc.)
    std::string display_name;  // Display name for GUI
    double x_position;  // Position for GUI layout
    double y_position;
    std::map<std::string, ParameterValue> parameters;
};

/// Edge in a GUI DAG representation
struct GUIDAGEdge {
    std::string source_node_id;
    std::string target_node_id;
};

/// Complete GUI DAG representation
struct GUIDAG {
    std::string name;
    std::string version;
    std::vector<GUIDAGNode> nodes;
    std::vector<GUIDAGEdge> edges;
};

/// DAG serialization functions
namespace dag_serialization {
    /// Load a GUI DAG from YAML file
    GUIDAG load_dag_from_yaml(const std::string& filename);
    
    /// Save a GUI DAG to YAML file
    void save_dag_to_yaml(const GUIDAG& dag, const std::string& filename);
}

} // namespace orc
