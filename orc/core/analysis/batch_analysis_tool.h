/*
 * File:        batch_analysis_tool.h
 * Module:      orc-core
 * Purpose:     Base class for GUI-triggered batch analysis tools
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef ORC_CORE_BATCH_ANALYSIS_TOOL_H
#define ORC_CORE_BATCH_ANALYSIS_TOOL_H

#include "analysis_tool.h"

namespace orc {

/**
 * @brief Base class for batch analysis tools that are triggered via GUI
 * 
 * Batch analysis tools are designed to process entire field ranges via the
 * RenderCoordinator and associated analysis decoders. The actual data 
 * processing happens in specialized decoder classes, not in the analyze() 
 * method itself.
 * 
 * This base class implements common boilerplate for:
 * - analyze() method that returns a registration-only result
 * - canApplyToGraph() returning false (these are analysis-only tools)
 * - applyToGraph() that does nothing
 * - isApplicableToStage() rejecting chroma_sink (which produces frames, not fields)
 * - estimateDurationSeconds() returning -1 (unknown, depends on field count)
 * 
 * Derived classes only need to implement:
 * - id()
 * - name()
 * - description()
 * - category()
 * - parameters()
 * - canAnalyze()
 */
class BatchAnalysisTool : public AnalysisTool {
public:
    /**
     * @brief Run the analysis (base implementation for batch tools)
     * 
     * Batch analysis tools don't perform their work here. Instead, the actual
     * processing happens in the RenderCoordinator via specialized decoders.
     * This method exists to satisfy the AnalysisTool interface and for future
     * command-line batch processing support.
     */
    AnalysisResult analyze(const AnalysisContext& ctx,
                          AnalysisProgress* progress) override final;

    /**
     * @brief Batch analysis tools cannot apply results back to the graph
     * @return Always false
     */
    bool canApplyToGraph() const override final {
        return false;
    }

    /**
     * @brief Apply analysis results (not supported for batch tools)
     * @return Always false
     */
    bool applyToGraph(const AnalysisResult& /*result*/,
                     Project& /*project*/,
                     NodeID /*node_id*/) override final {
        return false;
    }

    /**
     * @brief Check if this tool is applicable to the given stage type
     * 
     * Batch analysis tools work with field-based stages. They are not
     * applicable to frame-based output stages like chroma_sink, or
     * sink stages that don't produce outputs (AudioSink, EFMSink, LDSink, CCSink).
     * 
     * @param stage_name Name of the stage type
     * @return true if applicable (excludes output/sink stages)
     */
    bool isApplicableToStage(const std::string& stage_name) const override {
        // Video sink stages (raw/ffmpeg) produce RGB frames, not fields with observations
        // Sink stages (AudioSink, EFMSink, ld_sink, CCSink) produce no outputs
        return stage_name != "raw_video_sink" && 
               stage_name != "ffmpeg_video_sink" &&
               stage_name != "chroma_sink_base" &&  // Legacy base class
               stage_name != "AudioSink" && 
               stage_name != "EFMSink" && 
               stage_name != "ld_sink" &&
               stage_name != "hackdac_sink" &&
               stage_name != "CCSink";
    }

    /**
     * @brief Estimate analysis duration
     * @return -1 (unknown - depends on field count and stage complexity)
     */
    int estimateDurationSeconds(const AnalysisContext& /*ctx*/) const override {
        return -1;
    }

protected:
    /**
     * @brief Get the name of the decoder used for this analysis
     * 
     * Used for logging and status messages. Should return something like
     * "DropoutAnalysisDecoder" or "SNRAnalysisDecoder".
     */
    virtual std::string decoder_name() const = 0;
};

} // namespace orc

#endif // ORC_CORE_BATCH_ANALYSIS_TOOL_H
