/*
 * File:        field_mapping_range_analysis.h
 * Module:      orc-core/analysis
 * Purpose:     Field mapping range analysis tool (frame/timecode to field ID converter)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include "../analysis_tool.h"
#include "field_mapping_lookup.h"

namespace orc {

/**
 * @brief Field mapping range analysis tool
 * 
 * This tool allows users to specify frame numbers or timecodes and automatically
 * generates the correct field_map range parameter. It analyzes the source VBI data
 * to determine which field IDs correspond to the requested frames/timecodes.
 * 
 * Use case: "I want frames 1000-2000" -> tool finds field IDs and updates field_map
 */
class FieldMappingRangeAnalysisTool : public AnalysisTool {
public:
    std::string id() const override { return "field_mapping_range"; }
    std::string name() const override { return "Field Mapping Range (Frame/Timecode to Field IDs)"; }
    std::string description() const override { 
        return "Specify frame numbers or timecodes to automatically populate the field_map "
               "range parameter with the correct field IDs.";
    }
    std::string category() const override { return "Field Mapping"; }
    
    std::vector<ParameterDescriptor> parameters() const override;
    std::vector<ParameterDescriptor> parametersForContext(const AnalysisContext& ctx) const override;
    bool canAnalyze(AnalysisSourceType source_type) const override;
    bool isApplicableToStage(const std::string& stage_name) const override;
    int priority() const override { return 1; }  // Stage-specific tool
    
    AnalysisResult analyze(const AnalysisContext& ctx,
                          AnalysisProgress* progress) override;
    
    bool canApplyToGraph() const override { return true; }
    bool applyToGraph(AnalysisResult& result,
                     const Project& project,
                     NodeID node_id) override;
    
    int estimateDurationSeconds(const AnalysisContext& /*ctx*/) const override { return 2; }
};

} // namespace orc
