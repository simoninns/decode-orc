/*
 * File:        disc_quality_observer.cpp
 * Module:      orc-core
 * Purpose:     Disc quality observer implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "disc_quality_observer.h"
#include "observation_context.h"
#include "video_field_representation.h"
#include "field_id.h"
#include "logging.h"
#include <algorithm>
#include <cmath>

namespace orc {

void DiscQualityObserver::process_field(
    const VideoFieldRepresentation& representation,
    FieldID field_id,
    ObservationContext& context) {
    
    // Calculate quality score
    double quality_score = calculate_quality_score(representation, field_id);
    
    // Get field descriptor
    auto descriptor = representation.get_descriptor(field_id);
    if (!descriptor.has_value()) {
        context.set(field_id, "disc_quality", "quality_score", 0.0);
        context.set(field_id, "disc_quality", "dropout_count", 0);
        context.set(field_id, "disc_quality", "phase_valid", false);
        return;
    }
    
    // Get dropout hints for diagnostics
    auto dropout_hints = representation.get_dropout_hints(field_id);
    
    // Populate context with quality metrics
    context.set(field_id, "disc_quality", "quality_score", quality_score);
    context.set(field_id, "disc_quality", "dropout_count", static_cast<int32_t>(dropout_hints.size()));
    context.set(field_id, "disc_quality", "phase_valid", true);
    
    ORC_LOG_DEBUG("DiscQualityObserver: Field {} quality={:.3f} dropouts={}",
                  field_id.value(), quality_score, dropout_hints.size());
}

double DiscQualityObserver::calculate_quality_score(
    const VideoFieldRepresentation& representation,
    FieldID field_id) const {
    
    double score = 1.0;  // Start with perfect score
    
    // Factor 1: Dropout density
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
    
    // Clamp to [0.0, 1.0]
    score = std::max(0.0, std::min(1.0, score));
    
    return score;
}

} // namespace orc
