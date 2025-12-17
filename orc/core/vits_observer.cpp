/******************************************************************************
 * vits_observer.cpp
 *
 * VITS quality observer implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 ******************************************************************************/

#include "vits_observer.h"
#include <cmath>
#include <algorithm>
#include <numeric>

namespace orc {

VITSQualityObserver::VITSQualityObserver() {
    initialize_default_configs();
}

void VITSQualityObserver::initialize_default_configs() {
    // PAL configurations (from ld-process-vits)
    pal_white_configs_ = {
        {19, 12.0, 8.0}  // Line 19, start 12μs, length 8μs
    };
    
    pal_black_configs_ = {
        {22, 12.0, 50.0}  // Line 22, start 12μs, length 50μs
    };
    
    // NTSC configurations (from ld-process-vits)
    ntsc_white_configs_ = {
        {20, 14.0, 12.0},  // Line 20, start 14μs, length 12μs
        {20, 52.0, 8.0},   // Line 20, start 52μs, length 8μs
        {13, 13.0, 15.0}   // Line 13, start 13μs, length 15μs
    };
    
    ntsc_black_configs_ = {
        {1, 10.0, 20.0}  // Line 1, start 10μs, length 20μs
    };
}

void VITSQualityObserver::set_parameters(const std::map<std::string, std::string>& params) {
    Observer::set_parameters(params);
    
    // Allow parameter overrides
    auto it = params.find("white_ire_min");
    if (it != params.end()) {
        white_ire_min_ = std::stod(it->second);
    }
    
    it = params.find("white_ire_max");
    if (it != params.end()) {
        white_ire_max_ = std::stod(it->second);
    }
}

std::vector<std::shared_ptr<Observation>> VITSQualityObserver::process_field(
    const VideoFieldRepresentation& representation,
    FieldID field_id)
{
    auto obs = std::make_shared<VITSQualityObservation>();
    obs->field_id = field_id;
    obs->detection_basis = DetectionBasis::SAMPLE_DERIVED;
    obs->observer_version = observer_version();
    obs->observer_parameters = parameters_;
    
    // Get field descriptor to determine format
    auto descriptor_opt = representation.get_descriptor(field_id);
    if (!descriptor_opt.has_value()) {
        obs->confidence = ConfidenceLevel::NONE;
        return {obs};
    }
    
    const auto& descriptor = descriptor_opt.value();
    VideoFormat format = descriptor.format;
    
    // Select appropriate configurations based on format
    const auto& white_configs = (format == VideoFormat::PAL) ? pal_white_configs_ : ntsc_white_configs_;
    const auto& black_configs = (format == VideoFormat::PAL) ? pal_black_configs_ : ntsc_black_configs_;
    
    // Try white flag configurations until we find a valid one
    for (const auto& config : white_configs) {
        auto white_slice = get_field_line_slice(representation, field_id, 
                                               config.line_number, config.start_us, config.length_us);
        
        if (white_slice.empty()) {
            continue;
        }
        
        // Validate white level is in acceptable range
        double white_mean = calc_mean(white_slice);
        if (white_mean >= white_ire_min_ && white_mean <= white_ire_max_) {
            double wsnr = calculate_psnr(white_slice);
            obs->white_snr = wsnr;
            break;
        }
    }
    
    // Extract black level (always use first config)
    if (!black_configs.empty()) {
        const auto& config = black_configs[0];
        auto black_slice = get_field_line_slice(representation, field_id,
                                               config.line_number, config.start_us, config.length_us);
        
        if (!black_slice.empty()) {
            double bpsnr = calculate_psnr(black_slice);
            obs->black_psnr = bpsnr;
        }
    }
    
    // Set confidence based on what we found
    if (obs->white_snr.has_value() && obs->black_psnr.has_value()) {
        obs->confidence = ConfidenceLevel::HIGH;
    } else if (obs->white_snr.has_value() || obs->black_psnr.has_value()) {
        obs->confidence = ConfidenceLevel::MEDIUM;
    } else {
        obs->confidence = ConfidenceLevel::NONE;
    }
    
    return {obs};
}

std::vector<double> VITSQualityObserver::get_field_line_slice(
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
    
    // We need black and white IRE values for conversion
    // These are format-dependent calibration values
    // For now, use typical values - these should ideally come from VideoParameters
    double black_16b = 16384.0;  // Typical black level
    double white_16b = 53248.0;  // Typical white level
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

double VITSQualityObserver::calculate_psnr(const std::vector<double>& data) const {
    if (data.empty()) {
        return 0.0;
    }
    
    // PSNR uses 100 IRE as the reference signal
    double signal = 100.0;
    double noise = calc_std(data);
    
    if (noise <= 0.0) {
        return 0.0;
    }
    
    return 20.0 * std::log10(signal / noise);
}

double VITSQualityObserver::calc_mean(const std::vector<double>& data) const {
    if (data.empty()) {
        return 0.0;
    }
    
    double sum = std::accumulate(data.begin(), data.end(), 0.0);
    return sum / static_cast<double>(data.size());
}

double VITSQualityObserver::calc_std(const std::vector<double>& data) const {
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

double VITSQualityObserver::round_to_decimal_places(double value, int places) const {
    double multiplier = std::pow(10.0, places);
    return std::ceil(value * multiplier) / multiplier;
}

} // namespace orc
