#include "field_map_range_analysis.h"
#include "../analysis_registry.h"
#include "../../include/dag_executor.h"
#include "../../include/project.h"
#include "../../include/video_field_representation.h"
#include "../../observers/biphase_observer.h"
#include "logging.h"
#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace orc {

// Force linker to include this object file (for static registration)
void force_link_FieldMapRangeAnalysisTool() {}

// Register the tool
REGISTER_ANALYSIS_TOOL(FieldMapRangeAnalysisTool)

std::string FieldMapRangeAnalysisTool::id() const {
    return "field_map_range";
}

std::string FieldMapRangeAnalysisTool::name() const {
    return "Field Map Range Finder";
}

std::string FieldMapRangeAnalysisTool::description() const {
    return "Find a field range by start/end picture number or CLV timecode and "
           "generate a Field Map range specification.";
}

std::string FieldMapRangeAnalysisTool::category() const {
    return "Diagnostic";
}

std::vector<ParameterDescriptor> FieldMapRangeAnalysisTool::parameters() const {
    std::vector<ParameterDescriptor> params;
    
    ParameterDescriptor start_addr;
    start_addr.name = "startAddress";
    start_addr.display_name = "Start Address";
    start_addr.description = "Start picture number (e.g., '12345') or CLV timecode (e.g., '0:0:0.0').";
    start_addr.type = ParameterType::STRING;
    start_addr.constraints.default_value = std::string("");
    start_addr.constraints.required = true;
    params.push_back(start_addr);
    
    ParameterDescriptor end_addr;
    end_addr.name = "endAddress";
    end_addr.display_name = "End Address";
    end_addr.description = "End picture number (e.g., '12350') or CLV timecode (e.g., '0:0:0.5').";
    end_addr.type = ParameterType::STRING;
    end_addr.constraints.default_value = std::string("");
    end_addr.constraints.required = true;
    params.push_back(end_addr);
    
    return params;
}

bool FieldMapRangeAnalysisTool::canAnalyze(AnalysisSourceType source_type) const {
    return source_type == AnalysisSourceType::LaserDisc;
}

bool FieldMapRangeAnalysisTool::isApplicableToStage(const std::string& stage_name) const {
    return stage_name == "field_map";
}

namespace {

struct ParsedAddress {
    bool ok = false;
    bool is_timecode = false;
    int32_t picture_number = 0;
    std::string normalized;
    std::string error;
};

static std::string trim_copy(const std::string& input) {
    size_t start = 0;
    while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start]))) {
        start++;
    }
    size_t end = input.size();
    while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1]))) {
        end--;
    }
    return input.substr(start, end - start);
}

