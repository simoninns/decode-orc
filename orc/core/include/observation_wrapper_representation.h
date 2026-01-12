/*
 * File:        observation_wrapper_representation.h
 * Module:      orc-core
 * Purpose:     Wrapper that attaches observations to field representations
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include "video_field_representation.h"
#include "observer.h"
#include <map>
#include <memory>
#include <vector>

namespace orc {

/**
 * @brief Wrapper that attaches computed observations to a VideoFieldRepresentation
 * 
 * This wrapper is used by DAGFieldRenderer to provide observations alongside
 * the field data, enabling GUI features like VBI display without needing
 * full DAG execution with ObservationHistory.
 */
class ObservationWrapperRepresentation : public VideoFieldRepresentationWrapper {
public:
    /**
     * @brief Construct a wrapper with observations
     * 
     * @param source The source field representation
     * @param observations_map Map of field_id -> observations for that field
     */
    ObservationWrapperRepresentation(
        std::shared_ptr<VideoFieldRepresentation> source,
        std::map<FieldID, std::vector<std::shared_ptr<Observation>>> observations_map
    );
    
    ~ObservationWrapperRepresentation() override = default;
    
    // Forward data access to source
    const sample_type* get_line(FieldID id, size_t line) const override {
        return source_->get_line(id, line);
    }
    
    std::vector<sample_type> get_field(FieldID id) const override {
        return source_->get_field(id);
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
    
    // Override get_observations to return our computed observations
    std::vector<std::shared_ptr<Observation>> get_observations(FieldID id) const override;
    
    std::string type_name() const override { 
        return "ObservationWrapperRepresentation"; 
    }

private:
    std::map<FieldID, std::vector<std::shared_ptr<Observation>>> observations_map_;
};

} // namespace orc
