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
    
    ORC_LOG_INFO("Project loaded: {} (version {})", project.name, project.version);
    
    // Convert project to DAG
    auto dag = orc::project_to_dag(project);
    if (!dag) {
        ORC_LOG_ERROR("Failed to convert project to DAG");
        return 1;
    }
    
    // Find source node (any SOURCE type node)
    std::string source_node_id;
    for (const auto& node : project.nodes) {
        if (node.node_type == orc::NodeType::SOURCE) {
            source_node_id = node.node_id;
            break;
        }
    }
    
    if (source_node_id.empty()) {
        ORC_LOG_ERROR("No SOURCE node found in project");
        return 1;
    }
    
    ORC_LOG_INFO("Found source node: {}", source_node_id);
    
    // Execute DAG to get source representation
    DAGExecutor executor;
    auto results = executor.execute_to_node(*dag, source_node_id);
    
    auto source_it = results.find(source_node_id);
    if (source_it == results.end() || source_it->second.empty()) {
        ORC_LOG_ERROR("Failed to execute source node");
        return 1;
    }
    
    auto source_artifact = source_it->second[0];
    auto* video_rep = dynamic_cast<VideoFieldRepresentation*>(source_artifact.get());
    if (!video_rep) {
        ORC_LOG_ERROR("Source node did not produce VideoFieldRepresentation");
        return 1;
    }
    
    ORC_LOG_INFO("Running field mapping analysis...");
    
    // Run field mapping analysis
    FieldMappingAnalyzer analyzer;
    FieldMappingAnalyzer::Options analyzer_options;
    analyzer_options.pad_gaps = options.pad_gaps;
    analyzer_options.delete_unmappable_frames = options.delete_unmappable;
    
    auto decision = analyzer.analyze(*video_rep, analyzer_options);
    
    if (!decision.success) {
        ORC_LOG_ERROR("Field mapping analysis failed: {}", decision.rationale);
        return 1;
    }
    
    ORC_LOG_INFO("Field mapping analysis successful");
    ORC_LOG_INFO("Mapping specification: {}", decision.mapping_spec);
    ORC_LOG_INFO("");
    ORC_LOG_INFO("Statistics:");
    ORC_LOG_INFO("  correctedVBIErrors: {}", decision.stats.corrected_vbi_errors);
    ORC_LOG_INFO("  discType: {}", decision.is_cav ? "CAV" : "CLV");
    ORC_LOG_INFO("  gapsPadded: {}", decision.stats.gaps_padded);
    ORC_LOG_INFO("  paddingFrames: {}", decision.stats.padding_frames);
    ORC_LOG_INFO("  pulldownFrames: {}", decision.stats.pulldown_frames);
    ORC_LOG_INFO("  removedDuplicates: {}", decision.stats.removed_duplicates);
    ORC_LOG_INFO("  removedInvalidPhase: {}", decision.stats.removed_invalid_phase);
    ORC_LOG_INFO("  removedLeadInOut: {}", decision.stats.removed_lead_in_out);
    ORC_LOG_INFO("  removedUnmappable: {}", decision.stats.removed_unmappable);
    ORC_LOG_INFO("  totalFields: {}", decision.stats.total_fields);
    ORC_LOG_INFO("  videoFormat: {}", decision.is_pal ? "PAL" : "NTSC");
    
    // Update project file if requested
    if (options.update_project) {
        ORC_LOG_INFO("");
        ORC_LOG_INFO("Updating project file with mapping specification...");
        
        // Find field mapper node
        std::string field_mapper_node_id;
        for (const auto& node : project.nodes) {
            if (node.stage_name == "field_map") {
                field_mapper_node_id = node.node_id;
                break;
            }
        }
        
        if (field_mapper_node_id.empty()) {
            ORC_LOG_ERROR("No field_map node found in project");
            return 1;
        }
        
        // Update the field mapper node's ranges parameter in the project structure
        bool updated = false;
        for (auto& node : project.nodes) {
            if (node.node_id == field_mapper_node_id) {
                // Update the ranges parameter (ParameterValue is a variant, just assign the string)
                node.parameters["ranges"] = decision.mapping_spec;
                updated = true;
                ORC_LOG_INFO("Updated node '{}' ranges parameter", field_mapper_node_id);
                break;
            }
        }
        
        if (!updated) {
            ORC_LOG_ERROR("Failed to find node '{}' in project", field_mapper_node_id);
            return 1;
        }
        
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
