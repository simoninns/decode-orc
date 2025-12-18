/******************************************************************************
 * project_to_dag.cpp
 *
 * Project to DAG Conversion - Convert serializable Project to executable DAG
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 ******************************************************************************/

#include "project_to_dag.h"
#include "stage_registry.h"
#include <algorithm>
#include <sstream>

namespace orc {

std::shared_ptr<DAG> project_to_dag(const Project& project) {
    auto dag = std::make_shared<DAG>();
    auto& registry = StageRegistry::instance();
    
    // Convert each ProjectDAGNode to a DAGNode
    // All nodes are uniform now - SOURCE nodes just use TBCSourceStage
    std::vector<DAGNode> dag_nodes;
    
    for (const auto& proj_node : project.nodes) {
        DAGNode dag_node;
        dag_node.node_id = proj_node.node_id;
        
        // Instantiate stage from registry
        if (!registry.has_stage(proj_node.stage_name)) {
            throw ProjectConversionError(
                "Unknown stage type: " + proj_node.stage_name + " in node " + proj_node.node_id
            );
        }
        
        dag_node.stage = registry.create_stage(proj_node.stage_name);
        
        // Convert parameters from ParameterValue to string map
        for (const auto& [key, value] : proj_node.parameters) {
            std::string str_value;
            if (std::holds_alternative<std::string>(value)) {
                str_value = std::get<std::string>(value);
            } else if (std::holds_alternative<int>(value)) {
                str_value = std::to_string(std::get<int>(value));
            } else if (std::holds_alternative<double>(value)) {
                str_value = std::to_string(std::get<double>(value));
            } else if (std::holds_alternative<bool>(value)) {
                str_value = std::get<bool>(value) ? "true" : "false";
            }
            dag_node.parameters[key] = str_value;
        }
        
        // Find input edges for this node
        for (const auto& edge : project.edges) {
            if (edge.target_node_id == proj_node.node_id) {
                dag_node.input_node_ids.push_back(edge.source_node_id);
                dag_node.input_indices.push_back(0);  // Assume output index 0
            }
        }
        
        dag_nodes.push_back(dag_node);
    }
    
    // Add all nodes to DAG
    for (const auto& node : dag_nodes) {
        dag->add_node(node);
    }
    
    // Find SINK nodes for output
    std::vector<std::string> output_node_ids;
    for (const auto& proj_node : project.nodes) {
        if (proj_node.node_type == NodeType::SINK) {
            output_node_ids.push_back(proj_node.node_id);
        }
    }
    if (!output_node_ids.empty()) {
        dag->set_output_nodes(output_node_ids);
    }
    
    // Validate the DAG
    if (!dag->validate()) {
        std::ostringstream oss;
        oss << "DAG validation failed:";
        for (const auto& error : dag->get_validation_errors()) {
            oss << "\n  - " << error;
        }
        throw ProjectConversionError(oss.str());
    }
    
    return dag;
}

} // namespace orc
