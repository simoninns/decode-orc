/*
 * File:        burst_level_analysis.h
 * Module:      orc-core
 * Purpose:     Burst level analysis tool for stage outputs
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef ORC_CORE_ANALYSIS_BURST_LEVEL_ANALYSIS_H
#define ORC_CORE_ANALYSIS_BURST_LEVEL_ANALYSIS_H

#include "../batch_analysis_tool.h"
#include <memory>

namespace orc {

/**
 * @brief Burst level analysis tool for analyzing color burst levels across all fields
 * 
 * This tool processes all fields from a stage output and generates burst level statistics
 * that can be displayed in a graph dialog. It triggers batch processing through the
 * DAG executor to ensure all field data is available.
 * 
 * The actual data processing happens in BurstLevelAnalysisDecoder via the RenderCoordinator.
 */
class BurstLevelAnalysisTool : public BatchAnalysisTool {
public:
    std::string id() const override;
    std::string name() const override;
    std::string description() const override;
    std::string category() const override;
    
    std::vector<ParameterDescriptor> parameters() const override;
    bool canAnalyze(AnalysisSourceType source_type) const override;

protected:
    std::string decoder_name() const override;
};

} // namespace orc

#endif // ORC_CORE_ANALYSIS_BURST_LEVEL_ANALYSIS_H
