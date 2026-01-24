/*
 * File:        field_mapping_range_analysis.cpp
 * Module:      orc-core/analysis
 * Purpose:     Field mapping range analysis tool implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "field_mapping_range_analysis.h"
#include "../analysis_registry.h"
#include <optional>
#include "../../include/video_field_representation.h"
#include "../../include/dag_executor.h"
#include "../../include/project.h"
#include "../../include/logging.h"
#include "../../observers/biphase_observer.h"
#include "../../include/observation_context.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <regex>

namespace orc {

// Force linker to include this object file (for static registration)
void force_link_FieldMappingRangeAnalysisTool() {}

namespace {

std::optional<ParsedTimecode> parse_timecode_string(const std::string& timecode_str) {
    // Accept H:MM:SS.FF (flexible digits) where FF is picture number within second
    std::regex tc_regex(R"((\d+):(\d{1,2}):(\d{1,2})\.(\d{1,2}))");
    std::smatch match;
    if (!std::regex_match(timecode_str, match, tc_regex)) {
        return std::nullopt;
    }

    ParsedTimecode tc;
    tc.hours = std::stoi(match[1]);
    tc.minutes = std::stoi(match[2]);
    tc.seconds = std::stoi(match[3]);
    tc.picture_number = std::stoi(match[4]);

    if (!tc.is_valid()) {
        return std::nullopt;
    }
    return tc;
}

int32_t timecode_to_frame_count(const ParsedTimecode& tc, bool is_pal) {
    const int frames_per_second = is_pal ? 25 : 30;  // NTSC VBI uses 30 nominal
    const int total_seconds = tc.hours * 3600 + tc.minutes * 60 + tc.seconds;
    return total_seconds * frames_per_second + tc.picture_number;
}

std::optional<int32_t> get_int_observation(const ObservationContext& ctx,
                                           FieldID field_id,
                                           const std::string& ns,
                                           const std::string& key) {
    auto val = ctx.get(field_id, ns, key);
    if (!val || !std::holds_alternative<int32_t>(*val)) {
        return std::nullopt;
    }
    return std::get<int32_t>(*val);
}

}

std::vector<ParameterDescriptor> FieldMappingRangeAnalysisTool::parameters() const {
    std::vector<ParameterDescriptor> params;

    // Mode selection
    ParameterDescriptor mode;
    mode.name = "mode";
    mode.display_name = "Input Mode";
    mode.description = "Choose how to specify ranges";
    mode.type = ParameterType::STRING;
    mode.constraints.allowed_strings = {"Field IDs", "Picture Numbers", "Timecodes"};
    mode.constraints.default_value = std::string("Field IDs");
    params.push_back(mode);

    // Field ID range (Field IDs mode)
    {
        ParameterDescriptor start_field;
        start_field.name = "startFieldID";
        start_field.display_name = "Start field ID";
        start_field.description = "Starting field ID (inclusive)";
        start_field.type = ParameterType::INT32;
        start_field.constraints.default_value = static_cast<int32_t>(0);
        ParameterDependency dep;
        dep.parameter_name = "mode";
        dep.required_values = {"Field IDs"};
        start_field.constraints.depends_on = dep;
        params.push_back(start_field);
    }
    {
        ParameterDescriptor end_field;
        end_field.name = "endFieldID";
        end_field.display_name = "End field ID";
        end_field.description = "Ending field ID (inclusive)";
        end_field.type = ParameterType::INT32;
        end_field.constraints.default_value = static_cast<int32_t>(0);
        ParameterDependency dep;
        dep.parameter_name = "mode";
        dep.required_values = {"Field IDs"};
        end_field.constraints.depends_on = dep;
        params.push_back(end_field);
    }

    // Picture number range (CAV mode)
    {
        ParameterDescriptor start_picture;
        start_picture.name = "startPicture";
        start_picture.display_name = "Start picture number";
        start_picture.description = "Starting picture number (CAV discs)";
        start_picture.type = ParameterType::INT32;
        start_picture.constraints.default_value = static_cast<int32_t>(1);
        start_picture.constraints.min_value = static_cast<int32_t>(1);
        ParameterDependency pic_dep;
        pic_dep.parameter_name = "mode";
        pic_dep.required_values = {"Picture Numbers"};
        start_picture.constraints.depends_on = pic_dep;
        params.push_back(start_picture);
    }
    {
        ParameterDescriptor end_picture;
        end_picture.name = "endPicture";
        end_picture.display_name = "End picture number";
        end_picture.description = "Ending picture number (CAV discs)";
        end_picture.type = ParameterType::INT32;
        end_picture.constraints.default_value = static_cast<int32_t>(1);
        end_picture.constraints.min_value = static_cast<int32_t>(1);
        ParameterDependency pic_dep;
        pic_dep.parameter_name = "mode";
        pic_dep.required_values = {"Picture Numbers"};
        end_picture.constraints.depends_on = pic_dep;
        params.push_back(end_picture);
    }

    // Timecode range (CLV mode)
    {
        ParameterDescriptor start_timecode;
        start_timecode.name = "startTimecode";
        start_timecode.display_name = "Start time-code";
        start_timecode.description = "Starting time-code in H:MM:SS.FF format (CLV discs)";
        start_timecode.type = ParameterType::STRING;
        start_timecode.constraints.default_value = std::string("0:00:00.00");
        ParameterDependency tc_dep;
        tc_dep.parameter_name = "mode";
        tc_dep.required_values = {"Timecodes"};
        start_timecode.constraints.depends_on = tc_dep;
        params.push_back(start_timecode);
    }
    {
        ParameterDescriptor end_timecode;
        end_timecode.name = "endTimecode";
        end_timecode.display_name = "End time-code";
        end_timecode.description = "Ending time-code in H:MM:SS.FF format (CLV discs)";
        end_timecode.type = ParameterType::STRING;
        end_timecode.constraints.default_value = std::string("0:00:00.00");
        ParameterDependency tc_dep;
        tc_dep.parameter_name = "mode";
        tc_dep.required_values = {"Timecodes"};
        end_timecode.constraints.depends_on = tc_dep;
        params.push_back(end_timecode);
    }

    return params;
}

std::vector<ParameterDescriptor> FieldMappingRangeAnalysisTool::parametersForContext(const AnalysisContext& ctx) const {
    (void)ctx;
    return parameters();
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

    bool is_pal = false;
    if (auto first_desc = source->get_descriptor(source->field_range().start)) {
        is_pal = (first_desc->format == VideoFormat::PAL);
    }

    // Get parameters
    std::string mode = "Field IDs";
    auto mode_param = ctx.parameters.find("mode");
    if (mode_param != ctx.parameters.end() && std::holds_alternative<std::string>(mode_param->second)) {
        mode = std::get<std::string>(mode_param->second);
    }

    if (mode == "Field IDs") {
        // Use direct field ID specification
        int32_t start_field = 0;
        int32_t end_field = 0;

        auto start_param = ctx.parameters.find("startFieldID");
        if (start_param != ctx.parameters.end()) {
            if (std::holds_alternative<int32_t>(start_param->second)) {
                start_field = std::get<int32_t>(start_param->second);
            } else if (std::holds_alternative<int>(start_param->second)) {
                start_field = std::get<int>(start_param->second);
            }
        }

        auto end_param = ctx.parameters.find("endFieldID");
        if (end_param != ctx.parameters.end()) {
            if (std::holds_alternative<int32_t>(end_param->second)) {
                end_field = std::get<int32_t>(end_param->second);
            } else if (std::holds_alternative<int>(end_param->second)) {
                end_field = std::get<int>(end_param->second);
            }
        }

        // Validate against source range
        auto field_range = source->field_range();
        if (!field_range.is_valid()) {
            result.status = AnalysisResult::Failed;
            result.summary = "Invalid source field range";
            return result;
        }

        if (start_field < static_cast<int32_t>(field_range.start.value()) ||
            end_field < start_field ||
            end_field >= static_cast<int32_t>(field_range.end.value())) {
            result.status = AnalysisResult::Failed;
            result.summary = "Invalid field ID range";
            return result;
        }

        if (progress) {
            progress->setStatus("Generating field mapping specification...");
            progress->setProgress(90);
        }

        // Build ranges spec (inclusive)
        std::ostringstream ranges_spec;
        ranges_spec << start_field << "-" << end_field;

        result.status = AnalysisResult::Success;
        result.summary = "Field ID range mapped";
        result.graphData["ranges"] = ranges_spec.str();

        AnalysisResult::ResultItem item;
        item.type = "info";
        item.message = "Field Range: " + ranges_spec.str();
        item.metadata["range"] = ranges_spec.str();
        item.metadata["field_count"] = static_cast<long long>(end_field - start_field + 1);
        result.items.push_back(item);

        result.statistics["totalFields"] = static_cast<long long>(end_field - start_field + 1);
        result.statistics["startField"] = static_cast<long long>(start_field);
        result.statistics["endField"] = static_cast<long long>(end_field);

        if (progress) {
            progress->setStatus("Complete");
            progress->setProgress(100);
        }
        return result;
    }
    else if (mode == "Picture Numbers") {
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
            return result;
        }

        if (progress) {
            progress->setStatus("Scanning for picture numbers...");
            progress->setProgress(60);
        }

        std::optional<FieldID> start_field;
        std::optional<FieldID> end_field;

        BiphaseObserver observer;
        ObservationContext observation_context;

        auto field_range = source->field_range();
        const int64_t max_scan_fields = 1000;
        int64_t scanned = 0;
        bool found_any = false;

        for (FieldID fid = field_range.start; fid < field_range.end; ++fid, ++scanned) {
            if (!found_any && scanned >= max_scan_fields) {
                break;
            }

            observer.process_field(*source, fid, observation_context);
            auto pic_opt = get_int_observation(observation_context, fid, "vbi", "picture_number");
            if (!pic_opt) {
                continue;
            }
            int32_t pic = *pic_opt;
            found_any = true;  // Mark that we found at least one picture number
            
            // Find the field with the start picture number
            if (!start_field && pic == start_picture) {
                start_field = fid;
            }
            
            // Find the field with the end picture number (after start is found)
            if (start_field && pic == end_picture) {
                end_field = fid;
                break;  // Found both, can stop scanning
            }
        }

        if (!start_field || !end_field) {
            result.status = AnalysisResult::Failed;
            if (!found_any && scanned >= max_scan_fields) {
                result.summary = "No picture number data found in the first 1000 fields";
            } else {
                result.summary = "No picture number data in requested range";
            }
            return result;
        }

        std::ostringstream ranges_spec;
        ranges_spec << start_field->value() << "-" << end_field->value();

        result.status = AnalysisResult::Success;
        result.summary = "Picture number range mapped";
        result.graphData["ranges"] = ranges_spec.str();

        AnalysisResult::ResultItem item;
        item.type = "info";
        item.message = "Picture Range: " + ranges_spec.str();
        item.metadata["range"] = ranges_spec.str();
        result.items.push_back(item);

        result.statistics["startPicture"] = static_cast<long long>(start_picture);
        result.statistics["endPicture"] = static_cast<long long>(end_picture);
        result.statistics["startField"] = static_cast<long long>(start_field->value());
        result.statistics["endField"] = static_cast<long long>(end_field->value());

        if (progress) {
            progress->setStatus("Generating field mapping specification...");
            progress->setProgress(90);
        }

        if (progress) {
            progress->setStatus("Complete");
            progress->setProgress(100);
        }
        return result;
    }
    else { // Timecodes
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

        auto start_tc = parse_timecode_string(start_tc_str);
        auto end_tc = parse_timecode_string(end_tc_str);
        if (!start_tc || !end_tc) {
            result.status = AnalysisResult::Failed;
            result.summary = "Invalid timecode format (expected H:MM:SS.FF)";
            return result;
        }

        const int32_t start_frame = timecode_to_frame_count(*start_tc, is_pal);
        const int32_t end_frame = timecode_to_frame_count(*end_tc, is_pal);
        if (start_frame > end_frame) {
            result.status = AnalysisResult::Failed;
            result.summary = "Start timecode is after end timecode";
            return result;
        }

        if (progress) {
            progress->setStatus("Scanning for timecodes...");
            progress->setProgress(60);
        }

        std::optional<FieldID> start_field;
        std::optional<FieldID> end_field;

        BiphaseObserver observer;
        ObservationContext observation_context;

        auto field_range = source->field_range();
        const int64_t max_scan_fields = 1000;
        int64_t scanned = 0;
        bool found_any = false;

        for (FieldID fid = field_range.start; fid < field_range.end; ++fid, ++scanned) {
            if (!found_any && scanned >= max_scan_fields) {
                break;
            }

            observer.process_field(*source, fid, observation_context);
            auto h = get_int_observation(observation_context, fid, "vbi", "clv_timecode_hours");
            auto m = get_int_observation(observation_context, fid, "vbi", "clv_timecode_minutes");
            auto s = get_int_observation(observation_context, fid, "vbi", "clv_timecode_seconds");
            auto p = get_int_observation(observation_context, fid, "vbi", "clv_timecode_picture");
            if (!h || !m || !s || !p) {
                continue;
            }
            ParsedTimecode tc{*h, *m, *s, *p};
            if (!tc.is_valid()) {
                continue;
            }
            found_any = true;  // Mark that we found at least one valid timecode
            
            int32_t frame_val = timecode_to_frame_count(tc, is_pal);
            
            // Find the field with the start timecode
            if (!start_field && frame_val == start_frame) {
                start_field = fid;
            }
            
            // Find the field with the end timecode (after start is found)
            if (start_field && frame_val == end_frame) {
                end_field = fid;
                break;  // Found both, can stop scanning
            }
        }

        if (!start_field || !end_field) {
            result.status = AnalysisResult::Failed;
            if (!found_any && scanned >= max_scan_fields) {
                result.summary = "No timecode data found in the first 1000 fields";
            } else {
                result.summary = "No timecode data in requested range";
            }
            return result;
        }

        std::ostringstream ranges_spec;
        ranges_spec << start_field->value() << "-" << end_field->value();

        result.status = AnalysisResult::Success;
        result.summary = "Timecode range mapped";
        result.graphData["ranges"] = ranges_spec.str();

        AnalysisResult::ResultItem item;
        item.type = "info";
        item.message = "Timecode Range: " + ranges_spec.str();
        item.metadata["range"] = ranges_spec.str();
        result.items.push_back(item);

        result.statistics["startField"] = static_cast<long long>(start_field->value());
        result.statistics["endField"] = static_cast<long long>(end_field->value());

        if (progress) {
            progress->setStatus("Generating field mapping specification...");
            progress->setProgress(90);
        }

        if (progress) {
            progress->setStatus("Complete");
            progress->setProgress(100);
        }
        return result;
    }
}

bool FieldMappingRangeAnalysisTool::applyToGraph(AnalysisResult& result,
                                                const Project& project,
                                                NodeID node_id) {
    // Expect graphData["ranges"] containing inclusive range spec for FieldMapStage
    auto ranges_it = result.graphData.find("ranges");
    if (ranges_it == result.graphData.end()) {
        ORC_LOG_ERROR("FieldMappingRangeAnalysisTool::applyToGraph - no ranges in result");
        return false;
    }

    // Locate node
    const auto& nodes = project.get_nodes();
    auto node_it = std::find_if(nodes.begin(), nodes.end(),
        [&node_id](const ProjectDAGNode& n) { return n.node_id == node_id; });
    if (node_it == nodes.end()) {
        ORC_LOG_ERROR("FieldMappingRangeAnalysisTool::applyToGraph - node not found: {}", node_id);
        return false;
    }

    // Populate parameterChanges instead of modifying project directly
    result.parameterChanges["ranges"] = ranges_it->second;

    ORC_LOG_INFO("Prepared field mapping range '{}' for node {}", ranges_it->second, node_id);
    return true;
}

// Register the tool
REGISTER_ANALYSIS_TOOL(FieldMappingRangeAnalysisTool);

} // namespace orc
