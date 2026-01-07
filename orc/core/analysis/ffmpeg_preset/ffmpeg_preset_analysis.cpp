/*
 * File:        ffmpeg_preset_analysis.cpp
 * Module:      orc-core
 * Purpose:     FFmpeg export preset configuration analysis tool
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "ffmpeg_preset_analysis.h"
#include "../analysis_registry.h"
#include "../../include/project.h"
#include "../../include/dag_executor.h"
#include "logging.h"
#include <sstream>
#include <algorithm>

namespace orc {

std::string FFmpegPresetAnalysisTool::id() const {
    return "ffmpeg_preset_config";
}

std::string FFmpegPresetAnalysisTool::name() const {
    return "Configure Export Preset";
}

std::string FFmpegPresetAnalysisTool::description() const {
    return "Configure video export with convenient presets for archival, professional editing, "
           "web delivery, or broadcast. Configuration is applied immediately.";
}

std::string FFmpegPresetAnalysisTool::category() const {
    return "Configuration";
}

std::vector<ParameterDescriptor> FFmpegPresetAnalysisTool::parameters() const {
    // This tool doesn't use parameters directly - it opens a custom dialog
    // The dialog is handled in the GUI layer (ffmpegpresetdialog.cpp)
    return std::vector<ParameterDescriptor>();
}

std::vector<ParameterDescriptor> FFmpegPresetAnalysisTool::parametersForContext(const AnalysisContext& ctx) const {
    (void)ctx;  // Unused
    return parameters();
}

bool FFmpegPresetAnalysisTool::canAnalyze(AnalysisSourceType source_type) const {
    // Can work with laserdisc sources
    return source_type == AnalysisSourceType::LaserDisc;
}

bool FFmpegPresetAnalysisTool::isApplicableToStage(const std::string& stage_name) const {
    // This tool is applicable to ffmpeg_video_sink stages
    return stage_name == "ffmpeg_video_sink";
}

AnalysisResult FFmpegPresetAnalysisTool::analyze(const AnalysisContext& ctx,
                                                AnalysisProgress* progress) {
    AnalysisResult result;
    
    // This is an instant configuration tool - no progress needed
    (void)progress;
    (void)ctx;
    
    // The actual configuration happens in the GUI dialog
    // This just returns success
    result.summary = "FFmpeg export preset configuration tool.\n\n"
                    "Use the preset dialog to select export format.";
    result.status = AnalysisResult::Success;
    
    return result;
}

bool FFmpegPresetAnalysisTool::canApplyToGraph() const {
    return true;
}

bool FFmpegPresetAnalysisTool::applyToGraph(const AnalysisResult& result,
                                           Project& project,
                                           NodeID node_id) {
    // Application is handled directly by the GUI dialog
    // This is called if the user uses the analysis result dialog
    (void)result;
    (void)project;
    (void)node_id;
    return true;
}

// Register the tool
REGISTER_ANALYSIS_TOOL(FFmpegPresetAnalysisTool);

} // namespace orc
