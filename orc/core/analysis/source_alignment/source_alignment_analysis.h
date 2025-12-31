/*
 * File:        source_alignment_analysis.h
 * Module:      orc-core
 * Purpose:     Source alignment analysis tool
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#ifndef ORC_CORE_ANALYSIS_SOURCE_ALIGNMENT_ANALYSIS_H
#define ORC_CORE_ANALYSIS_SOURCE_ALIGNMENT_ANALYSIS_H

#include "../analysis_tool.h"

namespace orc {

/**
 * @brief Source alignment analysis tool
 * 
 * Analyzes multiple input sources and determines the optimal alignment
 * by finding common VBI frame numbers or CLV timecodes across all sources.
 * Generates an alignment map that can be applied to the source_align stage.
 */
class SourceAlignmentAnalysisTool : public AnalysisTool {
public:
    std::string id() const override;
    std::string name() const override;
    std::string description() const override;
    std::string category() const override;
    
    std::vector<ParameterDescriptor> parameters() const override;
    bool canAnalyze(AnalysisSourceType source_type) const override;
    bool isApplicableToStage(const std::string& stage_name) const override;
    int priority() const override { return 1; }  // Stage-specific tool
    
    AnalysisResult analyze(const AnalysisContext& ctx,
                          AnalysisProgress* progress) override;
    
    bool canApplyToGraph() const override;
    bool applyToGraph(const AnalysisResult& result,
                     Project& project,
                     NodeID node_id) override;
    
    int estimateDurationSeconds(const AnalysisContext& ctx) const override;
};

} // namespace orc

#endif // ORC_CORE_ANALYSIS_SOURCE_ALIGNMENT_ANALYSIS_H
