/*
 * File:        burst_level_observer.cpp
 * Module:      orc-core
 * Purpose:     Color burst median IRE level observer
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "burst_level_observer.h"
#include "logging.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace orc {

std::vector<std::shared_ptr<Observation>> BurstLevelObserver::process_field(
    const VideoFieldRepresentation& representation,
    FieldID field_id,
    const ObservationHistory& history) {
    (void)history;  // Unused
    
    auto obs = std::make_shared<BurstLevelObservation>();
    obs->field_id = field_id;
    obs->detection_basis = DetectionBasis::SAMPLE_DERIVED;
    obs->observer_version = observer_version();
    obs->observer_parameters = parameters_;
    obs->median_burst_ire = 0.0;
    
    // Get video parameters to find color burst location
    auto video_params_opt = representation.get_video_parameters();
    if (!video_params_opt.has_value()) {
        obs->confidence = ConfidenceLevel::NONE;
        return {obs};
    }
    
    const auto& video_params = video_params_opt.value();
    
    // Check if we have valid color burst range
    if (video_params.colour_burst_start < 0 || video_params.colour_burst_end < 0) {
        obs->confidence = ConfidenceLevel::NONE;
        return {obs};
    }
    
    if (video_params.colour_burst_start >= video_params.colour_burst_end) {
        obs->confidence = ConfidenceLevel::NONE;
        return {obs};
    }
    
    // Get field descriptor
    auto descriptor_opt = representation.get_descriptor(field_id);
    if (!descriptor_opt.has_value()) {
        obs->confidence = ConfidenceLevel::NONE;
        return {obs};
    }
    
    const auto& descriptor = descriptor_opt.value();
    
    // Collect all burst samples from all active lines
    std::vector<double> burst_levels_raw;
    
    // Sample from line 11 to end of active area (matching ld-decode's range 11-313)
    size_t start_line = 11;
    size_t end_line = std::min(descriptor.height - 10, static_cast<size_t>(video_params.last_active_field_line));
    
    // Process every line (not sampled - we want all lines for accurate median)
    for (size_t line = start_line; line < end_line; ++line) {
        const auto* line_data = representation.get_line(field_id, line);
        if (!line_data) {
            continue;
        }
        
        // Extract burst region samples
        size_t burst_start = static_cast<size_t>(video_params.colour_burst_start);
        size_t burst_end = static_cast<size_t>(video_params.colour_burst_end);
        
        // Make sure we don't exceed line width
        if (burst_end >= descriptor.width) {
            burst_end = descriptor.width - 1;
        }
        
        if (burst_end <= burst_start) {
            continue;
        }
        
        // Collect raw samples from this line's burst region
        std::vector<double> line_burst_samples;
        for (size_t sample_idx = burst_start; sample_idx <= burst_end; ++sample_idx) {
            line_burst_samples.push_back(static_cast<double>(line_data[sample_idx]));
        }
        
        if (line_burst_samples.size() < 4) {
            continue;  // Need enough samples
        }
        
        // Calculate mean of burst samples
        double mean = std::accumulate(line_burst_samples.begin(), line_burst_samples.end(), 0.0) / 
                     line_burst_samples.size();
        
        // Subtract mean (remove DC component)
        std::vector<double> centered;
        for (double sample : line_burst_samples) {
            centered.push_back(sample - mean);
        }
        
        // Calculate RMS
        double sum_squares = 0.0;
        for (double val : centered) {
            sum_squares += val * val;
        }
        double rms = std::sqrt(sum_squares / centered.size());
        
        // Convert RMS to peak amplitude: peak = RMS * sqrt(2)
        double peak_amplitude = rms * std::sqrt(2.0);
        
        // Skip if burst level is unreasonably high (> 30 IRE equivalent in raw units)
        double ire_per_unit = 100.0 / static_cast<double>(video_params.white_16b_ire - video_params.black_16b_ire);
        if (peak_amplitude * ire_per_unit > 30.0) {
            continue;  // Skip outliers
        }
        
        burst_levels_raw.push_back(peak_amplitude);
    }
    
    // Calculate median of all collected burst levels (in raw units)
    if (burst_levels_raw.empty()) {
        obs->confidence = ConfidenceLevel::NONE;
        return {obs};
    }
    
    double median_raw = calculate_median(burst_levels_raw);
    
    // Convert to IRE
    double ire_per_unit = 100.0 / static_cast<double>(video_params.white_16b_ire - video_params.black_16b_ire);
    obs->median_burst_ire = median_raw * ire_per_unit;
    obs->confidence = ConfidenceLevel::HIGH;
    
    ORC_LOG_DEBUG("BurstLevelObserver: Field {} median_burst_ire={:.2f}",
                  field_id.value(), obs->median_burst_ire);
    
    return {obs};
}

double BurstLevelObserver::calculate_median(std::vector<double> values) const {
    if (values.empty()) {
        return 0.0;
    }
    
    // Sort the values
    std::sort(values.begin(), values.end());
    
    size_t n = values.size();
    if (n % 2 == 0) {
        // Even number of elements: average the two middle values
        return (values[n/2 - 1] + values[n/2]) / 2.0;
    } else {
        // Odd number of elements: return the middle value
        return values[n/2];
    }
}

double BurstLevelObserver::sample_to_ire(uint16_t sample, uint16_t black_level, uint16_t white_level) const {
    if (white_level <= black_level) {
        return 0.0;  // Invalid levels
    }
    
    // Convert to IRE: black = 0 IRE, white = 100 IRE
    double normalized = static_cast<double>(sample - black_level) / 
                       static_cast<double>(white_level - black_level);
    
    return normalized * 100.0;
}

} // namespace orc
