/*
 * File:        field_mapping_range_analysis.cpp
 * Module:      orc-core/analysis
 * Purpose:     Field mapping range analysis tool implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "field_mapping_range_analysis.h"
#include "../analysis_registry.h"
#include "field_mapping_lookup.h"
#include "../../include/video_field_representation.h"
#include "../../include/dag_executor.h"
#include "../../include/project.h"
#include "../../include/logging.h"
#include <iostream>
#include <sstream>
#include <algorithm>

namespace orc {

std::vector<ParameterDescriptor> FieldMappingRangeAnalysisTool::parameters() const {
    std::vector<ParameterDescriptor> params;
    
    // Mode selection: use picture numbers or timecodes
    ParameterDescriptor mode;
    mode.name = "mode";
    mode.display_name = "Input Mode";
    mode.description = "Choose whether to specify ranges using picture numbers (CAV) or timecodes (CLV)";
    mode.type = ParameterType::STRING;
    mode.constraints.allowed_strings = {"Picture Numbers", "Timecodes"};
    mode.constraints.default_value = std::string("Picture Numbers");
    params.push_back(mode);
    
    // Picture number range (CAV mode)
    ParameterDescriptor start_picture;
    start_picture.name = "startPicture";
    start_picture.display_name = "Start picture number";
    start_picture.description = "Starting picture number (CAV discs)";
    start_picture.type = ParameterType::INT32;
    start_picture.constraints.default_value = static_cast<int32_t>(1);
    start_picture.constraints.min_value = static_cast<int32_t>(1);
    // Only enable when mode is "Picture Numbers"
    ParameterDependency pic_dep;
    pic_dep.parameter_name = "mode";
    pic_dep.required_values = {"Picture Numbers"};
    start_picture.constraints.depends_on = pic_dep;
    params.push_back(start_picture);
    
    ParameterDescriptor end_picture;
    end_picture.name = "endPicture";
    end_picture.display_name = "End picture number";
    end_picture.description = "Ending picture number (CAV discs)";
    end_picture.type = ParameterType::INT32;
    end_picture.constraints.default_value = static_cast<int32_t>(1);
    end_picture.constraints.min_value = static_cast<int32_t>(1);
    // Only enable when mode is "Picture Numbers"
    end_picture.constraints.depends_on = pic_dep;
    params.push_back(end_picture);
    
    // Timecode range (CLV mode)
    ParameterDescriptor start_timecode;
    start_timecode.name = "startTimecode";
    start_timecode.display_name = "Start time-code";
    start_timecode.description = "Starting time-code in H:MM:SS.FF format (CLV discs)";
    start_timecode.type = ParameterType::STRING;
    start_timecode.constraints.default_value = std::string("0:00:00.00");
    // Only enable when mode is "Timecodes"
    ParameterDependency tc_dep;
    tc_dep.parameter_name = "mode";
    tc_dep.required_values = {"Timecodes"};
    start_timecode.constraints.depends_on = tc_dep;
    params.push_back(start_timecode);
    
    ParameterDescriptor end_timecode;
    end_timecode.name = "endTimecode";
    end_timecode.display_name = "End time-code";
    end_timecode.description = "Ending time-code in H:MM:SS.FF format (CLV discs)";
    end_timecode.type = ParameterType::STRING;
    end_timecode.constraints.default_value = std::string("0:00:00.00");
    // Only enable when mode is "Timecodes"
    end_timecode.constraints.depends_on = tc_dep;
    params.push_back(end_timecode);
    
    return params;
}

bool FieldMappingRangeAnalysisTool::canAnalyze(AnalysisSourceType source_type) const {
    return source_type == AnalysisSourceType::LaserDisc;
}

bool FieldMappingRangeAnalysisTool::isApplicableToStage(const std::string& stage_name) const {
    return stage_name == "field_map";
}

AnalysisResult FieldMappingRangeAnalysisTool::analyze(const AnalysisContext& ctx,
                                                      AnalysisProgress* progress) {
    AnalysisResult result;
    
    if (progress) {
        progress->setStatus("Initializing field range analysis...");
        progress->setProgress(0);
    }
    
    // Get the DAG and project from context
    if (!ctx.dag || !ctx.project) {
        result.status = AnalysisResult::Failed;
        result.summary = "No DAG or project provided for analysis";
        ORC_LOG_ERROR("Field range analysis requires DAG and project in context");
        return result;
    }
    
    // Find the field_map node in the DAG
    const auto& dag_nodes = ctx.dag->nodes();
    auto node_it = std::find_if(dag_nodes.begin(), dag_nodes.end(),
        [&ctx](const DAGNode& node) { return node.node_id == ctx.node_id; });
    
    if (node_it == dag_nodes.end()) {
        result.status = AnalysisResult::Failed;
        result.summary = "Node not found in DAG";
        ORC_LOG_ERROR("Node '{}' not found in DAG", ctx.node_id);
        return result;
    }
    
    const auto& node = *node_it;
    
    // Get the input node ID
    if (node.input_node_ids.empty()) {
        result.status = AnalysisResult::Failed;
        result.summary = "Field map node has no input connected";
        ORC_LOG_ERROR("Field map node '{}' has no input", ctx.node_id);
        return result;
    }
    
    NodeID input_node_id = node.input_node_ids[0];
    ORC_LOG_DEBUG("Node '{}': Field range analysis - getting input from node '{}'", ctx.node_id, input_node_id);
    
    // Execute DAG to get the VideoFieldRepresentation from the input node
    DAGExecutor executor;
    std::shared_ptr<VideoFieldRepresentation> source;
    
    try {
        auto all_outputs = executor.execute_to_node(*ctx.dag, input_node_id);
        
        auto output_it = all_outputs.find(input_node_id);
        if (output_it == all_outputs.end() || output_it->second.empty()) {
            result.status = AnalysisResult::Failed;
            result.summary = "Input node produced no outputs";
            ORC_LOG_ERROR("Node '{}': Input node '{}' produced no outputs", ctx.node_id, input_node_id);
            return result;
        }
        
        // Find the VideoFieldRepresentation output
        for (const auto& artifact : output_it->second) {
            source = std::dynamic_pointer_cast<VideoFieldRepresentation>(artifact);
            if (source) {
                break;
            }
        }
        
        if (!source) {
            result.status = AnalysisResult::Failed;
            result.summary = "Input node did not produce VideoFieldRepresentation";
            ORC_LOG_ERROR("Node '{}': Input node '{}' did not produce VideoFieldRepresentation", ctx.node_id, input_node_id);
            return result;
        }
        
        ORC_LOG_DEBUG("Got VideoFieldRepresentation with {} fields", source->field_range().size());
    } catch (const std::exception& e) {
        result.status = AnalysisResult::Failed;
        result.summary = std::string("Failed to execute DAG: ") + e.what();
        ORC_LOG_ERROR("Field range analysis failed: {}", e.what());
        return result;
    }
    
    if (progress) {
        progress->setStatus("Analyzing VBI data...");
        progress->setProgress(30);
    }
    
    // Create the lookup helper
    FieldMappingLookup lookup(*source);
    
    // Get parameters
    std::string mode = "Picture Numbers";
    auto mode_param = ctx.parameters.find("mode");
    if (mode_param != ctx.parameters.end() && std::holds_alternative<std::string>(mode_param->second)) {
        mode = std::get<std::string>(mode_param->second);
    }
    
    FieldLookupResult lookup_result;
    std::ostringstream summary;
    
    if (mode == "Picture Numbers") {
        // CAV mode - use picture numbers
        int32_t start_picture = 1;
        int32_t end_picture = 1;
        
        auto start_param = ctx.parameters.find("startPicture");
        if (start_param != ctx.parameters.end()) {
            if (std::holds_alternative<int32_t>(start_param->second)) {
                start_picture = std::get<int32_t>(start_param->second);
            } else if (std::holds_alternative<int>(start_param->second)) {
                start_picture = std::get<int>(start_param->second);
            }
        }
        
        auto end_param = ctx.parameters.find("endPicture");
        if (end_param != ctx.parameters.end()) {
            if (std::holds_alternative<int32_t>(end_param->second)) {
                end_picture = std::get<int32_t>(end_param->second);
            } else if (std::holds_alternative<int>(end_param->second)) {
                end_picture = std::get<int>(end_param->second);
            }
        }
        
        if (start_picture <= 0 || end_picture <= 0 || start_picture > end_picture) {
            result.status = AnalysisResult::Failed;
            result.summary = "Invalid picture number range";
            ORC_LOG_ERROR("Invalid picture number range: {}-{}", start_picture, end_picture);
            return result;
        }
        
        if (progress) {
            progress->setStatus("Converting picture numbers to fields...");
            progress->setProgress(60);
        }
        
        lookup_result = lookup.get_fields_for_frame_range(start_picture, end_picture, true);
        
        if (!lookup_result.success) {
            result.status = AnalysisResult::Failed;
            result.summary = lookup_result.error_message;
            ORC_LOG_ERROR("Lookup failed: {}", lookup_result.error_message);
            return result;
        }
        
        summary << "Converted picture numbers " << start_picture << "-" << end_picture 
                << " to field IDs " << lookup_result.start_field_id.value() 
                << "-" << lookup_result.end_field_id.value();
        
    } else {
        // CLV mode - use timecodes
        std::string start_tc_str = "0:00:00.00";
        std::string end_tc_str = "0:00:00.00";
        
        auto start_param = ctx.parameters.find("startTimecode");
        if (start_param != ctx.parameters.end() && std::holds_alternative<std::string>(start_param->second)) {
            start_tc_str = std::get<std::string>(start_param->second);
        }
        
        auto end_param = ctx.parameters.find("endTimecode");
        if (end_param != ctx.parameters.end() && std::holds_alternative<std::string>(end_param->second)) {
            end_tc_str = std::get<std::string>(end_param->second);
        }
        
        auto start_tc = FieldMappingLookup::parse_timecode(start_tc_str);
        auto end_tc = FieldMappingLookup::parse_timecode(end_tc_str);
        
        if (!start_tc || !end_tc) {
            result.status = AnalysisResult::Failed;
            result.summary = "Invalid timecode format (expected H:MM:SS.FF)";
            ORC_LOG_ERROR("Invalid timecode format");
            return result;
        }
        
        if (progress) {
            progress->setStatus("Converting timecodes to fields...");
            progress->setProgress(60);
        }
        
        lookup_result = lookup.get_fields_for_timecode_range(*start_tc, *end_tc);
        
        if (!lookup_result.success) {
            result.status = AnalysisResult::Failed;
            result.summary = lookup_result.error_message;
            ORC_LOG_ERROR("Lookup failed: {}", lookup_result.error_message);
            return result;
        }
        
        summary << "Converted timecodes " << start_tc_str << "-" << end_tc_str 
                << " to field IDs " << lookup_result.start_field_id.value() 
                << "-" << lookup_result.end_field_id.value();
    }
    
    if (progress) {
        progress->setStatus("Generating field mapping specification...");
        progress->setProgress(90);
    }
    
    // Build the ranges parameter string (use exclusive end as per field_map convention)
    std::ostringstream ranges_spec;
    ranges_spec << lookup_result.start_field_id.value() << "-" << (lookup_result.end_field_id.value() - 1);
    
    result.status = AnalysisResult::Success;
    result.summary = summary.str();
    
    // Store the ranges spec for application to the graph
    result.graphData["ranges"] = ranges_spec.str();
    
    // Add details as result items
    AnalysisResult::ResultItem item;
    item.type = "info";
    item.message = "Field Range: " + ranges_spec.str();
    item.metadata["range"] = ranges_spec.str();
    item.metadata["field_count"] = static_cast<long long>(lookup_result.end_field_id.value() - lookup_result.start_field_id.value());
    result.items.push_back(item);
    
    // Statistics
    result.statistics["totalFields"] = static_cast<long long>(lookup_result.end_field_id.value() - lookup_result.start_field_id.value());
    result.statistics["startField"] = static_cast<long long>(lookup_result.start_field_id.value());
    result.statistics["endField"] = static_cast<long long>(lookup_result.end_field_id.value() - 1);
    result.statistics["discType"] = lookup_result.is_cav ? "CAV" : "CLV";
    result.statistics["videoFormat"] = lookup_result.is_pal ? "PAL" : "NTSC";
    
    if (progress) {
        progress->setStatus("Complete");
        progress->setProgress(100);
    }
    
    ORC_LOG_DEBUG("Field range analysis complete - {} fields", 
                 lookup_result.end_field_id.value() - lookup_result.start_field_id.value());
    return result;
}

bool FieldMappingRangeAnalysisTool::applyToGraph(const AnalysisResult& result,
                                                Project& project,
                                                NodeID node_id) {
    // Find the ranges spec in the result
    auto ranges_it = result.graphData.find("ranges");
    if (ranges_it == result.graphData.end()) {
        ORC_LOG_ERROR("FieldMappingRangeAnalysisTool::applyToGraph - No ranges specification in result");
        std::cerr << "No ranges specification in result" << std::endl;
        return false;
    }
    
    std::string ranges_spec = ranges_it->second;
    
    // Find the target node in the project
    const auto& nodes = project.get_nodes();
    auto node_it = std::find_if(nodes.begin(), nodes.end(),
        [&node_id](const ProjectDAGNode& n) { return n.node_id == node_id; });
    
    if (node_it == nodes.end()) {
        ORC_LOG_ERROR("FieldMappingRangeAnalysisTool::applyToGraph - node not found: {}", node_id.value());
        std::cerr << "Node not found: " << node_id.value() << std::endl;
        return false;
    }
    
    ORC_LOG_INFO("Node '{}': Applying field range results", node_id);
    if (node_it->parameters.count("ranges")) {
        auto& old_value = node_it->parameters.at("ranges");
        if (auto* str_val = std::get_if<std::string>(&old_value)) {
            ORC_LOG_INFO("Node '{}':   Old ranges parameter: {}", node_id, *str_val);
        }
    } else {
        ORC_LOG_INFO("Node '{}':   Old ranges parameter: (not set)", node_id);
    }
    ORC_LOG_INFO("Node '{}':   New ranges: {}", node_id, ranges_spec);
    
    std::cout << "Applying field range to node " << node_id.value() << std::endl;
    std::cout << "  Ranges: " << ranges_spec << std::endl;
    
    // Update the node's ranges parameter
    auto updated_params = node_it->parameters;
    updated_params["ranges"] = ranges_spec;
    project_io::set_node_parameters(project, node_id, updated_params);
    
    ORC_LOG_DEBUG("Successfully applied field range to field_map 'ranges' parameter");
    std::cout << "Successfully applied field range to field_map 'ranges' parameter" << std::endl;
    return true;
}

// Register the tool
REGISTER_ANALYSIS_TOOL(FieldMappingRangeAnalysisTool);

} // namespace orc
