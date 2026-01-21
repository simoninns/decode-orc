/*
 * File:        black_psnr_observer.cpp
 * Module:      orc-core
 * Purpose:     Black PSNR observer implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "black_psnr_observer.h"
#include "../include/observation_context.h"
#include "../include/video_field_representation.h"
#include "../include/field_id.h"
#include "../include/logging.h"
#include <cmath>
#include <algorithm>
#include <numeric>

namespace orc {

void BlackPSNRObserver::process_field(
    const VideoFieldRepresentation& representation,
    FieldID field_id,
    ObservationContext& context)
{
    // Get field descriptor to determine format
    auto descriptor_opt = representation.get_descriptor(field_id);
    if (!descriptor_opt.has_value()) {
        ORC_LOG_TRACE("BlackPSNRObserver: No descriptor for field {}", field_id.value());
        return;
    }
    
    const auto& descriptor = descriptor_opt.value();
    VideoFormat format = descriptor.format;
    
    // VITS black level locations (from ld-process-vits)
    // PAL: Line 22, 12μs start, 50μs length
    // NTSC: Line 1, 10μs start, 20μs length
    
    size_t line;
    double start_us;
    double length_us;
    
    if (format == VideoFormat::PAL) {
        line = 22;
        start_us = 12.0;
        length_us = 50.0;
    } else {
        line = 1;
        start_us = 10.0;
        length_us = 20.0;
    }
    
    auto black_slice = get_line_slice_ire(representation, field_id, line, start_us, length_us);
    
    if (black_slice.empty()) {
        ORC_LOG_TRACE("BlackPSNRObserver: No valid black level data for field {}", field_id.value());
        return;
    }
    
    double noise_std = calc_std(black_slice);
    double black_mean = calc_mean(black_slice);
    double psnr_db = calculate_psnr(black_slice);
    
    // Store in observation context
    context.set(field_id, "black_psnr", "psnr_db", psnr_db);
    
    ORC_LOG_DEBUG("BlackPSNRObserver: Field {} psnr={:.2f} dB (mean={:.1f} IRE, std={:.3f})",
                 field_id.value(), psnr_db, black_mean, noise_std);
}

std::vector<double> BlackPSNRObserver::get_line_slice_ire(
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

double BlackPSNRObserver::calculate_psnr(const std::vector<double>& data) const {
    if (data.empty()) {
        return 0.0;
    }
    
    // PSNR uses 100 IRE as the reference signal
    double signal = 100.0;
    double noise = calc_std(data);
    
    // For very low noise (essentially perfect signal), cap at a reasonable maximum
    if (noise <= 0.001) {
        return 80.0;  // Cap at 80 dB for effectively noiseless signals
    }
    
    return 20.0 * std::log10(signal / noise);
}

double BlackPSNRObserver::calc_mean(const std::vector<double>& data) const {
    if (data.empty()) {
        return 0.0;
    }
    
    double sum = std::accumulate(data.begin(), data.end(), 0.0);
    return sum / static_cast<double>(data.size());
}

double BlackPSNRObserver::calc_std(const std::vector<double>& data) const {
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
