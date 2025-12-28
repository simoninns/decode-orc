/*
 * File:        burst_level_analysis.cpp
 * Module:      orc-core
 * Purpose:     Burst level analysis tool implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "burst_level_analysis.h"
#include "../analysis_registry.h"
#include "logging.h"

namespace orc {

std::string BurstLevelAnalysisTool::id() const {
    return "burst_level_analysis";
}

std::string BurstLevelAnalysisTool::name() const {
    return "Burst Level Analysis";
}

std::string BurstLevelAnalysisTool::description() const {
    return "Analyze color burst level statistics across all fields from stage output";
}

std::string BurstLevelAnalysisTool::category() const {
    return "Analysis";
}

std::vector<ParameterDescriptor> BurstLevelAnalysisTool::parameters() const {
    // No parameters needed for basic burst level analysis
    return {};
}

bool BurstLevelAnalysisTool::canAnalyze(AnalysisSourceType source_type) const {
    // Works with field sources that have burst level observation data
    (void)source_type;
    return true;
}

bool BurstLevelAnalysisTool::isApplicableToStage(const std::string& stage_name) const {
    // Applicable to field-based stages (not frame-based output stages like chroma_sink)
    // ChromaSinkStage produces RGB frames, not fields with burst level observations
    if (stage_name == "chroma_sink") {
        return false;
    }
    
    // All field-based stages can have burst level analysis
    return true;
}

AnalysisResult BurstLevelAnalysisTool::analyze(const AnalysisContext& ctx,
                                              AnalysisProgress* progress) {
    (void)ctx;  // Unused - GUI-triggered tool
    AnalysisResult result;
    
    // This is a batch analysis tool that will be triggered via the GUI
    // The actual data processing happens in the RenderCoordinator and BurstLevelAnalysisDecoder
    // This method exists to satisfy the AnalysisTool interface and for future
    // command-line batch processing support
    
    if (progress) {
        progress->setStatus("Burst level analysis will be processed via GUI");
        progress->setProgress(100);
    }
    
    result.status = AnalysisResult::Success;
    result.summary = "Burst level analysis tool registered";
    
    ORC_LOG_DEBUG("Burst level analysis tool registered (GUI-triggered batch processing)");
    
    return result;
}

bool BurstLevelAnalysisTool::canApplyToGraph() const {
    // Analysis only, nothing to apply back to graph
    return false;
}

bool BurstLevelAnalysisTool::applyToGraph(const AnalysisResult& result,
                                         Project& project,
                                         const std::string& node_id) {
    (void)result;
    (void)project;
    (void)node_id;
    
    // Analysis only, nothing to apply
    return false;
}

int BurstLevelAnalysisTool::estimateDurationSeconds(const AnalysisContext& ctx) const {
    (void)ctx;
    
    // Duration depends on number of fields and stage complexity
    // Return -1 to indicate unknown
    return -1;
}

// Auto-register this tool
REGISTER_ANALYSIS_TOOL(BurstLevelAnalysisTool);

} // namespace orc
