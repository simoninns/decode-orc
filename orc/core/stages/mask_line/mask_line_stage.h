/*
 * File:        mask_line_stage.h
 * Module:      orc-core
 * Purpose:     Line masking stage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include "video_field_representation.h"
#include "stage_parameter.h"
#include "dag_executor.h"
#include "preview_renderer.h"
#include <memory>
#include <vector>
#include <cstdint>

namespace orc {

/**
 * @brief Wrapper that masks (blanks) specified lines in fields
 */
class MaskedLineRepresentation : public VideoFieldRepresentationWrapper {
public:
    MaskedLineRepresentation(
        std::shared_ptr<const VideoFieldRepresentation> source,
        const std::string& line_spec,
        double mask_ire);
    
    // Override get_line to return masked data for specified lines
    const sample_type* get_line(FieldID id, size_t line) const override;
    
    // Override get_field to return masked field data
    std::vector<sample_type> get_field(FieldID id) const override;
    
    // Dual-channel support for YC sources
    bool has_separate_channels() const override {
        return source_ ? source_->has_separate_channels() : false;
    }
    
    const sample_type* get_line_luma(FieldID id, size_t line) const override;
    const sample_type* get_line_chroma(FieldID id, size_t line) const override;
    std::vector<sample_type> get_field_luma(FieldID id) const override;
    std::vector<sample_type> get_field_chroma(FieldID id) const override;

private:
    struct LineRange {
        char parity;  // 'F' = first, 'S' = second, 'A' = all
        size_t start;
        size_t end;
    };
    
    bool should_mask_line(FieldID field_id, size_t line_num) const;
    void parse_line_spec(const std::string& line_spec);
    uint16_t ire_to_sample(double ire) const;
    
    std::vector<LineRange> line_ranges_;  // List of ranges with parity
    double mask_ire_;  // IRE value (0-100)
    
    // Cache for masked lines (mutable to allow caching in const methods)
    mutable std::unordered_map<FieldID, std::unordered_map<size_t, std::vector<uint16_t>>> masked_line_cache_;
    
    // Separate caches for YC sources
    mutable std::unordered_map<FieldID, std::unordered_map<size_t, std::vector<uint16_t>>> masked_luma_cache_;
    mutable std::unordered_map<FieldID, std::unordered_map<size_t, std::vector<uint16_t>>> masked_chroma_cache_;
};

/**
 * @brief Line masking stage - masks specified lines in specified fields
 * 
 * This stage allows masking (blanking) specific lines in fields based on
 * field parity and line numbers. Common uses include:
 * - Masking NTSC closed caption line (field line 20, first field only - traditional "line 21" is index 20)
 * - Removing visible VBI data
 * - Hiding other unwanted visible information on specific lines
 * 
 * Line specification format: PARITY:LINE or PARITY:START-END
 * - F: = First field only
 * - S: = Second field only  
 * - A: = All fields (both)
 * Examples: "F:20" (NTSC CC), "S:6-22" (second field lines 6-22), "A:10,F:20"
 * 
 * Mask value is in IRE units (0-100), where 0 = black, 100 = white.
 * Line numbers are 0-based field line numbers (not frame line numbers).
 */
class MaskLineStage : public DAGStage, public ParameterizedStage, public PreviewableStage {
public:
    MaskLineStage() = default;
    
    // DAGStage interface
    std::string version() const override { return "1.0"; }
    NodeTypeInfo get_node_type_info() const override {
        return NodeTypeInfo{
            NodeType::TRANSFORM,
            "mask_line",
            "Mask Line",
            "Mask (blank) specified lines in fields by parity",
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
     * @brief Process a field representation (masks specified lines)
     * 
     * @param source Input field representation
     * @return New representation with masked lines
     */
    std::shared_ptr<const VideoFieldRepresentation> process(
        std::shared_ptr<const VideoFieldRepresentation> source) const;
    
    // ParameterizedStage interface
    std::vector<ParameterDescriptor> get_parameter_descriptors(VideoSystem project_format = VideoSystem::Unknown) const override;
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
    std::string line_spec_ = "";  // e.g., "F:21" or "S:15-17,A:21"
    double mask_ire_ = 0.0;  // IRE value, 0-100 (default: 0 = black)
    
    mutable std::shared_ptr<const VideoFieldRepresentation> cached_output_;
};

} // namespace orc
