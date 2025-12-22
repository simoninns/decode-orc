/*
 * File:        project_to_dag.cpp
 * Module:      orc-core
 * Purpose:     Project to DAG conversion
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns *
 * ARCHITECTURE NOTE:
 * This file uses READ-ONLY access to Project via const getters.
 * It NEVER modifies Project state - use project_io:: functions for that. */


#include "project_to_dag.h"
#include "stage_registry.h"
#include "logging.h"
#include <algorithm>
#include <sstream>

namespace orc {

std::shared_ptr<DAG> project_to_dag(const Project& project) {
    auto dag = std::make_shared<DAG>();
    auto& registry = StageRegistry::instance();
    
    // Convert each ProjectDAGNode to a DAGNode
    // All nodes are uniform now - SOURCE nodes just use TBCSourceStage
    std::vector<DAGNode> dag_nodes;
    
    for (const auto& proj_node : project.get_nodes()) {
        DAGNode dag_node;
        dag_node.node_id = proj_node.node_id;
        
        // Instantiate stage from registry
        if (!registry.has_stage(proj_node.stage_name)) {
            throw ProjectConversionError(
                "Unknown stage type: " + proj_node.stage_name + " in node " + proj_node.node_id
            );
        }
        
        dag_node.stage = registry.create_stage(proj_node.stage_name);
        
        ORC_LOG_DEBUG("Node '{}': Converting from project (stage: {}, {} parameters)",
                      proj_node.node_id, proj_node.stage_name, proj_node.parameters.size());
        
        // Copy parameters directly (already strongly typed)
        dag_node.parameters = proj_node.parameters;
        for (const auto& [key, value] : proj_node.parameters) {
            std::visit([&proj_node, &key](const auto& v) {
                ORC_LOG_DEBUG("Node '{}':   param '{}' = {}", proj_node.node_id, key, v);
            }, value);
        }
        
        // Find input edges for this node
        for (const auto& edge : project.get_edges()) {
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
    for (const auto& proj_node : project.get_nodes()) {
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
