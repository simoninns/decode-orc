/*
 * File:        vectorscope_analysis.h
 * Module:      orc-core
 * Purpose:     Vectorscope analysis tool for chroma decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef ORC_CORE_ANALYSIS_VECTORSCOPE_ANALYSIS_H
#define ORC_CORE_ANALYSIS_VECTORSCOPE_ANALYSIS_H

#include "../analysis_tool.h"
#include "vectorscope_data.h"
#include <memory>

namespace orc {

// Forward declaration
class ChromaSinkStage;

/**
 * @brief Vectorscope visualization tool for chroma decoder output
 * 
 * This is a "live" analysis tool that continuously extracts U/V data from
 * decoded RGB fields for real-time vectorscope display. Unlike batch analysis
 * tools, this one doesn't produce a static result - it provides a data stream
 * to the GUI.
 */
class VectorscopeAnalysisTool : public AnalysisTool {
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
                     NodeID node_id) override;
    
    int estimateDurationSeconds(const AnalysisContext& ctx) const override;
    
    /**
     * @brief Extract vectorscope data from a decoded RGB field
     * 
     * @param rgb_data RGB field data (16-bit per channel, interleaved R,G,B)
     * @param width Field width in pixels
     * @param height Field height in lines
     * @param field_number Field number for identification
     * @param subsample Subsampling factor (1 = all pixels, 2 = every other pixel, etc.)
     * @param field_id Optional field index (0=first/odd, 1=second/even) for blend color tracking
     * @return Vectorscope data with U/V samples
     */
    static VectorscopeData extractFromRGB(
        const uint16_t* rgb_data,
        uint32_t width,
        uint32_t height,
        uint64_t field_number,
        uint32_t subsample = 1,
        uint8_t field_id = 0);
    
    /**
     * @brief Extract vectorscope data from both fields in an interlaced RGB frame
     * 
     * Processes even lines (first field) and odd lines (second field) separately,
     * tagging each sample with its field_id for proper visualization.
     * 
     * @param rgb_data RGB frame data (16-bit per channel, interleaved R,G,B)
     * @param width Frame width in pixels
     * @param height Frame height in lines (both fields combined)
     * @param field_number Field number for identification (first field)
     * @param subsample Subsampling factor (1 = all pixels, 2 = every other pixel, etc.)
     * @return Vectorscope data with U/V samples from both fields
     */
    static VectorscopeData extractFromInterlacedRGB(
        const uint16_t* rgb_data,
        uint32_t width,
        uint32_t height,
        uint64_t field_number,
        uint32_t subsample = 1);
};

} // namespace orc

#endif // ORC_CORE_ANALYSIS_VECTORSCOPE_ANALYSIS_H
