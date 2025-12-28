/*
 * File:        dropout_analysis.h
 * Module:      orc-core
 * Purpose:     Dropout analysis tool for stage outputs
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#ifndef ORC_CORE_ANALYSIS_DROPOUT_ANALYSIS_H
#define ORC_CORE_ANALYSIS_DROPOUT_ANALYSIS_H

#include "../analysis_tool.h"
#include <memory>

namespace orc {

/**
 * @brief Dropout analysis tool for analyzing dropout statistics across all fields
 * 
 * This tool processes all fields from a stage output and generates dropout statistics
 * that can be displayed in a graph dialog. It triggers batch processing through the
 * DAG executor to ensure all field data is available.
 */
class DropoutAnalysisTool : public AnalysisTool {
public:
    std::string id() const override;
    std::string name() const override;
    std::string description() const override;
    std::string category() const override;
    
    std::vector<ParameterDescriptor> parameters() const override;
    bool canAnalyze(AnalysisSourceType source_type) const override;
    bool isApplicableToStage(const std::string& stage_name) const override;
    
    AnalysisResult analyze(const AnalysisContext& ctx,
                          AnalysisProgress* progress) override;
    
    bool canApplyToGraph() const override;
    bool applyToGraph(const AnalysisResult& result,
                     Project& project,
                     const std::string& node_id) override;
    
    int estimateDurationSeconds(const AnalysisContext& ctx) const override;
};

} // namespace orc

#endif // ORC_CORE_ANALYSIS_DROPOUT_ANALYSIS_H
