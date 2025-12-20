/*
 * File:        pal_phase_observer.cpp
 * Module:      orc-core
 * Purpose:     PAL field phase ID observer
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "pal_phase_observer.h"
#include "observation_history.h"
#include "logging.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace orc {

std::vector<std::shared_ptr<Observation>> PALPhaseObserver::process_field(
    const VideoFieldRepresentation& representation,
    FieldID field_id,
    const ObservationHistory& history) {
    
    auto obs = std::make_shared<PALPhaseObservation>();
    obs->field_id = field_id;
    obs->detection_basis = DetectionBasis::SAMPLE_DERIVED;
    obs->observer_version = observer_version();
    obs->observer_parameters = parameters_;
    obs->field_phase_id = -1;  // Unknown by default
    
    // Get video parameters
    auto video_params_opt = representation.get_video_parameters();
    if (!video_params_opt.has_value()) {
        obs->confidence = ConfidenceLevel::NONE;
        return {obs};
    }
    
    const auto& video_params = video_params_opt.value();
    
    // Only works for PAL
    if (video_params.system != VideoSystem::PAL) {
        obs->confidence = ConfidenceLevel::NONE;
        return {obs};
    }
    
    // Get field parity from observation history (if FieldParityObserver ran before us)
    // This is critical for correct PAL phase detection!
    bool is_first_field = false;
    auto parity_from_history = history.get_observation(field_id, "FieldParity");
    
    if (parity_from_history) {
        // Use the already-calculated parity from history
        auto* parity_obs = dynamic_cast<FieldParityObservation*>(parity_from_history.get());
        if (parity_obs && parity_obs->confidence_pct >= 25) {
            is_first_field = parity_obs->is_first_field;
        } else {
            obs->confidence = ConfidenceLevel::LOW;
            return {obs};  // Can't determine field parity reliably
        }
    } else {
        // Fallback: Calculate field parity ourselves (if FieldParityObserver not in pipeline)
        FieldParityObserver parity_observer;
        auto parity_observations = parity_observer.process_field(representation, field_id, history);
        
        if (parity_observations.empty()) {
            obs->confidence = ConfidenceLevel::NONE;
            return {obs};
        }
        
        auto* parity_obs = dynamic_cast<FieldParityObservation*>(parity_observations[0].get());
        if (!parity_obs || parity_obs->confidence_pct < 25) {
            obs->confidence = ConfidenceLevel::LOW;
            return {obs};  // Can't determine field parity reliably
        }
        is_first_field = parity_obs->is_first_field;
    }
    
    // PAL has line offsets (ld-decode: lineoffset = 2 for first field, 3 for second)
    size_t line_offset = is_first_field ? 2 : 3;
    
    // Get median burst level for comparison
    double median_burst = 0.0;
    std::vector<double> burst_levels;
    for (size_t line = 11; line < 300; ++line) {
        auto level_opt = get_burst_level(representation, field_id, line, video_params);
        if (level_opt.has_value()) {
            burst_levels.push_back(level_opt.value());
        }
    }
    
    if (burst_levels.empty()) {
        obs->confidence = ConfidenceLevel::NONE;
        return {obs};
    }
    
    std::sort(burst_levels.begin(), burst_levels.end());
    median_burst = burst_levels[burst_levels.size() / 2];
    
    // Step 1: Determine 4-field sequence based on burst presence on line 6
    // PAL line numbering accounts for field offset (line 6 + lineoffset)
    auto burst6_opt = get_burst_level(representation, field_id, 6 + line_offset, video_params);
    if (!burst6_opt.has_value()) {
        obs->confidence = ConfidenceLevel::LOW;
        return {obs};  // Can't determine without line 6 burst info
    }
    
    double burst6_level = burst6_opt.value();
    
    // Determine if line 6 has valid burst
    bool has_burst_line6;
    if (burst6_level >= median_burst * 0.8 && burst6_level <= median_burst * 1.2) {
        has_burst_line6 = true;
    } else if (burst6_level < median_burst * 0.2) {
        has_burst_line6 = false;
    } else {
        // Ambiguous burst level
        obs->confidence = ConfidenceLevel::LOW;
        return {obs};
    }
    
    // Map to 4-field sequence
    // (first field, has burst on line 6) → phase
    int phase_4field;
    if (is_first_field && !has_burst_line6) {
        phase_4field = 1;
    } else if (!is_first_field && has_burst_line6) {
        phase_4field = 2;
    } else if (is_first_field && has_burst_line6) {
        phase_4field = 3;
    } else {  // !is_first_field && !has_burst_line6
        phase_4field = 4;
    }
    
    // Step 2: Determine if it's fields 1-4 or 5-8 by checking burst phase on lines 7,11,15,19 (accounting for line offset)
    int rising_count = 0;
    int total_count = 0;
    
    for (size_t line : {7, 11, 15, 19}) {
        auto rising_opt = compute_line_burst_rising(representation, field_id, line + line_offset, video_params);
        if (rising_opt.has_value()) {
            if (rising_opt.value()) {
                rising_count++;
            }
            total_count++;
        }
    }
    
    if (total_count == 0 || (rising_count * 2) == total_count) {
        // Can't determine or exactly 50/50
        obs->confidence = ConfidenceLevel::LOW;
        obs->field_phase_id = phase_4field;  // At least we know 1-4 sequence
        return {obs};
    }
    
    // Determine if it's first four or second four
    bool is_first_four = (rising_count * 2) > total_count;
    
    // For field 2/6, reverse the determination
    if (phase_4field == 2) {
        is_first_four = !is_first_four;
    }
    
    // Final phase ID
    obs->field_phase_id = phase_4field + (is_first_four ? 0 : 4);
    obs->confidence = (total_count >= 3) ? ConfidenceLevel::HIGH : ConfidenceLevel::MEDIUM;
    
    ORC_LOG_DEBUG("PALPhaseObserver: Field {} phase_id={} (confidence={})",
                  field_id.value(), obs->field_phase_id, 
                  obs->confidence == ConfidenceLevel::HIGH ? "HIGH" : 
                  obs->confidence == ConfidenceLevel::MEDIUM ? "MEDIUM" : 
                  obs->confidence == ConfidenceLevel::LOW ? "LOW" : "NONE");
    
    return {obs};
}

std::optional<double> PALPhaseObserver::get_burst_level(
    const VideoFieldRepresentation& representation,
    FieldID field_id,
    size_t line,
    const VideoParameters& video_params) const
{
    const auto* line_data = representation.get_line(field_id, line);
    if (!line_data) {
        return std::nullopt;
    }
    
    size_t burst_start = static_cast<size_t>(video_params.colour_burst_start);
    size_t burst_end = static_cast<size_t>(video_params.colour_burst_end);
    
    if (burst_end <= burst_start) {
        return std::nullopt;
    }
    
    // Collect burst samples
    std::vector<double> burst_samples;
    for (size_t idx = burst_start; idx <= burst_end; ++idx) {
        burst_samples.push_back(static_cast<double>(line_data[idx]));
    }
    
    if (burst_samples.size() < 4) {
        return std::nullopt;
    }
    
    // Calculate mean and subtract it
    double mean = std::accumulate(burst_samples.begin(), burst_samples.end(), 0.0) / burst_samples.size();
    
    std::vector<double> centered;
    for (double sample : burst_samples) {
        centered.push_back(sample - mean);
    }
    
    // Calculate RMS * sqrt(2) to get peak amplitude
    double rms = calculate_rms(centered);
    return rms * std::sqrt(2.0);
}

std::optional<bool> PALPhaseObserver::compute_line_burst_rising(
    const VideoFieldRepresentation& representation,
    FieldID field_id,
    size_t line,
    const VideoParameters& video_params) const
{
    const auto* line_data = representation.get_line(field_id, line);
    if (!line_data) {
        return std::nullopt;
    }
    
    size_t burst_start = static_cast<size_t>(video_params.colour_burst_start);
    size_t burst_end = static_cast<size_t>(video_params.colour_burst_end);
    
    if (burst_end <= burst_start) {
        return std::nullopt;
    }
    
    // Collect burst samples
    std::vector<double> burst_samples;
    for (size_t idx = burst_start; idx <= burst_end; ++idx) {
        burst_samples.push_back(static_cast<double>(line_data[idx]));
    }
    
    if (burst_samples.size() < 8) {
        return std::nullopt;
    }
    
    // Remove DC component
    double mean = std::accumulate(burst_samples.begin(), burst_samples.end(), 0.0) / burst_samples.size();
    
    std::vector<double> centered;
    for (double sample : burst_samples) {
        centered.push_back(sample - mean);
    }
    
    // Calculate RMS for threshold
    double rms = calculate_rms(centered);
    double threshold = rms;
    
    if (threshold < 1.0) {
        return std::nullopt;  // Signal too weak
    }
    
    // Count zero crossings and determine if they're rising or falling
    int rising_count = 0;
    int total_crossings = 0;
    
    for (size_t i = 1; i < centered.size(); ++i) {
        // Check for zero crossing
        if ((centered[i-1] < 0 && centered[i] >= 0) || (centered[i-1] >= 0 && centered[i] < 0)) {
            total_crossings++;
            
            // Check if magnitude is significant
            if (std::abs(centered[i]) > threshold * 0.3) {
                // Rising if crossing from negative to positive
                if (centered[i-1] < 0 && centered[i] >= 0) {
                    rising_count++;
                }
            }
        }
    }
    
    if (total_crossings < 8) {
        return std::nullopt;  // Not enough crossings for valid burst
    }
    
    // Majority of crossings should be rising or falling
    return rising_count > (total_crossings / 2);
}

double PALPhaseObserver::calculate_rms(const std::vector<double>& data) const {
    if (data.empty()) {
        return 0.0;
    }
    
    double sum_squares = 0.0;
    for (double val : data) {
        sum_squares += val * val;
    }
    
    return std::sqrt(sum_squares / data.size());
}

} // namespace orc
