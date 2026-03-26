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
#include <orc_preview_carriers.h>
#include <orc_source_parameters.h>
#include <memory>

// Forward declaration (ComponentFrame is in global namespace, not orc::)
class ComponentFrame;

namespace orc {

// Forward declarations
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
    bool applyToGraph(AnalysisResult& result,
                     const Project& project,
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
    
    /**
     * @brief Extract vectorscope data directly from ComponentFrame U/V channels
     * 
     * This is the preferred method as it uses the native U/V chroma values from
     * the decoder, avoiding RGB→YUV conversion artifacts and signal level issues.
     * 
     * @param frame ComponentFrame with decoded Y/U/V data
     * @param field_number Field number for identification
     * @param subsample Subsampling factor (1 = all pixels, 2 = every other pixel, etc.)
     * @return Vectorscope data with native U/V samples from both fields
     */
    static VectorscopeData extractFromComponentFrame(
        const ::ComponentFrame& frame,
        const ::orc::SourceParameters& video_parameters,
        uint64_t field_number,
        uint32_t subsample = 1);

    /**
     * @brief Extract vectorscope data from a colour preview carrier.
     *
     * Uses the decoded U/V planes already present in the carrier and can either
     * limit sampling to the active picture window or include the entire frame.
     */
    static VectorscopeData extractFromColourFrameCarrier(
        const ColourFrameCarrier& carrier,
        uint64_t field_number,
        uint32_t subsample = 1,
        bool active_area_only = true);
};

} // namespace orc

#endif // ORC_CORE_ANALYSIS_VECTORSCOPE_ANALYSIS_H
