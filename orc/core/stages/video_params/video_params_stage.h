/*
 * File:        video_params_stage.h
 * Module:      orc-core
 * Purpose:     Video parameters override stage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include "video_field_representation.h"
#include "stage_parameter.h"
#include "dag_executor.h"
#include "preview_renderer.h"
#include "../hints/active_line_hint.h"
#include <memory>

namespace orc {

/**
 * @brief Wrapper that overrides video parameters hints
 */
class VideoParamsOverrideRepresentation : public VideoFieldRepresentationWrapper {
public:
    VideoParamsOverrideRepresentation(
        std::shared_ptr<const VideoFieldRepresentation> source,
        const std::optional<VideoParameters>& override_params)
        : VideoFieldRepresentationWrapper(
            source,
            ArtifactID("video_params_override"),
            Provenance{}),
          override_params_(override_params)
    {
        // If we have override params, use them; otherwise use source params
        if (override_params_.has_value()) {
            cached_video_params_ = override_params_;
        } else if (source) {
            cached_video_params_ = source->get_video_parameters();
        }
    }
    
    // Override video parameters hint
    std::optional<VideoParameters> get_video_parameters() const override {
        return cached_video_params_;
    }
    
    // Override active line hint (derived from overridden video parameters)
    std::optional<ActiveLineHint> get_active_line_hint() const override {
        if (!cached_video_params_.has_value() || !cached_video_params_->is_valid()) {
            return std::nullopt;
        }
        
        // Use frame-based active line information (chroma decoders work with frames)
        if (cached_video_params_->first_active_frame_line >= 0 && 
            cached_video_params_->last_active_frame_line >= 0) {
            ActiveLineHint hint;
            hint.first_active_frame_line = cached_video_params_->first_active_frame_line;
            hint.last_active_frame_line = cached_video_params_->last_active_frame_line;
            hint.source = HintSource::USER_OVERRIDE;
            hint.confidence_pct = HintTraits::USER_CONFIDENCE;
            return hint;
        }
        
        return std::nullopt;
    }
    
    // Forward get_line to source
    const sample_type* get_line(FieldID id, size_t line) const override {
        return source_ ? source_->get_line(id, line) : nullptr;
    }
    
    // Forward get_field to source
    std::vector<sample_type> get_field(FieldID id) const override {
        return source_ ? source_->get_field(id) : std::vector<sample_type>{};
    }
    
    // Dual-channel support for YC sources
    bool has_separate_channels() const override {
        return source_ ? source_->has_separate_channels() : false;
    }
    
    const sample_type* get_line_luma(FieldID id, size_t line) const override {
        return source_ ? source_->get_line_luma(id, line) : nullptr;
    }
    
    const sample_type* get_line_chroma(FieldID id, size_t line) const override {
        return source_ ? source_->get_line_chroma(id, line) : nullptr;
    }
    
    std::vector<sample_type> get_field_luma(FieldID id) const override {
        return source_ ? source_->get_field_luma(id) : std::vector<sample_type>{};
    }
    
    std::vector<sample_type> get_field_chroma(FieldID id) const override {
        return source_ ? source_->get_field_chroma(id) : std::vector<sample_type>{};
    }

private:
    std::optional<VideoParameters> override_params_;
};

/**
 * @brief Video parameters stage - allows overriding video parameter hints
 * 
 * This stage allows manual override of video parameters that are normally
 * extracted from TBC metadata. This is useful when:
 * - The TBC metadata is incorrect or missing
 * - You want to adjust sample ranges for cropping or processing
 * - You need to override IRE levels or sample rate information
 * - Testing different parameter configurations
 * 
 * Parameters can be set individually - unset parameters are inherited from
 * the input source. This allows partial overrides without specifying all
 * parameters.
 */
class VideoParamsStage : public DAGStage, public ParameterizedStage, public PreviewableStage {
public:
    VideoParamsStage() = default;
    
    // DAGStage interface
    std::string version() const override { return "1.0"; }
    NodeTypeInfo get_node_type_info() const override {
        return NodeTypeInfo{
            NodeType::TRANSFORM,
            "video_params",
            "Video Parameters",
            "Override video parameter hints (dimensions, IRE levels, sample ranges)",
            1, 1,  // Exactly one input
            1, UINT32_MAX,  // Many outputs
            VideoFormatCompatibility::ALL
        };
    }
    std::vector<ArtifactPtr> execute(
        const std::vector<ArtifactPtr>& inputs,
        const std::map<std::string, ParameterValue>& parameters) override;
    
    size_t required_input_count() const override { return 1; }
    size_t output_count() const override { return 1; }
    
    /**
     * @brief Process a field representation (overrides video parameters)
     * 
     * @param source Input field representation
     * @return New representation with overridden video parameters
     */
    std::shared_ptr<const VideoFieldRepresentation> process(
        std::shared_ptr<const VideoFieldRepresentation> source) const;
    
    // ParameterizedStage interface
    std::vector<ParameterDescriptor> get_parameter_descriptors(VideoSystem project_format = VideoSystem::Unknown, SourceType source_type = SourceType::Unknown) const override;
    std::map<std::string, ParameterValue> get_parameters() const override;
    bool set_parameters(const std::map<std::string, ParameterValue>& params) override;
    
    // PreviewableStage interface
    bool supports_preview() const override { return true; }
    std::vector<PreviewOption> get_preview_options() const override;
    PreviewImage render_preview(
        const std::string& option_id,
        uint64_t index,
        PreviewNavigationHint hint) const override;

private:
    /**
     * @brief Build VideoParameters from current parameter values
     * @param source_params Optional source parameters to use as base
     * @return VideoParameters with overrides applied
     */
    std::optional<VideoParameters> build_video_parameters(
        const std::optional<VideoParameters>& source_params) const;

    mutable std::shared_ptr<const VideoFieldRepresentation> cached_output_;
    
    // Parameters - all optional, -1 means "use source value"
    int32_t field_width_ = -1;
    int32_t field_height_ = -1;
    int32_t colour_burst_start_ = -1;
    int32_t colour_burst_end_ = -1;
    int32_t active_video_start_ = -1;
    int32_t active_video_end_ = -1;
    int32_t first_active_field_line_ = -1;
    int32_t last_active_field_line_ = -1;
    int32_t white_16b_ire_ = -1;
    int32_t black_16b_ire_ = -1;
};

} // namespace orc
