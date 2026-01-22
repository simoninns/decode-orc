/*
 * File:        burst_level_observer.cpp
 * Module:      orc-core
 * Purpose:     Color burst median IRE level observer
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "burst_level_observer.h"
#include "../include/observation_context.h"
#include "../include/video_field_representation.h"
#include "../include/field_id.h"
#include "../include/logging.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace orc {

void BurstLevelObserver::process_field(
    const VideoFieldRepresentation& representation,
    FieldID field_id,
    ObservationContext& context)
{
    // Get video parameters to find color burst location
    auto video_params_opt = representation.get_video_parameters();
    if (!video_params_opt.has_value()) {
        ORC_LOG_TRACE("BurstLevelObserver: No video parameters for field {}", field_id.value());
        return;
    }
    
    const auto& video_params = video_params_opt.value();
    
    // Check if we have valid color burst range
    if (video_params.colour_burst_start < 0 || video_params.colour_burst_end < 0) {
        ORC_LOG_TRACE("BurstLevelObserver: Invalid burst range for field {}", field_id.value());
        return;
    }
    
    if (video_params.colour_burst_start >= video_params.colour_burst_end) {
        ORC_LOG_TRACE("BurstLevelObserver: Invalid burst range for field {}", field_id.value());
        return;
    }
    
    // Get field descriptor
    auto descriptor_opt = representation.get_descriptor(field_id);
    if (!descriptor_opt.has_value()) {
        ORC_LOG_TRACE("BurstLevelObserver: No descriptor for field {}", field_id.value());
        return;
    }
    
    const auto& descriptor = descriptor_opt.value();
    
    // Collect all burst samples from sampled lines
    std::vector<double> burst_levels_raw;
    
    // Sample from line 11 to end of active area
    size_t start_line = 11;
    size_t end_line = std::min(descriptor.height - 10, 
                               static_cast<size_t>(video_params.last_active_field_line));
    
    // Sample just 3 lines (top, middle, bottom) for performance
    std::vector<size_t> sample_lines = {
        start_line,                                    // Top of active area
        start_line + (end_line - start_line) / 2,     // Middle
        end_line - 1                                   // Bottom
    };
    
    for (size_t line : sample_lines) {
        const uint16_t* line_data = representation.get_line(field_id, line);
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
        double mean = std::accumulate(line_burst_samples.begin(), 
                                     line_burst_samples.end(), 0.0) / 
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
        double ire_per_unit = 100.0 / static_cast<double>(
            video_params.white_16b_ire - video_params.black_16b_ire);
        if (peak_amplitude * ire_per_unit > 30.0) {
            continue;  // Skip outliers
        }
        
        burst_levels_raw.push_back(peak_amplitude);
    }
    
    // Calculate median of all collected burst levels (in raw units)
    if (burst_levels_raw.empty()) {
        ORC_LOG_TRACE("BurstLevelObserver: No valid burst samples for field {}", 
                     field_id.value());
        return;
    }
    
    double median_raw = calculate_median(burst_levels_raw);
    
    // Convert to IRE
    double ire_per_unit = 100.0 / static_cast<double>(
        video_params.white_16b_ire - video_params.black_16b_ire);
    double median_burst_ire = median_raw * ire_per_unit;
    
    // Store in observation context
    context.set(field_id, "burst_level", "median_burst_ire", median_burst_ire);
    
    ORC_LOG_DEBUG("BurstLevelObserver: Field {} median_burst_ire={:.2f}",
                  field_id.value(), median_burst_ire);
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

} // namespace orc

