/*
 * File:        source_align_stage.h
 * Module:      orc-core
 * Purpose:     Source alignment stage for synchronizing multiple sources
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include "video_field_representation.h"
#include "stage_parameter.h"
#include "dag_executor.h"
#include "previewable_stage.h"
#include <memory>
#include <vector>
#include <string>

namespace orc {

/**
 * @brief Source alignment stage that synchronizes multiple sources
 * 
 * This Many-to-Many stage takes multiple input sources and aligns them
 * by finding the first common field across all sources based on VBI 
 * frame numbers (CAV) or CLV timecodes. It then drops fields from each
 * source as needed so that output field_id 0, 1, 2, 3, etc. represent
 * the same actual field from all sources.
 * 
 * This is critical after field_map stages which may output padded fields,
 * since there's no guarantee that field_id 0 from different sources
 * represents the same VBI frame number or timecode.
 * 
 * Use cases:
 * - Aligning multiple TBC captures of the same disc before stacking
 * - Synchronizing sources that started at different disc positions
 * - Ensuring consistent field numbering across multiple processing chains
 */
class SourceAlignStage : public DAGStage, public ParameterizedStage, public PreviewableStage {
public:
    SourceAlignStage() = default;
    
    // DAGStage interface
    std::string version() const override { return "1.0"; }
    
    NodeTypeInfo get_node_type_info() const override {
        return NodeTypeInfo{
            NodeType::COMPLEX,  // Many-to-Many
            "source_align",
            "Source Align",
            "Synchronize multiple sources by VBI frame number or CLV timecode",
            1, 16,  // 1 to 16 inputs
            2, UINT32_MAX,  // Many outputs (one per input, min 2 to indicate multiple artifacts)
            VideoFormatCompatibility::ALL
        };
    }
    
    std::vector<ArtifactPtr> execute(
        const std::vector<ArtifactPtr>& inputs,
        const std::map<std::string, ParameterValue>& parameters) override;
    
    // PreviewableStage interface
    bool supports_preview() const override { return true; }
    std::vector<PreviewOption> get_preview_options() const override;
    PreviewImage render_preview(const std::string& option_id, uint64_t index,
                               PreviewNavigationHint hint = PreviewNavigationHint::Random) const override;
    
    size_t required_input_count() const override { return 1; }
    size_t output_count() const override { return UINT32_MAX; }  // Same as input count
    
    // Stage inspection
    std::optional<StageReport> generate_report() const override;
    
    // ParameterizedStage interface
    std::vector<ParameterDescriptor> get_parameter_descriptors(VideoSystem project_format = VideoSystem::Unknown) const override;
    std::map<std::string, ParameterValue> get_parameters() const override;
    bool set_parameters(const std::map<std::string, ParameterValue>& params) override;

private:
    /**
     * @brief Find the first common field across all sources
     * @param sources Input sources to align
     * @return Vector of field offsets for each source (field_id to start from)
     */
    std::vector<FieldID> find_alignment_offsets(
        const std::vector<std::shared_ptr<const VideoFieldRepresentation>>& sources) const;
    
    /**
     * @brief Get VBI frame number or CLV timecode frame equivalent for a field
     * @return Frame number if found, -1 otherwise
     */
    int32_t get_frame_number_from_vbi(
        const VideoFieldRepresentation& source,
        FieldID field_id) const;
    
    /**
     * @brief Parse alignment map specification string
     * @param alignment_spec String like "1+2, 2+2, 3+1, 4+1"
     * @return Vector of offsets for each input (1-indexed input IDs)
     */
    static std::vector<std::pair<size_t, size_t>> parse_alignment_map(const std::string& alignment_spec);
    
    /**
     * @brief Apply field order enforcement (ensure first field is always first)
     * @param offsets Current alignment offsets
     * @param sources Input sources
     * @return Modified offsets with enforcement applied
     */
    std::vector<FieldID> apply_field_order_enforcement(
        std::vector<FieldID> offsets,
        const std::vector<std::shared_ptr<const VideoFieldRepresentation>>& sources) const;
    
    // Store alignment information for reporting
    mutable std::vector<FieldID> alignment_offsets_;
    mutable std::vector<std::shared_ptr<const VideoFieldRepresentation>> input_sources_;
    
    // Cached outputs for preview rendering
    mutable std::vector<std::shared_ptr<const VideoFieldRepresentation>> cached_outputs_;
    
    // Parameters
    std::string alignment_map_;
    bool enforce_field_order_ = true;
};

} // namespace orc
