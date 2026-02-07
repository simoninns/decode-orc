/*
 * File:        project_to_dag.cpp
 * Module:      orc-core
 * Purpose:     Project to DAG conversion
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns *
 * ARCHITECTURE NOTE:
 * This file uses READ-ONLY access to Project via const getters.
 * It NEVER modifies Project state - use project_io:: functions for that. */


#include "project_to_dag.h"
#include "stage_registry.h"
#include "logging.h"
#include "observation_context.h"
#include <algorithm>
#include <sstream>
#include <filesystem>

namespace orc {

// Helper function to resolve paths relative to project root
// Matches the resolve_path function in project.cpp
static std::string resolve_path_for_execution(const std::string& path, const std::string& project_root) {
    if (path.empty() || project_root.empty()) {
        return path;
    }
    
    // First expand any ${PROJECT_ROOT} variables
    const std::string variable = "${PROJECT_ROOT}";
    std::string expanded = path;
    size_t pos = 0;
    while ((pos = expanded.find(variable, pos)) != std::string::npos) {
        expanded.replace(pos, variable.length(), project_root);
        pos += project_root.length();
    }
    
    // Create a filesystem path
    std::filesystem::path p(expanded);
    
    // If already absolute, return normalized version
    if (p.is_absolute()) {
        try {
            return std::filesystem::weakly_canonical(p).string();
        } catch (const std::filesystem::filesystem_error&) {
            return p.string();
        }
    }
    
    // Resolve relative to project root
    std::filesystem::path resolved = std::filesystem::path(project_root) / p;
    try {
        return std::filesystem::weakly_canonical(resolved).string();
    } catch (const std::filesystem::filesystem_error&) {
        return resolved.string();
    }
}

std::shared_ptr<DAG> project_to_dag(const Project& project) {
    auto dag = std::make_shared<DAG>();
    auto& registry = StageRegistry::instance();
    
    // Get project root for path resolution
    const std::string& project_root = project.get_project_root();
    
    // Convert each ProjectDAGNode to a DAGNode
    // All nodes are uniform now - SOURCE nodes just use TBCSourceStage
    std::vector<DAGNode> dag_nodes;
    
    for (const auto& proj_node : project.get_nodes()) {
        DAGNode dag_node;
        dag_node.node_id = proj_node.node_id;
        
        // Instantiate stage from registry
        if (!registry.has_stage(proj_node.stage_name)) {
            throw ProjectConversionError(
                "Unknown stage type: " + proj_node.stage_name + " in node " + std::to_string(proj_node.node_id.value())
            );
        }
        
        dag_node.stage = registry.create_stage(proj_node.stage_name);
        
        ORC_LOG_DEBUG("Node '{}': Converting from project (stage: {}, {} parameters)",
                      proj_node.node_id, proj_node.stage_name, proj_node.parameters.size());
        
        // Copy and resolve parameters
        dag_node.parameters = proj_node.parameters;
        
        // Resolve file paths relative to project root
        for (auto& [param_name, param_value] : dag_node.parameters) {
            if (std::holds_alternative<std::string>(param_value)) {
                // Check if this is a file path parameter
                bool is_file_path = (param_name.find("_path") != std::string::npos ||
                                   param_name == "output_path" ||
                                   param_name == "input_path");
                
                if (is_file_path) {
                    std::string path = std::get<std::string>(param_value);
                    if (!path.empty()) {
                        std::string resolved = resolve_path_for_execution(path, project_root);
                        if (resolved != path) {
                            ORC_LOG_DEBUG("Node '{}':   Resolved path '{}' -> '{}'", 
                                        proj_node.node_id, path, resolved);
                        }
                        param_value = resolved;
                    }
                }
            }
        }
        
        for (const auto& [key, value] : dag_node.parameters) {
            std::visit([&proj_node, key_ref = std::cref(key)](const auto& v) {
                ORC_LOG_DEBUG("Node '{}':   param '{}' = {}", proj_node.node_id, key_ref.get(), v);
            }, value);
        }
        
        // Apply parameters to the stage instance if it's parameterized
        auto* param_stage = dynamic_cast<ParameterizedStage*>(dag_node.stage.get());
        if (param_stage && !dag_node.parameters.empty()) {
            param_stage->set_parameters(dag_node.parameters);
            ORC_LOG_DEBUG("Node '{}': Applied {} parameters to stage instance", 
                         proj_node.node_id, dag_node.parameters.size());
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
    std::vector<NodeID> output_node_ids;
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

void validate_source_nodes(const std::shared_ptr<DAG>& dag) {
    if (!dag) {
        throw ProjectConversionError("Cannot validate null DAG");
    }
    
    // Try to execute each source node to validate they can be accessed
    // Source nodes may produce empty output if no file path is configured (valid placeholder state)
    ORC_LOG_DEBUG("Validating {} DAG nodes", dag->nodes().size());
    
    for (const auto& node : dag->nodes()) {
        // Check if this is a source node by checking if it has no inputs
        if (node.input_node_ids.empty()) {
            ORC_LOG_DEBUG("Validating source node: {}", node.node_id);
            try {
                // Execute the stage with empty inputs to validate
                // This will trigger TBC loading and validation
                ObservationContext observation_context;
                auto outputs = node.stage->execute({}, node.parameters, observation_context);
                if (outputs.empty()) {
                    // Empty output is valid - source may have no file configured (placeholder node)
                    ORC_LOG_WARN("Source node '{}' produced no output (no file configured)", node.node_id);
                } else {
                    ORC_LOG_DEBUG("Source node validation passed: {}", node.node_id);
                }
            } catch (const std::exception& e) {
                // Source validation failed - re-throw with more context
                throw ProjectConversionError(
                    "Source validation failed for node '" + node.node_id.to_string() + "': " + e.what()
                );
            }
        }
    }
}

} // namespace orc
