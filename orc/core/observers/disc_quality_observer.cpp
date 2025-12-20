/*
 * File:        disc_quality_observer.cpp
 * Module:      orc-core
 * Purpose:     Disc quality observer implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "disc_quality_observer.h"
#include "observation_history.h"
#include "logging.h"
#include <algorithm>
#include <cmath>

namespace orc {

std::vector<std::shared_ptr<Observation>> DiscQualityObserver::process_field(
    const VideoFieldRepresentation& representation,
    FieldID field_id,
    const ObservationHistory& history) {
    
    std::vector<std::shared_ptr<Observation>> observations;
    auto observation = std::make_shared<DiscQualityObservation>();
    observation->field_id = field_id;
    observation->detection_basis = DetectionBasis::SAMPLE_DERIVED;
    observation->observer_version = observer_version();
    
    // Get field descriptor
    auto descriptor = representation.get_descriptor(field_id);
    if (!descriptor.has_value()) {
        observation->confidence = ConfidenceLevel::NONE;
        observation->quality_score = 0.0;
        observations.push_back(observation);
        return observations;
    }
    
    // Calculate quality score
    observation->quality_score = calculate_quality_score(representation, field_id, history);
    
    // Get dropout hints for diagnostics
    auto dropout_hints = representation.get_dropout_hints(field_id);
    observation->dropout_count = dropout_hints.size();
    
    // Check phase correctness from observation history
    auto phase_obs = history.get_observation(field_id, "PALPhase");
    if (phase_obs) {
        // Phase observation exists and is valid
        observation->has_valid_phase = true;
    }
    
    // Set confidence based on data availability
    if (observation->dropout_count > 0 || phase_obs) {
        observation->confidence = ConfidenceLevel::HIGH;
    } else {
        observation->confidence = ConfidenceLevel::MEDIUM;
    }
    
    ORC_LOG_DEBUG("DiscQualityObserver: Field {} quality={:.3f} dropouts={}",
                  field_id.value(), observation->quality_score, observation->dropout_count);
    
    observations.push_back(observation);
    return observations;
}

double DiscQualityObserver::calculate_quality_score(
    const VideoFieldRepresentation& representation,
    FieldID field_id,
    const ObservationHistory& history) const {
    
    double score = 1.0;  // Start with perfect score
    
    // Factor 1: Dropout density (most important)
    auto dropout_hints = representation.get_dropout_hints(field_id);
    if (!dropout_hints.empty()) {
        auto descriptor = representation.get_descriptor(field_id);
        if (descriptor.has_value()) {
            // Calculate total dropout samples
            size_t total_dropout_samples = 0;
            for (const auto& region : dropout_hints) {
                total_dropout_samples += (region.end_sample - region.start_sample);
            }
            
            // Total field samples
            size_t total_samples = descriptor->width * descriptor->height;
            
            // Dropout ratio (0.0 = none, 1.0 = entire field)
            double dropout_ratio = static_cast<double>(total_dropout_samples) / 
                                   static_cast<double>(total_samples);
            
            // Penalize heavily for dropouts (exponential)
            score *= std::exp(-10.0 * dropout_ratio);
        }
    }
    
    // Factor 2: VITS quality metrics (if available)
    auto vits_obs = history.get_observation(field_id, "VITSQuality");
    if (vits_obs) {
        // TODO: Extract SNR or quality metrics from VITS observation
        // For now, boost score slightly if VITS present (indicates good signal)
        score *= 1.05;  // 5% bonus for having VITS
    }
    
    // Clamp to [0.0, 1.0]
    score = std::max(0.0, std::min(1.0, score));
    
    return score;
}

} // namespace orc
