/*
 * File:        ffmpeg_preset_analysis.h
 * Module:      orc-core
 * Purpose:     FFmpeg export preset configuration analysis tool
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#ifndef ORC_CORE_ANALYSIS_FFMPEG_PRESET_ANALYSIS_H
#define ORC_CORE_ANALYSIS_FFMPEG_PRESET_ANALYSIS_H

#include "../analysis_tool.h"

namespace orc {

/**
 * @brief FFmpeg export preset configuration tool
 * 
 * Provides convenient presets for video export without requiring users
 * to understand codec details. Based on profiles from legacy tbc-video-export tool.
 * 
 * This tool generates parameters for the FFmpegVideoSink stage based on
 * common export scenarios:
 * - Lossless archival (FFV1, ProRes, lossless H.264/H.265/AV1)
 * - Professional editing (ProRes variants)
 * - Web delivery (H.264, H.265, AV1)
 * - Broadcast (D10/IMX)
 * - Hardware-accelerated encoding
 */
class FFmpegPresetAnalysisTool : public AnalysisTool {
public:
    std::string id() const override;
    std::string name() const override;
    std::string description() const override;
    std::string category() const override;
    
    std::vector<ParameterDescriptor> parameters() const override;
    std::vector<ParameterDescriptor> parametersForContext(const AnalysisContext& ctx) const override;
    
    bool canAnalyze(AnalysisSourceType source_type) const override;
    bool isApplicableToStage(const std::string& stage_name) const override;
    int priority() const override { return 1; }  // Stage-specific tool
    
    AnalysisResult analyze(const AnalysisContext& ctx,
                          AnalysisProgress* progress) override;
    
    bool canApplyToGraph() const override;
    bool applyToGraph(const AnalysisResult& result,
                     Project& project,
                     NodeID node_id) override;
    
    // This is a configuration tool - no analysis needed (instant)
    int estimateDurationSeconds(const AnalysisContext& /*ctx*/) const override { return 0; }
};

} // namespace orc

#endif // ORC_CORE_ANALYSIS_FFMPEG_PRESET_ANALYSIS_H