static bool parse_int32(const std::string& input, int32_t& value) {
    try {
        size_t pos = 0;
        long long parsed = std::stoll(input, &pos, 10);
        if (pos != input.size()) {
            return false;
        }
        if (parsed < std::numeric_limits<int32_t>::min() ||
            parsed > std::numeric_limits<int32_t>::max()) {
            return false;
        }
        value = static_cast<int32_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

static ParsedAddress parse_address(const std::string& input, bool is_pal) {
    ParsedAddress result;
    std::string trimmed = trim_copy(input);
    if (trimmed.empty()) {
        result.error = "Address is empty";
        return result;
    }
    
    if (trimmed.find(':') == std::string::npos) {
        int32_t pn = 0;
        if (!parse_int32(trimmed, pn) || pn <= 0) {
            result.error = "Invalid picture number: " + trimmed;
            return result;
        }
        result.ok = true;
        result.is_timecode = false;
        result.picture_number = pn;
        result.normalized = trimmed;
        return result;
    }
    
    std::vector<std::string> parts;
    std::stringstream ss(trimmed);
    std::string part;
    while (std::getline(ss, part, ':')) {
        parts.push_back(trim_copy(part));
    }
    
    if (parts.size() < 3 || parts.size() > 4) {
        result.error = "Invalid timecode format: " + trimmed;
        return result;
    }
    
    int32_t hours = 0;
    int32_t minutes = 0;
    int32_t seconds = 0;
    int32_t pictures = 0;
    
    if (!parse_int32(parts[0], hours) || !parse_int32(parts[1], minutes)) {
        result.error = "Invalid timecode format: " + trimmed;
        return result;
    }
    
    if (parts.size() == 4) {
        if (!parse_int32(parts[2], seconds) || !parse_int32(parts[3], pictures)) {
            result.error = "Invalid timecode format: " + trimmed;
            return result;
        }
    } else {
        const std::string& sec_part = parts[2];
        size_t dot_pos = sec_part.find('.');
        if (dot_pos == std::string::npos) {
            dot_pos = sec_part.find(';');
        }
        if (dot_pos == std::string::npos) {
            result.error = "Timecode must include picture component (e.g., 0:0:0.0)";
            return result;
        }
        std::string seconds_str = trim_copy(sec_part.substr(0, dot_pos));
        std::string pictures_str = trim_copy(sec_part.substr(dot_pos + 1));
        if (!parse_int32(seconds_str, seconds) || !parse_int32(pictures_str, pictures)) {
            result.error = "Invalid timecode format: " + trimmed;
            return result;
        }
    }
    
    int32_t fps = is_pal ? 25 : 30;
    long long frame_index = static_cast<long long>(hours) * 3600LL * fps +
                            static_cast<long long>(minutes) * 60LL * fps +
                            static_cast<long long>(seconds) * fps +
                            static_cast<long long>(pictures);
    long long picture_number = frame_index + 1;  // 0:0:0.0 = picture number 1
    if (picture_number <= 0 || picture_number > std::numeric_limits<int32_t>::max()) {
        result.error = "Timecode out of range: " + trimmed;
        return result;
    }
    
    std::ostringstream normalized;
    normalized << hours << ":" << minutes << ":" << seconds << "." << pictures;
    result.ok = true;
    result.is_timecode = true;
    result.picture_number = static_cast<int32_t>(picture_number);
    result.normalized = normalized.str();
    return result;
}

static std::optional<int32_t> get_picture_number_from_vbi(
    const ObservationContext& observation_context,
    FieldID field_id,
    bool is_pal)
{
    auto picture_num = observation_context.get(field_id, "vbi", "picture_number");
    if (picture_num && std::holds_alternative<int32_t>(*picture_num)) {
        return std::get<int32_t>(*picture_num);
    }
    
    auto hours_opt = observation_context.get(field_id, "vbi", "clv_timecode_hours");
    auto minutes_opt = observation_context.get(field_id, "vbi", "clv_timecode_minutes");
    auto seconds_opt = observation_context.get(field_id, "vbi", "clv_timecode_seconds");
    auto picture_opt = observation_context.get(field_id, "vbi", "clv_timecode_picture");
    
    if (hours_opt && minutes_opt && seconds_opt && picture_opt) {
        if (std::holds_alternative<int32_t>(*hours_opt) &&
            std::holds_alternative<int32_t>(*minutes_opt) &&
            std::holds_alternative<int32_t>(*seconds_opt) &&
            std::holds_alternative<int32_t>(*picture_opt)) {
            
            int32_t hours = std::get<int32_t>(*hours_opt);
            int32_t minutes = std::get<int32_t>(*minutes_opt);
            int32_t seconds = std::get<int32_t>(*seconds_opt);
            int32_t picture = std::get<int32_t>(*picture_opt);
            int32_t fps = is_pal ? 25 : 30;
            long long frame_index = static_cast<long long>(hours) * 3600LL * fps +
                                    static_cast<long long>(minutes) * 60LL * fps +
                                    static_cast<long long>(seconds) * fps +
                                    static_cast<long long>(picture);
            long long picture_number = frame_index + 1;  // 0:0:0.0 = picture number 1
            if (picture_number > 0 && picture_number <= std::numeric_limits<int32_t>::max()) {
                return static_cast<int32_t>(picture_number);
            }
        }
    }
    
    return std::nullopt;
}

} // namespace

AnalysisResult FieldMapRangeAnalysisTool::analyze(const AnalysisContext& ctx,
                                                  AnalysisProgress* progress) {
    AnalysisResult result;
    
    if (progress) {
        progress->setStatus("Initializing field map range analysis...");
        progress->setProgress(0);
    }
    
    std::string start_input;
    std::string end_input;
    
    auto param_it = ctx.parameters.find("startAddress");
    if (param_it != ctx.parameters.end() && std::holds_alternative<std::string>(param_it->second)) {
        start_input = std::get<std::string>(param_it->second);
    }
    param_it = ctx.parameters.find("endAddress");
    if (param_it != ctx.parameters.end() && std::holds_alternative<std::string>(param_it->second)) {
        end_input = std::get<std::string>(param_it->second);
    }
    
    if (start_input.empty() || end_input.empty()) {
        result.status = AnalysisResult::Failed;
        result.summary = "Start and end addresses are required.";
        return result;
    }
    
    if (!ctx.dag || !ctx.project) {
        result.status = AnalysisResult::Failed;
        result.summary = "No DAG or project provided for analysis";
        ORC_LOG_ERROR("Field map range analysis requires DAG and project in context");
        return result;
    }
    
    const auto& dag_nodes = ctx.dag->nodes();
    auto node_it = std::find_if(dag_nodes.begin(), dag_nodes.end(),
        [&ctx](const DAGNode& node) { return node.node_id == ctx.node_id; });
    
    if (node_it == dag_nodes.end()) {
        result.status = AnalysisResult::Failed;
        result.summary = "Node not found in DAG";
        ORC_LOG_ERROR("Node '{}' not found in DAG", ctx.node_id);
        return result;
    }
    
    if (node_it->input_node_ids.empty()) {
        result.status = AnalysisResult::Failed;
        result.summary = "Field map node has no input connected";
        ORC_LOG_ERROR("Field map node '{}' has no input", ctx.node_id);
        return result;
    }
    
    NodeID input_node_id = node_it->input_node_ids[0];
    ORC_LOG_DEBUG("Node '{}': Field map range analysis - getting input from node '{}'", ctx.node_id, input_node_id);
    
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
    } catch (const std::exception& e) {
        result.status = AnalysisResult::Failed;
        result.summary = "Analysis failed: " + std::string(e.what());
        ORC_LOG_ERROR("Field map range analysis failed: {}", e.what());
        return result;
    }
    
    auto field_range = source->field_range();
    if (field_range.size() == 0) {
        result.status = AnalysisResult::Failed;
        result.summary = "No fields found in source";
        return result;
    }
    
    bool is_pal = false;
    auto first_desc = source->get_descriptor(field_range.start);
    if (first_desc && first_desc->format == VideoFormat::PAL) {
        is_pal = true;
    }
    
    ParsedAddress start_addr = parse_address(start_input, is_pal);
    if (!start_addr.ok) {
        result.status = AnalysisResult::Failed;
        result.summary = "Start address error: " + start_addr.error;
        return result;
    }
    
    ParsedAddress end_addr = parse_address(end_input, is_pal);
    if (!end_addr.ok) {
        result.status = AnalysisResult::Failed;
        result.summary = "End address error: " + end_addr.error;
        return result;
    }
    
    if (progress) {
        progress->setStatus("Extracting VBI data from fields...");
        progress->setProgress(10);
    }
    
    BiphaseObserver biphase_observer;
    auto& obs_context = executor.get_observation_context();
    
    for (FieldID fid = field_range.start; fid < field_range.end; fid = FieldID(fid.value() + 1)) {
        biphase_observer.process_field(*source, fid, obs_context);
    }
    
    if (progress) {
        progress->setStatus("Scanning for start/end frames...");
        progress->setProgress(30);
    }
    
    bool start_found = false;
    bool end_found = false;
    FieldID start_field;
    FieldID end_field;
    
    size_t total_fields = field_range.size();
    size_t processed_fields = 0;
    bool end_same_as_start = (start_addr.picture_number == end_addr.picture_number);
    bool end_frame_active = false;
    
    for (FieldID fid = field_range.start; fid < field_range.end; fid = FieldID(fid.value() + 1)) {
        auto pn_opt = get_picture_number_from_vbi(obs_context, fid, is_pal);
        processed_fields++;
        
        if (!pn_opt) {
            continue;
        }
        
        int32_t pn = *pn_opt;
        
        if (!start_found) {
            if (pn == start_addr.picture_number) {
                start_found = true;
                start_field = fid;
                if (end_same_as_start) {
                    end_found = true;
                    end_field = fid;
                    end_frame_active = true;
                }
            }
        } else {
            if (end_same_as_start) {
                if (pn == start_addr.picture_number) {
                    end_field = fid;
                } else if (end_found && end_frame_active) {
                    break;
                }
            } else {
                if (!end_found && pn == end_addr.picture_number) {
                    end_found = true;
                    end_field = fid;
                    end_frame_active = true;
                } else if (end_found && end_frame_active) {
                    if (pn == end_addr.picture_number) {
                        end_field = fid;
                    } else {
                        break;
                    }
                }
            }
        }
        
        if (progress && (processed_fields % 5000 == 0)) {
            int progress_value = 30 + static_cast<int>(60.0 * processed_fields / std::max<size_t>(1, total_fields));
            progress->setProgress(std::min(progress_value, 90));
            if (progress->isCancelled()) {
                result.status = AnalysisResult::Cancelled;
                return result;
            }
        }
    }
    
    if (!start_found) {
        result.status = AnalysisResult::Failed;
        result.summary = "Start address not found in source.";
        return result;
    }
    
    if (!end_found) {
        result.status = AnalysisResult::Failed;
        result.summary = "End address not found after start address.";
        return result;
    }
    
    if (start_field.value() > end_field.value()) {
        result.status = AnalysisResult::Failed;
        result.summary = "Computed field range is invalid (start after end).";
        return result;
    }
    
    std::ostringstream range_spec;
    range_spec << start_field.value() << "-" << end_field.value();
    
    result.graphData["rangeSpec"] = range_spec.str();
    
    std::ostringstream summary;
    summary << "Field range located successfully.\n\n";
    summary << "Start address: " << start_input << " (picture number " << start_addr.picture_number << ")\n";
    summary << "End address: " << end_input << " (picture number " << end_addr.picture_number << ")\n\n";
    summary << "Field range: " << start_field.value() << "-" << end_field.value() << "\n";
    summary << "Range spec: " << range_spec.str() << "\n\n";
    summary << "Click 'Apply to Node' to update the Field Map stage.";
    
    result.summary = summary.str();
    result.status = AnalysisResult::Success;
    
    if (progress) {
        progress->setStatus("Analysis complete");
        progress->setProgress(100);
    }
    
    return result;
}

bool FieldMapRangeAnalysisTool::canApplyToGraph() const {
    return true;
}

bool FieldMapRangeAnalysisTool::applyToGraph(AnalysisResult& result,
                                             const Project& /*project*/,
                                             NodeID node_id) {
    if (result.status != AnalysisResult::Success) {
        ORC_LOG_ERROR("Cannot apply failed analysis result");
        return false;
    }
    
    auto it = result.graphData.find("rangeSpec");
    if (it == result.graphData.end()) {
        ORC_LOG_ERROR("Analysis result does not contain rangeSpec");
        return false;
    }
    
    const std::string& range_spec = it->second;
    
    try {
        result.parameterChanges["ranges"] = range_spec;
        ORC_LOG_DEBUG("Prepared range spec '{}' for node '{}'", range_spec, node_id);
        return true;
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("Failed to prepare range spec: {}", e.what());
        return false;
    }
}

int FieldMapRangeAnalysisTool::estimateDurationSeconds(const AnalysisContext& ctx) const {
    (void)ctx;
    return 10;
}

} // namespace orc
