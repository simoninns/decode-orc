/*
 * File:        field_invert_stage.h
 * Module:      orc-core
 * Purpose:     Field inversion stage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#pragma once

#include "video_field_representation.h"
#include "stage_parameter.h"
#include "dag_executor.h"
#include "preview_renderer.h"
#include <memory>

namespace orc {

/**
 * @brief Wrapper that inverts field parity hints
 */
class InvertedFieldRepresentation : public VideoFieldRepresentationWrapper {
public:
    InvertedFieldRepresentation(
        std::shared_ptr<const VideoFieldRepresentation> source)
        : VideoFieldRepresentationWrapper(
            source,
            ArtifactID("inverted_field"),
            Provenance{})
    {}
    
    // Override parity hint to invert field order
    std::optional<FieldParityHint> get_field_parity_hint(FieldID id) const override {
        auto parity_hint = source_->get_field_parity_hint(id);
        if (!parity_hint.has_value()) {
            return std::nullopt;
        }
        
        // Invert the is_first_field flag
        FieldParityHint inverted_hint = *parity_hint;
        inverted_hint.is_first_field = !parity_hint->is_first_field;
        return inverted_hint;
    }
    
    // Forward get_line to source
    const sample_type* get_line(FieldID id, size_t line) const override {
        return source_ ? source_->get_line(id, line) : nullptr;
    }
    
    // Forward get_field to source
    std::vector<sample_type> get_field(FieldID id) const override {
        return source_ ? source_->get_field(id) : std::vector<sample_type>{};
    }
};

/**
 * @brief Field inversion stage - inverts field order
 * 
 * This stage inverts the field order by flipping the is_first_field hint
 * for all fields. This is useful when the field order detection is incorrect
 * or when you want to deliberately swap field order.
 * 
 * Use cases:
 * - Correcting incorrect field order detection
 * - Testing field order effects
 * - Creating intentionally reversed field order for frame rendering
 */
class FieldInvertStage : public DAGStage, public ParameterizedStage, public PreviewableStage {
public:
    FieldInvertStage() = default;
    
    // DAGStage interface
    std::string version() const override { return "1.0"; }
    NodeTypeInfo get_node_type_info() const override {
        return NodeTypeInfo{
            NodeType::TRANSFORM,
            "field_invert",
            "Field Invert",
            "Invert field order (swap first/second field hints)",
            1, 1,  // Exactly one input
            1, 1   // Exactly one output
        };
    }
    std::vector<ArtifactPtr> execute(
        const std::vector<ArtifactPtr>& inputs,
        const std::map<std::string, ParameterValue>& parameters) override;
    
    size_t required_input_count() const override { return 1; }
    size_t output_count() const override { return 1; }
    
    /**
     * @brief Process a field representation (inverts field order)
     * 
     * @param source Input field representation
     * @return New representation with inverted field order
     */
    std::shared_ptr<const VideoFieldRepresentation> process(
        std::shared_ptr<const VideoFieldRepresentation> source) const;
    
    // ParameterizedStage interface
    std::vector<ParameterDescriptor> get_parameter_descriptors() const override;
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
    mutable std::shared_ptr<const VideoFieldRepresentation> cached_output_;
};

} // namespace orc
