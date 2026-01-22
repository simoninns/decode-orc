#include "disc_mapper_analysis.h"
#include "disc_mapper_analyzer.h"
#include "../analysis_registry.h"
#include "../../include/video_field_representation.h"
#include "../../include/tbc_video_field_representation.h"
#include "../../include/dag_executor.h"
#include "../../include/project.h"
#include "logging.h"
#include <iostream>
#include <algorithm>
#include <sstream>

namespace orc {

// Force linker to include this object file (for static registration)
void force_link_DiscMapperAnalysisTool() {}

std::string DiscMapperAnalysisTool::id() const {
    return "field_mapping";
}

std::string DiscMapperAnalysisTool::name() const {
    return "Disc Mapper";
}

std::string DiscMapperAnalysisTool::description() const {
    return "Detect and correct skipped, repeated, and missing fields caused by "
           "laserdisc player tracking problems.";
}

std::string DiscMapperAnalysisTool::category() const {
    return "Diagnostic";
}

std::vector<ParameterDescriptor> DiscMapperAnalysisTool::parameters() const {
    return {};
}

bool DiscMapperAnalysisTool::canAnalyze(AnalysisSourceType source_type) const {
    // Can analyze laserdisc sources
    return source_type == AnalysisSourceType::LaserDisc;
}

bool DiscMapperAnalysisTool::isApplicableToStage(const std::string& stage_name) const {
    // Field mapping analysis is only applicable to field_map stages
    // because it generates a mapping specification that the field_map stage uses
    return stage_name == "field_map";
}

AnalysisResult DiscMapperAnalysisTool::analyze(const AnalysisContext& ctx,
                                               AnalysisProgress* progress) {
    AnalysisResult result;
    
    if (progress) {
        progress->setStatus("Initializing disc mapper analysis...");
        progress->setProgress(0);
    }
    
    // Get the VideoFieldRepresentation from the DAG execution
    // The field_map node should have exactly one input
    if (!ctx.dag || !ctx.project) {
        result.status = AnalysisResult::Failed;
        result.summary = "No DAG or project provided for analysis";
        ORC_LOG_ERROR("Field mapping analysis requires DAG and project in context");
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
    ORC_LOG_DEBUG("Node '{}': Field mapping analysis - getting input from node '{}'", ctx.node_id, input_node_id);
    
    // Execute DAG to get the VideoFieldRepresentation from the input node
    // We need to get it as a stage output
    DAGExecutor executor;
    try {
        auto all_outputs = executor.execute_to_node(*ctx.dag, input_node_id);
        
        // Get the outputs from the input node
        auto output_it = all_outputs.find(input_node_id);
        if (output_it == all_outputs.end() || output_it->second.empty()) {
            result.status = AnalysisResult::Failed;
            result.summary = "Input node produced no outputs";
            ORC_LOG_ERROR("Node '{}': Input node '{}' produced no outputs", ctx.node_id, input_node_id);
            return result;
        }
        
        // Find the VideoFieldRepresentation output
        std::shared_ptr<VideoFieldRepresentation> source;
        for (const auto& artifact : output_it->second) {
            // Try to cast to VideoFieldRepresentation
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
        
        if (progress) {
            progress->setStatus("Running field analysis...");
            progress->setProgress(20);
        }
        
        // Now run the analyzer on the representation
        DiscMapperAnalyzer analyzer;
        DiscMapperAnalyzer::Options options;
        
        if (progress && progress->isCancelled()) {
            result.status = AnalysisResult::Cancelled;
            return result;
        }
        
        if (progress) {
            progress->setStatus("Analyzing field sequence...");
            progress->setProgress(20);
        }
        
        // Run field mapping analysis (20-90% progress range)
        FieldMappingDecision decision = analyzer.analyze(*source, options, progress);

        if (!decision.success) {
            result.status = AnalysisResult::Failed;
            result.summary = decision.rationale.empty()
                ? "Disc mapper analysis is currently stubbed out"
                : decision.rationale;

            for (const auto& warning : decision.warnings) {
                AnalysisResult::ResultItem item;
                item.type = "warning";
                item.message = warning;
                result.items.push_back(item);
            }
            return result;
        }
        
        if (progress && progress->isCancelled()) {
            result.status = AnalysisResult::Cancelled;
            return result;
        }
        
        if (progress) {
            progress->setStatus("Processing results...");
            progress->setProgress(90);
        }
        
        // Convert warnings to result items
        for (const auto& warning : decision.warnings) {
            AnalysisResult::ResultItem item;
            item.type = "warning";
            item.message = warning;
            result.items.push_back(item);
            
            if (progress) {
                progress->reportPartialResult(item);
            }
        }
        
        if (progress) {
            progress->setStatus("Analysis complete");
            progress->setProgress(100);
        }
        
        // Build detailed summary
        const auto& stats = decision.stats;
        size_t total_frames = stats.total_fields / 2;
        size_t final_frames = total_frames - stats.removed_lead_in_out - stats.removed_invalid_phase 
                            - stats.removed_duplicates - stats.removed_unmappable + stats.padding_frames;
        
        std::ostringstream summary;
        std::string disc_type = decision.is_cav ? "CAV" : "CLV";
        std::string video_format = decision.is_pal ? "PAL" : "NTSC";
        
        summary << "Source: " << video_format << " " << disc_type << " disc\n\n";
        
        summary << "Input:\n";
        summary << "  " << stats.total_fields << " fields (" << total_frames << " field pairs/frames)\n\n";
        
        summary << "Output:\n";
        summary << "  " << final_frames << " frames (" << (final_frames * 2) << " fields)";
        
        if (stats.removed_duplicates > 0 || stats.gaps_padded > 0 || stats.removed_lead_in_out > 0) {
            summary << " (";
            bool need_sep = false;
            if (stats.removed_duplicates > 0) {
                summary << stats.removed_duplicates << " duplicates removed";
                need_sep = true;
            }
            if (stats.gaps_padded > 0) {
                if (need_sep) summary << ", ";
                summary << stats.gaps_padded << " gaps padded";
                need_sep = true;
            }
            if (stats.removed_lead_in_out > 0) {
                if (need_sep) summary << ", ";
                summary << stats.removed_lead_in_out << " lead-in/out removed";
            }
            summary << ")";
        }
        
        // Add generated mapping spec to summary
        summary << "\n\nGenerated Field Mapping:\n";
        if (decision.mapping_spec.length() <= 200) {
            summary << "  " << decision.mapping_spec;
        } else {
            summary << "  " << decision.mapping_spec.substr(0, 200) << "...\n";
            summary << "  (Full spec: " << decision.mapping_spec.length() << " chars - see details below)";
        }
        
        result.summary = summary.str();
        
        // Statistics
        result.statistics["discType"] = decision.is_cav ? "CAV" : "CLV";
        result.statistics["videoFormat"] = decision.is_pal ? "PAL" : "NTSC";
        result.statistics["totalFields"] = static_cast<long long>(stats.total_fields);
        result.statistics["outputFields"] = static_cast<long long>(final_frames * 2);
        result.statistics["outputFrames"] = static_cast<long long>(final_frames);
        result.statistics["removedLeadInOut"] = static_cast<long long>(stats.removed_lead_in_out);
        result.statistics["removedInvalidPhase"] = static_cast<long long>(stats.removed_invalid_phase);
        result.statistics["removedDuplicates"] = static_cast<long long>(stats.removed_duplicates);
        result.statistics["removedUnmappable"] = static_cast<long long>(stats.removed_unmappable);
        result.statistics["correctedVBIErrors"] = static_cast<long long>(stats.corrected_vbi_errors);
        result.statistics["pulldownFrames"] = static_cast<long long>(stats.pulldown_frames);
        result.statistics["paddingFrames"] = static_cast<long long>(stats.padding_frames);
        result.statistics["gapsPadded"] = static_cast<long long>(stats.gaps_padded);
        
        // Store mapping spec for graph application
        result.graphData["mappingSpec"] = decision.mapping_spec;
        result.graphData["rationale"] = decision.rationale;
        
        ORC_LOG_DEBUG("Field mapping analysis - adding mapping spec to result items ({} chars)", 
                     decision.mapping_spec.length());
        
        // Add detailed info items for display
        AnalysisResult::ResultItem spec_item;
        spec_item.type = "info";
        spec_item.message = "Generated Field Mapping Specification:\n\n" + decision.mapping_spec;
        result.items.push_back(spec_item);
        
        ORC_LOG_DEBUG("Field mapping analysis - adding rationale to result items ({} chars)", 
                     decision.rationale.length());
        
        // Add rationale as separate item
        AnalysisResult::ResultItem rationale_item;
        rationale_item.type = "info";
        rationale_item.message = "Analysis Rationale:\n\n" + decision.rationale;
        result.items.push_back(rationale_item);
        
        ORC_LOG_DEBUG("Field mapping analysis complete - {} result items total", result.items.size());
        
        result.status = AnalysisResult::Success;
        return result;
        
    } catch (const std::exception& e) {
        result.status = AnalysisResult::Failed;
        result.summary = std::string("Analysis failed: ") + e.what();
        ORC_LOG_ERROR("Field mapping analysis failed: {}", e.what());
        return result;
    }
}

bool DiscMapperAnalysisTool::canApplyToGraph() const {
    return true;
}

bool DiscMapperAnalysisTool::applyToGraph(const AnalysisResult& result,
                                         Project& project,
                                         NodeID node_id) {
    // Find the target node in the project
    const auto& nodes = project.get_nodes();
    auto node_it = std::find_if(nodes.begin(), nodes.end(),
        [&node_id](const ProjectDAGNode& n) { return n.node_id == node_id; });
    
    if (node_it == nodes.end()) {
        std::cerr << "DiscMapperAnalysisTool::applyToGraph: node not found: " << node_id.value() << std::endl;
        return false;
    }
    
    // Apply mapping spec to the node's parameters
    auto mapping_it = result.graphData.find("mappingSpec");
    if (mapping_it == result.graphData.end()) {
        ORC_LOG_ERROR("DiscMapperAnalysisTool::applyToGraph - No mapping spec in result");
        std::cerr << "No mapping spec in result" << std::endl;
        return false;
    }
    std::string mappingSpec = mapping_it->second;
    
    ORC_LOG_DEBUG("Node '{}': Applying field mapping results", node_id);
    if (node_it->parameters.count("ranges")) {
        auto& old_value = node_it->parameters.at("ranges");
        if (auto* str_val = std::get_if<std::string>(&old_value)) {
            ORC_LOG_DEBUG("Node '{}':   Old ranges parameter: {}", node_id, *str_val);
        }
    } else {
        ORC_LOG_DEBUG("Node '{}':   Old ranges parameter: (not set)", node_id);
    }
    ORC_LOG_DEBUG("Node '{}':   New mapping spec: {}", node_id, mappingSpec);
    
    std::cout << "Applying field mapping results to node " << node_id.value() << std::endl;
    std::cout << "  Mapping spec: " << mappingSpec << std::endl;
    auto rationale_it = result.graphData.find("rationale");
    if (rationale_it != result.graphData.end()) {
        std::cout << "  Rationale: " << rationale_it->second << std::endl;
    }
    
    // Set the FieldMapStage's "ranges" parameter to the computed mapping spec
    // Use project_io function to modify parameters
    auto updated_params = node_it->parameters;
    updated_params["ranges"] = mappingSpec;
    project_io::set_node_parameters(project, node_id, updated_params);
    
    ORC_LOG_DEBUG("Successfully applied mapping spec to FieldMapStage 'ranges' parameter");
    std::cout << "Successfully applied mapping spec to FieldMapStage 'ranges' parameter" << std::endl;
    return true;
}

int DiscMapperAnalysisTool::estimateDurationSeconds(const AnalysisContext& ctx) const {
    (void)ctx;
    // Disc mapper needs to load entire TBC and run observers
    // Estimate: ~5-10 seconds for typical TBC file
    return 5;
}

// Register the tool
REGISTER_ANALYSIS_TOOL(DiscMapperAnalysisTool);

} // namespace orc
