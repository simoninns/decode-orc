/*
 * File:        preview_helpers.cpp
 * Module:      orc-core
 * Purpose:     Helper functions for implementing PreviewableStage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "preview_helpers.h"
#include "logging.h"
#include <algorithm>

namespace orc {
namespace PreviewHelpers {

std::vector<PreviewOption> get_standard_preview_options(
    const std::shared_ptr<const VideoFieldRepresentation>& representation)
{
    std::vector<PreviewOption> options;
    
    if (!representation) {
        return options;
    }
    
    auto video_params = representation->get_video_parameters();
    if (!video_params) {
        return options;
    }
    
    uint64_t field_count = representation->field_count();
    if (field_count == 0) {
        return options;
    }
    
    uint32_t width = video_params->field_width;
    uint32_t height = video_params->field_height;
    
    // Calculate DAR correction based on active video region
    // For 4:3 DAR, we want the active area to display at 4:3 ratio
    double dar_correction = 0.7;  // Default fallback
    if (video_params->active_video_start >= 0 && video_params->active_video_end > video_params->active_video_start &&
        video_params->first_active_frame_line >= 0 && video_params->last_active_frame_line > video_params->first_active_frame_line) {
        uint32_t active_width = video_params->active_video_end - video_params->active_video_start;
        uint32_t active_height = video_params->last_active_frame_line - video_params->first_active_frame_line;
        // Calculate DAR correction: active area should display at 4:3
        // Example: PAL 702x576 → ratio 1.219, target 1.333, multiply width by 1.333/1.219 = 1.094
        double active_ratio = static_cast<double>(active_width) / static_cast<double>(active_height);
        double target_ratio = 4.0 / 3.0;
        dar_correction = target_ratio / active_ratio;
        ORC_LOG_DEBUG("PreviewHelpers: Calculated DAR correction = {:.3f} (active {}x{}, ratio {:.3f})",
                     dar_correction, active_width, active_height, active_ratio);
    } else {
        ORC_LOG_WARN("PreviewHelpers: Using fallback DAR 0.7 (active_video: {}-{}, active_frame_line: {}-{})",
                    video_params->active_video_start, video_params->active_video_end,
                    video_params->first_active_frame_line, video_params->last_active_frame_line);
    }
    
    // Field previews
    options.push_back(PreviewOption{
        "field", "Field (Y)", false, width, height, field_count, dar_correction
    });
    options.push_back(PreviewOption{
        "field_raw", "Field (Raw)", false, width, height, field_count, dar_correction
    });
    
    // Split and frame previews (require at least 2 fields)
    if (field_count >= 2) {
        uint64_t pair_count = field_count / 2;
        uint64_t frame_count = field_count / 2;
        
        options.push_back(PreviewOption{
            "split", "Split (Y)", false, width, height * 2, pair_count, dar_correction
        });
        options.push_back(PreviewOption{
            "split_raw", "Split (Raw)", false, width, height * 2, pair_count, dar_correction
        });
        options.push_back(PreviewOption{
            "frame", "Frame (Y)", false, width, height * 2, frame_count, dar_correction
        });
        options.push_back(PreviewOption{
            "frame_raw", "Frame (Raw)", false, width, height * 2, frame_count, dar_correction
        });
    }
    
    return options;
}

PreviewImage render_field_preview(
    const std::shared_ptr<const VideoFieldRepresentation>& representation,
    FieldID field_id,
    bool apply_ire_scaling)
{
    PreviewImage result;
    
    if (!representation || !representation->has_field(field_id)) {
        return result;
    }
    
    auto descriptor = representation->get_descriptor(field_id);
    if (!descriptor) {
        return result;
    }
    
    auto video_params = representation->get_video_parameters();
    if (!video_params) {
        return result;
    }
    
    result.width = descriptor->width;
    result.height = descriptor->height;
    result.rgb_data.resize(result.width * result.height * 3);
    
    // Calculate scaling parameters
    double blackIRE = video_params->black_16b_ire;
    double whiteIRE = video_params->white_16b_ire;
    double ireRange = whiteIRE - blackIRE;
    const double ire_scale = 255.0 / ireRange;
    const double raw_scale = 255.0 / 65535.0;
    
    // Render field as grayscale
    for (uint32_t y = 0; y < descriptor->height; ++y) {
        const uint16_t* line = representation->get_line(field_id, y);
        if (!line) continue;
        
        for (uint32_t x = 0; x < descriptor->width; ++x) {
            double sample = static_cast<double>(line[x]);
            uint8_t gray;
            
            if (apply_ire_scaling) {
                double scaled = std::max(0.0, std::min(255.0, (sample - blackIRE) * ire_scale));
                gray = static_cast<uint8_t>(scaled);
            } else {
                gray = static_cast<uint8_t>(sample * raw_scale);
            }
            
            size_t offset = (y * result.width + x) * 3;
            result.rgb_data[offset + 0] = gray;
            result.rgb_data[offset + 1] = gray;
            result.rgb_data[offset + 2] = gray;
        }
    }
    
    return result;
}

PreviewImage render_split_preview(
    const std::shared_ptr<const VideoFieldRepresentation>& representation,
    uint64_t pair_index,
    bool apply_ire_scaling)
{
    PreviewImage result;
    
    if (!representation) {
        return result;
    }
    
    FieldID first_field(pair_index * 2);
    FieldID second_field(pair_index * 2 + 1);
    
    if (!representation->has_field(first_field) || !representation->has_field(second_field)) {
        return result;
    }
    
    auto desc_first = representation->get_descriptor(first_field);
    auto desc_second = representation->get_descriptor(second_field);
    if (!desc_first || !desc_second) {
        return result;
    }
    
    auto video_params = representation->get_video_parameters();
    if (!video_params) {
        return result;
    }
    
    result.width = desc_first->width;
    result.height = desc_first->height + desc_second->height;
    result.rgb_data.resize(result.width * result.height * 3);
    
    // Calculate scaling parameters
    double blackIRE = video_params->black_16b_ire;
    double whiteIRE = video_params->white_16b_ire;
    double ireRange = whiteIRE - blackIRE;
    const double ire_scale = 255.0 / ireRange;
    const double raw_scale = 255.0 / 65535.0;
    
    auto render_field = [&](FieldID field_id, uint32_t y_offset) {
        auto descriptor = representation->get_descriptor(field_id);
        if (!descriptor) return;
        
        for (uint32_t y = 0; y < descriptor->height; ++y) {
            const uint16_t* line = representation->get_line(field_id, y);
            if (!line) continue;
            
            for (uint32_t x = 0; x < descriptor->width; ++x) {
                double sample = static_cast<double>(line[x]);
                uint8_t gray;
                
                if (apply_ire_scaling) {
                    double scaled = std::max(0.0, std::min(255.0, (sample - blackIRE) * ire_scale));
                    gray = static_cast<uint8_t>(scaled);
                } else {
                    gray = static_cast<uint8_t>(sample * raw_scale);
                }
                
                size_t offset = ((y + y_offset) * result.width + x) * 3;
                result.rgb_data[offset + 0] = gray;
                result.rgb_data[offset + 1] = gray;
                result.rgb_data[offset + 2] = gray;
            }
        }
    };
    
    // Render first field on top, second field on bottom
    render_field(first_field, 0);
    render_field(second_field, desc_first->height);
    
    return result;
}

PreviewImage render_frame_preview(
    const std::shared_ptr<const VideoFieldRepresentation>& representation,
    uint64_t frame_index,
    bool apply_ire_scaling)
{
    auto start_time = std::chrono::high_resolution_clock::now();
    PreviewImage result;
    
    if (!representation) {
        return result;
    }
    
    // Determine field indices based on parity
    FieldID first_field(frame_index * 2);
    FieldID second_field(frame_index * 2 + 1);
    
    // Check parity of field 0 to adjust frame start
    auto parity_hint = representation->get_field_parity_hint(FieldID(0));
    if (parity_hint.has_value() && !parity_hint->is_first_field) {
        // Field 0 is second field, adjust indices
        first_field = FieldID(frame_index * 2 + 1);
        second_field = FieldID(frame_index * 2 + 2);
    }
    
    if (!representation->has_field(first_field) || !representation->has_field(second_field)) {
        return result;
    }
    
    auto desc_first = representation->get_descriptor(first_field);
    auto desc_second = representation->get_descriptor(second_field);
    if (!desc_first || !desc_second) {
        return result;
    }
    
    auto video_params = representation->get_video_parameters();
    if (!video_params) {
        return result;
    }
    
    result.width = desc_first->width;
    result.height = desc_first->height + desc_second->height;
    result.rgb_data.resize(result.width * result.height * 3);
    
    // Calculate scaling parameters
    double blackIRE = video_params->black_16b_ire;
    double whiteIRE = video_params->white_16b_ire;
    double ireRange = whiteIRE - blackIRE;
    const double ire_scale = 255.0 / ireRange;
    const double raw_scale = 255.0 / 65535.0;
    
    // Determine field weaving order from parity hints
    auto first_parity = representation->get_field_parity_hint(first_field);
    auto second_parity = representation->get_field_parity_hint(second_field);
    
    bool first_field_on_even_lines = true;  // Default: first field on even lines
    if (first_parity.has_value() && second_parity.has_value()) {
        // Check for inversion: first has is_first_field=false AND second has is_first_field=true
        if (!first_parity->is_first_field && second_parity->is_first_field) {
            first_field_on_even_lines = false;  // Swap spatial order
        }
    }
    
    // Pre-compute scaling as integers for faster conversion (16-bit fixed-point)
    int32_t ire_black = static_cast<int32_t>(blackIRE);
    int32_t ire_mult = static_cast<int32_t>(ire_scale * 65536.0);  // Fixed-point 0.16
    int32_t raw_mult = static_cast<int32_t>(raw_scale * 65536.0);  // Fixed-point 0.16
    
    // Weave the two fields into a frame
    auto weave_start = std::chrono::high_resolution_clock::now();
    int get_line_calls = 0;
    for (uint32_t y = 0; y < result.height; ++y) {
        bool is_even_line = (y % 2 == 0);
        bool use_first_field = (is_even_line == first_field_on_even_lines);
        
        FieldID source_field = use_first_field ? first_field : second_field;
        uint32_t source_y = y / 2;
        
        const uint16_t* line = representation->get_line(source_field, source_y);
        get_line_calls++;
        if (!line) continue;
        
        uint8_t* rgb_line = &result.rgb_data[y * result.width * 3];
        
        if (apply_ire_scaling) {
            // Optimized IRE scaling using integer math
            for (uint32_t x = 0; x < result.width; ++x) {
                int32_t sample = static_cast<int32_t>(line[x]) - ire_black;
                int32_t scaled = (sample * ire_mult) >> 16;  // Fixed-point multiply with 16-bit fraction
                uint8_t gray = static_cast<uint8_t>(std::max(0, std::min(255, scaled)));
                rgb_line[x * 3 + 0] = gray;
                rgb_line[x * 3 + 1] = gray;
                rgb_line[x * 3 + 2] = gray;
            }
        } else {
            // Optimized raw scaling using integer math
            for (uint32_t x = 0; x < result.width; ++x) {
                int32_t scaled = (static_cast<int32_t>(line[x]) * raw_mult) >> 16;  // Fixed-point multiply with 16-bit fraction
                uint8_t gray = static_cast<uint8_t>(scaled);
                rgb_line[x * 3 + 0] = gray;
                rgb_line[x * 3 + 1] = gray;
                rgb_line[x * 3 + 2] = gray;
            }
        }
    }
    auto weave_end = std::chrono::high_resolution_clock::now();
    auto weave_ms = std::chrono::duration_cast<std::chrono::milliseconds>(weave_end - weave_start).count();
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    ORC_LOG_DEBUG("PreviewHelpers::render_frame_preview: frame {} rendered in {} ms ({}x{} px) - weave: {}ms ({} get_line calls)",
                 frame_index, duration_ms, result.width, result.height, weave_ms, get_line_calls);
    
    return result;
}

PreviewImage render_standard_preview(
    const std::shared_ptr<const VideoFieldRepresentation>& representation,
    const std::string& option_id,
    uint64_t index,
    PreviewNavigationHint hint)
{
    if (!representation) {
        PreviewImage result;
        return result;
    }
    
    bool apply_ire_scaling = (option_id.find("_raw") == std::string::npos);
    
    if (option_id == "field" || option_id == "field_raw") {
        return render_field_preview(representation, FieldID(index), apply_ire_scaling);
    }
    
    if (option_id == "split" || option_id == "split_raw") {
        return render_split_preview(representation, index, apply_ire_scaling);
    }
    
    if (option_id == "frame" || option_id == "frame_raw") {
        return render_frame_preview(representation, index, apply_ire_scaling);
    }
    
    ORC_LOG_WARN("PreviewHelpers: Unknown preview option '{}'", option_id);
    PreviewImage result;
    return result;
}

} // namespace PreviewHelpers
} // namespace orc
