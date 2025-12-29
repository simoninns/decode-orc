/*
 * File:        command_analyze_source_aligns.cpp
 * Module:      orc-cli
 * Purpose:     Analyze source alignment for all source_align stages
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "command_analyze_source_aligns.h"
#include "project.h"
#include "project_to_dag.h"
#include "dag_executor.h"
#include "stage_registry.h"
#include "analysis/source_alignment/source_alignment_analysis.h"
#include "analysis/analysis_registry.h"
#include "logging.h"

#include <iostream>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

namespace orc {
namespace cli {

int analyze_source_aligns_command(const AnalyzeSourceAlignsOptions& options) {
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
    
    // Find all source_align nodes
    std::vector<orc::NodeID> source_align_node_ids;
    for (const auto& node : project.get_nodes()) {
        if (node.stage_name == "source_align") {
            source_align_node_ids.push_back(node.node_id);
        }
    }
    
    if (source_align_node_ids.empty()) {
        ORC_LOG_ERROR("No source_align nodes found in project");
        return 1;
    }
    
    ORC_LOG_INFO("Found {} source_align node(s)", source_align_node_ids.size());
    
    // Get the analysis tool from registry
    auto& analysis_registry = AnalysisRegistry::instance();
    auto* tool = analysis_registry.findById("source_alignment");
    if (!tool) {
        ORC_LOG_ERROR("Source alignment analysis tool not found in registry");
        return 1;
    }
    
    // Analyze each source_align node
    std::map<orc::NodeID, AnalysisResult> results;
    int success_count = 0;
    
    for (const auto& node_id : source_align_node_ids) {
        ORC_LOG_INFO("");
        ORC_LOG_INFO("=== Analyzing source_align node: {} ===", node_id);
        
        // Set up analysis context
        AnalysisContext ctx;
        ctx.node_id = node_id;
        ctx.dag = dag;
        ctx.project = std::make_shared<Project>(project);
        
        // Run analysis
        try {
            auto result = tool->analyze(ctx, nullptr);
            results[node_id] = result;
            
            if (result.status == AnalysisResult::Success) {
                ORC_LOG_INFO("Source alignment analysis successful for node {}", node_id);
                ORC_LOG_INFO("");
                ORC_LOG_INFO("{}", result.summary);
                
                // Extract the alignment map from the result
                if (result.graphData.count("alignmentMap")) {
                    const std::string& alignment_map = result.graphData.at("alignmentMap");
                    
                    // Update the node's parameters
                    std::map<std::string, ParameterValue> params;
                    params["alignmentMap"] = alignment_map;
                    
                    try {
                        project_io::set_node_parameters(project, node_id, params);
                        ORC_LOG_INFO("Updated node '{}' alignmentMap parameter to: {}", node_id, alignment_map);
                        success_count++;
                    } catch (const std::exception& e) {
                        ORC_LOG_ERROR("Failed to update node '{}' parameters: {}", node_id, e.what());
                    }
                } else {
                    ORC_LOG_WARN("No alignment map found in analysis result for node {}", node_id);
                }
            } else {
                ORC_LOG_ERROR("Source alignment analysis failed for node {}: {}", node_id, result.summary);
            }
        } catch (const std::exception& e) {
            ORC_LOG_ERROR("Failed to analyze source_align node {}: {}", node_id, e.what());
        }
    }
    
    // Save the updated project
    if (success_count > 0) {
        ORC_LOG_INFO("");
        ORC_LOG_INFO("Saving updated project with {} updated node(s)...", success_count);
        
        try {
            orc::project_io::save_project(project, options.project_path);
            ORC_LOG_INFO("Project file updated successfully: {}", options.project_path);
        } catch (const std::exception& e) {
            ORC_LOG_ERROR("Failed to save project file: {}", e.what());
            return 1;
        }
    } else {
        ORC_LOG_ERROR("No source_align nodes were successfully analyzed");
        return 1;
    }
    
    ORC_LOG_INFO("");
    ORC_LOG_INFO("=== Source alignment analysis complete ===");
    ORC_LOG_INFO("Successfully analyzed and updated {} of {} source_align node(s)", 
                 success_count, source_align_node_ids.size());
    
    return 0;
}

} // namespace cli
} // namespace orc
