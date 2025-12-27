/*
 * File:        command_analyze_field_mapping.cpp
 * Module:      orc-cli
 * Purpose:     Analyze field mapping and optionally update project file
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "command_analyze_field_mapping.h"
#include "project.h"
#include "project_to_dag.h"
#include "dag_executor.h"
#include "stage_registry.h"
#include "analysis/field_mapping/field_mapping_analyzer.h"
#include "logging.h"

#include <iostream>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

namespace orc {
namespace cli {

int analyze_field_mapping_command(const AnalyzeFieldMappingOptions& options) {
    // Check if project file exists
    if (!fs::exists(options.project_path)) {
        ORC_LOG_ERROR("Project file not found: {}", options.project_path);
        return 1;
    }
    
    ORC_LOG_INFO("Loading project: {}", options.project_path);
    
    // Load project
    Project project;
    try {
        project = orc::project_io::load_project(options.project_path);
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("Failed to load project: {}", e.what());
        return 1;
    }
    
    ORC_LOG_INFO("Project loaded: {} (version {})", project.get_name(), project.get_version());
    
    // Convert project to DAG
    auto dag = orc::project_to_dag(project);
    if (!dag) {
        ORC_LOG_ERROR("Failed to convert project to DAG");
        return 1;
    }
    
    // Find all source nodes
    std::vector<std::string> source_node_ids;
    for (const auto& node : project.get_nodes()) {
        if (node.node_type == orc::NodeType::SOURCE) {
            source_node_ids.push_back(node.node_id);
        }
    }
    
    if (source_node_ids.empty()) {
        ORC_LOG_ERROR("No SOURCE nodes found in project");
        return 1;
    }
    
    ORC_LOG_INFO("Found {} source node(s)", source_node_ids.size());
    
    // For each source, find connected field_map nodes and analyze
    std::map<std::string, FieldMappingDecision> decisions;
    
    for (const auto& source_node_id : source_node_ids) {
        ORC_LOG_INFO("");
        ORC_LOG_INFO("=== Analyzing source: {} ===", source_node_id);
        
        // Find field_map node(s) connected to this source
        std::string field_map_node_id;
        for (const auto& edge : project.get_edges()) {
            if (edge.source_node_id == source_node_id) {
                // Check if target is a field_map node
                for (const auto& node : project.get_nodes()) {
                    if (node.node_id == edge.target_node_id && node.stage_name == "field_map") {
                        field_map_node_id = edge.target_node_id;
                        break;
                    }
                }
                if (!field_map_node_id.empty()) break;
            }
        }
        
        if (field_map_node_id.empty()) {
            ORC_LOG_WARN("No field_map node found connected to source {}, skipping", source_node_id);
            continue;
        }
        
        ORC_LOG_INFO("Found connected field_map node: {}", field_map_node_id);
        
        // Execute DAG to get source representation
        try {
            DAGExecutor executor;
            auto results = executor.execute_to_node(*dag, source_node_id);
            
            auto source_it = results.find(source_node_id);
            if (source_it == results.end() || source_it->second.empty()) {
                ORC_LOG_ERROR("Failed to execute source node {}", source_node_id);
                continue;
            }
            
            auto source_artifact = source_it->second[0];
            auto* video_rep = dynamic_cast<VideoFieldRepresentation*>(source_artifact.get());
            if (!video_rep) {
                ORC_LOG_ERROR("Source node {} did not produce VideoFieldRepresentation", source_node_id);
                continue;
            }
            
            ORC_LOG_INFO("Running field mapping analysis...");
            
            // Run field mapping analysis
            FieldMappingAnalyzer analyzer;
            FieldMappingAnalyzer::Options analyzer_options;
            analyzer_options.pad_gaps = options.pad_gaps;
            analyzer_options.delete_unmappable_frames = options.delete_unmappable;
            
            auto decision = analyzer.analyze(*video_rep, analyzer_options);
            
            if (!decision.success) {
                ORC_LOG_ERROR("Field mapping analysis failed for {}: {}", source_node_id, decision.rationale);
                continue;
            }
            
            ORC_LOG_INFO("Field mapping analysis successful");
            ORC_LOG_INFO("Mapping specification: {}", decision.mapping_spec);
            ORC_LOG_INFO("Statistics:");
            ORC_LOG_INFO("  Input: {} fields", decision.stats.total_fields);
            ORC_LOG_INFO("  Output: {} fields", (decision.stats.total_fields - decision.stats.removed_lead_in_out * 2 
                         - decision.stats.removed_invalid_phase * 2 - decision.stats.removed_duplicates * 2 
                         - decision.stats.removed_unmappable * 2 + decision.stats.padding_frames * 2));
            ORC_LOG_INFO("  Removed: invalid_phase={} duplicates={} lead_in_out={}", 
                         decision.stats.removed_invalid_phase, decision.stats.removed_duplicates, 
                         decision.stats.removed_lead_in_out);
            ORC_LOG_INFO("  Added: padding_frames={}", decision.stats.padding_frames);
            
            // Store decision for this field_map node
            decisions[field_map_node_id] = decision;
        } catch (const std::exception& e) {
            ORC_LOG_ERROR("Failed to analyze source node {}: {}", source_node_id, e.what());
            continue;
        }
    }
    
    if (decisions.empty()) {
        ORC_LOG_ERROR("No field mapping analyses succeeded");
        return 1;
    }
    
    // Update project file if requested
    if (options.update_project) {
        ORC_LOG_INFO("");
        ORC_LOG_INFO("Updating project file with mapping specifications...");
        
        int updated_count = 0;
        for (const auto& [field_map_node_id, decision] : decisions) {
            // Update the field mapper node's ranges parameter
            const auto& nodes = project.get_nodes();
            auto it = std::find_if(nodes.begin(), nodes.end(),
                [&field_map_node_id](const ProjectDAGNode& n) { return n.node_id == field_map_node_id; });
            
            if (it != nodes.end()) {
                std::string old_value = "";
                if (it->parameters.count("ranges")) {
                    if (auto* str_val = std::get_if<std::string>(&it->parameters.at("ranges"))) {
                        old_value = *str_val;
                    }
                }
                
                // Use project_io to modify parameters
                auto updated_params = it->parameters;
                updated_params["ranges"] = decision.mapping_spec;
                project_io::set_node_parameters(project, field_map_node_id, updated_params);
                
                updated_count++;
                ORC_LOG_INFO("Updated node '{}' ranges parameter", field_map_node_id);
                ORC_LOG_INFO("  Old value: {}", old_value.empty() ? "(not set)" : old_value);
                ORC_LOG_INFO("  New value: {}", decision.mapping_spec);
            }
        }
        
        if (updated_count == 0) {
            ORC_LOG_ERROR("Failed to update any field_map nodes");
            return 1;
        }
        
        ORC_LOG_DEBUG("About to save project with {} updated nodes", updated_count);
        
        // Save the project using the built-in save function
        try {
            orc::project_io::save_project(project, options.project_path);
            ORC_LOG_INFO("Project file updated successfully: {}", options.project_path);
        } catch (const std::exception& e) {
            ORC_LOG_ERROR("Failed to save project file: {}", e.what());
            return 1;
        }
    }
    
    return 0;
}

} // namespace cli
} // namespace orc
