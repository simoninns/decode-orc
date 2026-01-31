/*
 * File:        white_snr_observer.cpp
 * Module:      orc-core
 * Purpose:     White SNR observer implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "white_snr_observer.h"
#include "../include/observation_context.h"
#include "../include/video_field_representation.h"
#include "../include/field_id.h"
#include "../include/logging.h"
#include <cmath>
#include <algorithm>
#include <numeric>

namespace orc {

void WhiteSNRObserver::process_field(
    const VideoFieldRepresentation& representation,
    FieldID field_id,
    ObservationContext& context)
{
    // Get field descriptor to determine format
    auto descriptor_opt = representation.get_descriptor(field_id);
    if (!descriptor_opt.has_value()) {
        ORC_LOG_TRACE("WhiteSNRObserver: No descriptor for field {}", field_id.value());
        return;
    }
    
    const auto& descriptor = descriptor_opt.value();
    VideoFormat format = descriptor.format;
    
    // VITS white flag locations (from ld-process-vits)
    // PAL: Line 19, 12μs start, 8μs length
    // NTSC: Line 20 (14μs, 12μs), Line 20 (52μs, 8μs), Line 13 (13μs, 15μs)
    // (Same line numbers for both top and bottom fields - using field-local line numbering)
    
    struct WhiteConfig {
        size_t line;
        double start_us;
        double length_us;
    };
    
    std::vector<WhiteConfig> configs;
    if (format == VideoFormat::PAL) {
        // PAL configurations (from ld-process-vits)
        configs = {
            {19, 12.0, 8.0}  // Line 19, start 12µs, length 8µs
        };
    } else {
        // NTSC: try primary VITS locations, then one-line offset to cover field-parity half-line differences
        // NTSC configurations (from ld-process-vits); same lines for both fields (field-local numbering)
        configs = {
            {20, 14.0, 12.0}, // Line 20, start 14µs, length 12µs
            {20, 52.0, 8.0},  // Line 20, start 52µs, length 8µs
            {13, 13.0, 15.0}  // Line 13, start 13µs, length 15µs
        };
    }
    
    // Validation range for white level (90-110 IRE)
    double white_ire_min = 90.0;
    double white_ire_max = 110.0;
    
    // Try each configuration until we find valid white flag
    for (const auto& config : configs) {
        auto white_slice = get_line_slice_ire(representation, field_id,
                                              config.line, config.start_us, config.length_us);
        
        if (white_slice.empty()) {
            ORC_LOG_TRACE("WhiteSNRObserver: Field {} line {} slice empty", field_id.value(), config.line);
            continue;
        }
        
        // Validate white level is in acceptable range
        double white_mean = calc_mean(white_slice);
        if (white_mean >= white_ire_min && white_mean <= white_ire_max) {
            double noise_std = calc_std(white_slice);
            double snr_db = calculate_psnr(white_slice);
            
            // Store in observation context
            context.set(field_id, "white_snr", "snr_db", snr_db);
            
            ORC_LOG_DEBUG("WhiteSNRObserver: Field {} snr={:.2f} dB (mean={:.1f} IRE, std={:.3f})",
                         field_id.value(), snr_db, white_mean, noise_std);
            return;
        } else {
            ORC_LOG_DEBUG("WhiteSNRObserver: Field {} line {} mean outside range ({:.1f} IRE)",
                         field_id.value(), config.line, white_mean);
        }
    }
    
    ORC_LOG_DEBUG("WhiteSNRObserver: No valid white flag found for field {}", field_id.value());
}

std::vector<double> WhiteSNRObserver::get_line_slice_ire(
    const VideoFieldRepresentation& representation,
    FieldID field_id,
    size_t field_line,
    double start_us,
    double length_us) const
{
    std::vector<double> result;
    
    auto descriptor_opt = representation.get_descriptor(field_id);
    if (!descriptor_opt.has_value()) {
        return result;
    }
    
    const auto& descriptor = descriptor_opt.value();
    
    // Adjust for 1-based line numbering
    size_t line_index = field_line - 1;
    
    // Range check
    if (line_index >= descriptor.height) {
        return result;
    }
    
    // Calculate samples per microsecond
    VideoFormat format = descriptor.format;
    double us_per_line = (format == VideoFormat::PAL) ? 64.0 : 63.5;
    double samples_per_us = static_cast<double>(descriptor.width) / us_per_line;
    
    // Calculate sample positions
    size_t start_sample = static_cast<size_t>(start_us * samples_per_us);
    size_t length_samples = static_cast<size_t>(length_us * samples_per_us);
    
    // Range check
    if (start_sample + length_samples > descriptor.width) {
        return result;
    }
    
    // Get the line data
    const uint16_t* line_data = representation.get_line(field_id, line_index);
    if (!line_data) {
        return result;
    }
    
    // Get video parameters for IRE conversion
    auto video_params_opt = representation.get_video_parameters();
    double black_16b = 16384.0;  // Default
    double white_16b = 53248.0;  // Default
    
    if (video_params_opt.has_value()) {
        black_16b = static_cast<double>(video_params_opt->black_16b_ire);
        white_16b = static_cast<double>(video_params_opt->white_16b_ire);
    }
    
    double ire_scale = 100.0 / (white_16b - black_16b);
    
    // Convert samples to IRE values
    result.reserve(length_samples);
    for (size_t i = 0; i < length_samples; ++i) {
        uint16_t sample = line_data[start_sample + i];
        double ire = (static_cast<double>(sample) - black_16b) * ire_scale;
        result.push_back(ire);
    }
    
    return result;
}

double WhiteSNRObserver::calculate_psnr(const std::vector<double>& data) const {
    if (data.empty()) {
        return 0.0;
    }
    
    // White SNR uses the mean of the data as the signal (not a fixed reference)
    double signal = calc_mean(data);
    double noise = calc_std(data);
    
    // For very low noise (essentially perfect signal), cap at a reasonable maximum
    if (noise <= 0.001) {
        return 80.0;  // Cap at 80 dB for effectively noiseless signals
    }
    
    return 20.0 * std::log10(signal / noise);
}

double WhiteSNRObserver::calc_mean(const std::vector<double>& data) const {
    if (data.empty()) {
        return 0.0;
    }
    
    double sum = std::accumulate(data.begin(), data.end(), 0.0);
    return sum / static_cast<double>(data.size());
}

double WhiteSNRObserver::calc_std(const std::vector<double>& data) const {
    if (data.empty()) {
        return 0.0;
    }
    
    double mean = calc_mean(data);
    double sum_squared_diff = 0.0;
    
    for (double value : data) {
        double diff = value - mean;
        sum_squared_diff += diff * diff;
    }
    
    return std::sqrt(sum_squared_diff / static_cast<double>(data.size()));
}

} // namespace orc
