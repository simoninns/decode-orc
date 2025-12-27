/*
 * File:        dropout_analysis_observer.cpp
 * Module:      orc-core
 * Purpose:     Dropout analysis observer implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "dropout_analysis_observer.h"
#include "observation_history.h"
#include "biphase_observer.h"
#include "logging.h"
#include <algorithm>

namespace orc {

std::vector<std::shared_ptr<Observation>> DropoutAnalysisObserver::process_field(
    const VideoFieldRepresentation& representation,
    FieldID field_id,
    const ObservationHistory& history)
{
    auto observation = std::make_shared<DropoutAnalysisObservation>();
    observation->field_id = field_id;
    observation->detection_basis = DetectionBasis::HINT_DERIVED;
    observation->confidence = ConfidenceLevel::HIGH;
    observation->observer_version = observer_version();
    observation->mode = mode_;
    
    // Get dropout hints from the source
    auto dropout_hints = representation.get_dropout_hints(field_id);
    observation->dropout_count = dropout_hints.size();
    
    // Calculate total dropout length based on mode
    if (mode_ == DropoutAnalysisMode::FULL_FIELD) {
        observation->total_dropout_length = 
            calculate_full_field_dropout_length(dropout_hints);
    } else {
        observation->total_dropout_length = 
            calculate_visible_area_dropout_length(dropout_hints, representation, field_id);
    }
    
    // Try to get frame number from VBI observations
    auto observations = history.get_observations(field_id, "Biphase");
    if (!observations.empty()) {
        auto biphase_obs = std::dynamic_pointer_cast<BiphaseObservation>(observations[0]);
        if (biphase_obs && biphase_obs->picture_number.has_value()) {
            observation->frame_number = biphase_obs->picture_number;
        }
    }
    
    ORC_LOG_TRACE("DropoutAnalysisObserver: Field {} mode={} count={} length={:.1f}",
                  field_id.value(), 
                  mode_ == DropoutAnalysisMode::FULL_FIELD ? "FULL" : "VISIBLE",
                  observation->dropout_count,
                  observation->total_dropout_length);
    
    return {observation};
}

double DropoutAnalysisObserver::calculate_full_field_dropout_length(
    const std::vector<DropoutRegion>& dropouts) const
{
    double total_length = 0.0;
    
    for (const auto& dropout : dropouts) {
        total_length += static_cast<double>(dropout.end_sample - dropout.start_sample);
    }
    
    return total_length;
}

double DropoutAnalysisObserver::calculate_visible_area_dropout_length(
    const std::vector<DropoutRegion>& dropouts,
    const VideoFieldRepresentation& representation,
    FieldID field_id) const
{
    double visible_length = 0.0;
    
    // Get video parameters to determine visible area
    auto video_params_opt = representation.get_video_parameters();
    if (!video_params_opt.has_value()) {
        ORC_LOG_WARN("DropoutAnalysisObserver: No video parameters available, using full field");
        return calculate_full_field_dropout_length(dropouts);
    }
    
    const auto& video_params = video_params_opt.value();
    
    // Check if we have valid active area parameters
    if (video_params.first_active_field_line < 0 || 
        video_params.last_active_field_line < 0 ||
        video_params.active_video_start < 0 ||
        video_params.active_video_end < 0) {
        ORC_LOG_WARN("DropoutAnalysisObserver: Invalid active area parameters, using full field");
        return calculate_full_field_dropout_length(dropouts);
    }
    
    // Process each dropout region
    for (const auto& dropout : dropouts) {
        // Check if dropout is in the visible vertical range
        if (dropout.line < static_cast<uint32_t>(video_params.first_active_field_line) ||
            dropout.line > static_cast<uint32_t>(video_params.last_active_field_line)) {
            continue;  // Skip dropouts outside visible vertical range
        }
        
        // Check if dropout overlaps with visible horizontal range
        int32_t dropout_start = static_cast<int32_t>(dropout.start_sample);
        int32_t dropout_end = static_cast<int32_t>(dropout.end_sample);
        
        if (dropout_start < video_params.active_video_end && 
            dropout_end > video_params.active_video_start) {
            // Clamp to active video boundaries
            int32_t visible_start = std::max(dropout_start, video_params.active_video_start);
            int32_t visible_end = std::min(dropout_end, video_params.active_video_end);
            
            if (visible_end > visible_start) {
                visible_length += static_cast<double>(visible_end - visible_start);
            }
        }
    }
    
    return visible_length;
}

} // namespace orc
