/*
 * File:        dropout_analysis.cpp
 * Module:      orc-core
 * Purpose:     Dropout analysis tool implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "dropout_analysis.h"
#include "../analysis_registry.h"
#include "logging.h"

namespace orc {

std::string DropoutAnalysisTool::id() const {
    return "dropout_analysis";
}

std::string DropoutAnalysisTool::name() const {
    return "Dropout Analysis";
}

std::string DropoutAnalysisTool::description() const {
    return "Analyze dropout statistics across all fields from stage output";
}

std::string DropoutAnalysisTool::category() const {
    return "Analysis";
}

std::vector<ParameterDescriptor> DropoutAnalysisTool::parameters() const {
    // Parameters could include analysis mode (full field vs visible area)
    return {};
}

bool DropoutAnalysisTool::canAnalyze(AnalysisSourceType source_type) const {
    // Works with field sources that have dropout observation data
    (void)source_type;
    return true;
}

bool DropoutAnalysisTool::isApplicableToStage(const std::string& stage_name) const {
    // Applicable to field-based stages (not frame-based output stages like chroma_sink)
    // ChromaSinkStage produces RGB frames, not fields with dropout observations
    if (stage_name == "chroma_sink") {
        return false;
    }
    
    // All field-based stages can have dropout analysis
    return true;
}

AnalysisResult DropoutAnalysisTool::analyze(const AnalysisContext& ctx,
                                           AnalysisProgress* progress) {
    (void)ctx;  // Unused - GUI-triggered tool
    AnalysisResult result;
    
    // This is a batch analysis tool that will be triggered via the GUI
    // The actual data processing happens in the RenderCoordinator and DropoutAnalysisDecoder
    // This method exists to satisfy the AnalysisTool interface and for future
    // command-line batch processing support
    
    if (progress) {
        progress->setStatus("Dropout analysis will be processed via GUI");
        progress->setProgress(100);
    }
    
    result.status = AnalysisResult::Success;
    result.summary = "Dropout analysis tool registered";
    
    ORC_LOG_DEBUG("Dropout analysis tool registered (GUI-triggered batch processing)");
    
    return result;
}

bool DropoutAnalysisTool::canApplyToGraph() const {
    // Analysis only, nothing to apply back to graph
    return false;
}

bool DropoutAnalysisTool::applyToGraph(const AnalysisResult& result,
                                      Project& project,
                                      const std::string& node_id) {
    (void)result;
    (void)project;
    (void)node_id;
    
    // Analysis only, nothing to apply
    return false;
}

int DropoutAnalysisTool::estimateDurationSeconds(const AnalysisContext& ctx) const {
    (void)ctx;
    
    // Duration depends on number of fields and stage complexity
    // Return -1 to indicate unknown
    return -1;
}

// Auto-register this tool
REGISTER_ANALYSIS_TOOL(DropoutAnalysisTool);

} // namespace orc
