/*
 * File:        snr_analysis.cpp
 * Module:      orc-core
 * Purpose:     SNR analysis tool implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "snr_analysis.h"
#include "../analysis_registry.h"
#include "logging.h"

namespace orc {

std::string SNRAnalysisTool::id() const {
    return "snr_analysis";
}

std::string SNRAnalysisTool::name() const {
    return "SNR Analysis";
}

std::string SNRAnalysisTool::description() const {
    return "Analyze signal-to-noise ratio statistics across all fields from stage output";
}

std::string SNRAnalysisTool::category() const {
    return "Analysis";
}

std::vector<ParameterDescriptor> SNRAnalysisTool::parameters() const {
    // Parameters could include analysis mode (white/black/both)
    return {};
}

bool SNRAnalysisTool::canAnalyze(AnalysisSourceType source_type) const {
    // Works with field sources that have SNR observation data
    (void)source_type;
    return true;
}

bool SNRAnalysisTool::isApplicableToStage(const std::string& stage_name) const {
    // Applicable to field-based stages (not frame-based output stages like chroma_sink)
    // ChromaSinkStage produces RGB frames, not fields with SNR observations
    if (stage_name == "chroma_sink") {
        return false;
    }
    
    // All field-based stages can have SNR analysis
    return true;
}

AnalysisResult SNRAnalysisTool::analyze(const AnalysisContext& ctx,
                                       AnalysisProgress* progress) {
    (void)ctx;  // Unused - GUI-triggered tool
    AnalysisResult result;
    
    // This is a batch analysis tool that will be triggered via the GUI
    // The actual data processing happens in the RenderCoordinator and SNRAnalysisDecoder
    // This method exists to satisfy the AnalysisTool interface and for future
    // command-line batch processing support
    
    if (progress) {
        progress->setStatus("SNR analysis will be processed via GUI");
        progress->setProgress(100);
    }
    
    result.status = AnalysisResult::Success;
    result.summary = "SNR analysis tool registered";
    
    ORC_LOG_DEBUG("SNR analysis tool registered (GUI-triggered batch processing)");
    
    return result;
}

bool SNRAnalysisTool::canApplyToGraph() const {
    // Analysis only, nothing to apply back to graph
    return false;
}

bool SNRAnalysisTool::applyToGraph(const AnalysisResult& result,
                                  Project& project,
                                  const std::string& node_id) {
    (void)result;
    (void)project;
    (void)node_id;
    
    // Analysis only, nothing to apply
    return false;
}

int SNRAnalysisTool::estimateDurationSeconds(const AnalysisContext& ctx) const {
    (void)ctx;
    
    // Duration depends on number of fields and stage complexity
    // Return -1 to indicate unknown
    return -1;
}

// Auto-register this tool
REGISTER_ANALYSIS_TOOL(SNRAnalysisTool);

} // namespace orc
