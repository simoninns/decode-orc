/*
 * File:        vectorscope_observer.cpp
 * Module:      orc-core
 * Purpose:     Vectorscope data extraction observer implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "vectorscope_observer.h"
#include "logging.h"
#include "tbc_metadata.h"
#include "tbc_video_field_representation.h"
#include <cmath>
#include <random>

namespace orc {

std::vector<std::shared_ptr<Observation>> VectorscopeObserver::process_field(
    const VideoFieldRepresentation& representation,
    FieldID field_id,
    const ObservationHistory& history) {
    (void)history;  // Unused for now
    
    std::vector<std::shared_ptr<Observation>> observations;
    auto observation = std::make_shared<VectorscopeObservation>();
    observation->field_id = field_id;
    observation->detection_basis = DetectionBasis::SAMPLE_DERIVED;
    observation->observer_version = observer_version();
    observation->blend_color = blend_color_;
    observation->defocus = defocus_;
    observation->graticule_mode = graticule_mode_;
    observation->field_select = field_select_;
    
    // Get field descriptor
    auto descriptor = representation.get_descriptor(field_id);
    if (!descriptor.has_value()) {
        observation->confidence = ConfidenceLevel::NONE;
        observations.push_back(observation);
        return observations;
    }
    
    // Get video parameters
    auto video_params = representation.get_video_parameters();
    if (!video_params.has_value()) {
        observation->confidence = ConfidenceLevel::NONE;
        observations.push_back(observation);
        return observations;
    }
    
    // Extract U/V sample data
    try {
        VectorscopeData uv_data;
        extract_uv_samples(representation, field_id, video_params.value(), uv_data);
        observation->field_data.push_back(uv_data);
        observation->confidence = ConfidenceLevel::HIGH;
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("VectorscopeObserver::process_field - Error extracting data: {}", e.what());
        observation->confidence = ConfidenceLevel::NONE;
    }
    
    observations.push_back(observation);
    return observations;
}

void VectorscopeObserver::extract_uv_samples(
    const VideoFieldRepresentation& representation,
    FieldID field_id,
    const VideoParameters& video_params,
    VectorscopeData& out_data) {
    
    // Get colorburst end position (rough estimate - typically at ~1/10 of line width)
    size_t colorburst_end = video_params.field_width / 10;
    
    // Initialize random number generator for defocussing
    std::minstd_rand randomEngine(12345);
    std::normal_distribution<double> normalDist(0.0, 100.0);
    
    size_t samples_per_line = 0;
    
    // For each line in the active video area
    for (int32_t line_number = video_params.first_active_frame_line; 
         line_number < video_params.last_active_frame_line; 
         line_number++) {
        
        const uint16_t* line_data = representation.get_line(field_id, line_number);
        if (line_data == nullptr) {
            continue;
        }
        
        size_t line_sample_count = 0;
        
        // For each sample in the active area, extract U/V values
        for (int32_t xPosition = video_params.active_video_start; 
             xPosition < video_params.active_video_end; 
             xPosition++) {
            
            auto [u_val, v_val] = decode_uv_sample(
                line_data, xPosition, colorburst_end, video_params);
            
            // If defocussing, add random noise to U/V
            if (defocus_) {
                u_val += normalDist(randomEngine);
                v_val += normalDist(randomEngine);
            }
            
            out_data.samples.push_back({u_val, v_val});
            line_sample_count++;
        }
        
        if (samples_per_line == 0) {
            samples_per_line = line_sample_count;
        }
        out_data.line_count++;
    }
    
    out_data.samples_per_line = samples_per_line;
}

std::pair<double, double> VectorscopeObserver::decode_uv_sample(
    const uint16_t* line_data,
    size_t sample_index,
    size_t colorburst_end,
    const VideoParameters& video_params) const {
    
    // This is a simplified decoder
    // In composite video, U and V are modulated onto subcarriers
    // For a proper implementation, we would need to:
    // 1. Demodulate the chroma subcarrier
    // 2. Low-pass filter
    // 3. Extract U and V components
    
    // TEMPORARY: Generate test pattern to verify rendering works
    // Use sample value to create visible pattern
    double sample_val = static_cast<double>(line_data[sample_index]);
    double u = (sample_val - 32768.0) * 2.0;  // Scale to vectorscope range
    double v = (sample_index % 1000) * 65.0 - 32500.0;  // Vary by position
    
    // TODO: Implement proper PAL/NTSC chroma decoding
    
    (void)colorburst_end;
    (void)video_params;
    
    return {u, v};
}

} // namespace orc

