/*
 * File:        mask_line_analysis.h
 * Module:      orc-core
 * Purpose:     Line masking configuration analysis tool
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#ifndef ORC_CORE_ANALYSIS_MASK_LINE_ANALYSIS_H
#define ORC_CORE_ANALYSIS_MASK_LINE_ANALYSIS_H

#include "../analysis_tool.h"

namespace orc {

/**
 * @brief Line masking configuration tool
 * 
 * Provides convenient checkboxes for common line masking scenarios,
 * particularly for hiding visible VBI data like closed captions.
 * 
 * This tool generates parameters for the MaskLine stage based on
 * common use cases, making it easy to:
 * - Mask NTSC closed caption line (field line 21, first field)
 * - Mask other common VBI lines
 * - Configure custom line masking
 */
class MaskLineAnalysisTool : public AnalysisTool {
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

#endif // ORC_CORE_ANALYSIS_MASK_LINE_ANALYSIS_H
