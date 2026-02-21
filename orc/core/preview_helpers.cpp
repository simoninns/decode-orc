/*
 * File:        preview_helpers.cpp
 * Module:      orc-core
 * Purpose:     Helper functions for implementing PreviewableStage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "preview_helpers.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "logging.h"

namespace orc {
namespace PreviewHelpers {

// Helper function for consistent 16-bit to 8-bit grayscale conversion
// Uses fixed-point integer math for speed and consistency across all preview modes
inline uint8_t scale_16bit_to_8bit(
    uint16_t sample,
    bool apply_ire_scaling,
    int32_t ire_black,
    int32_t ire_mult,
    int32_t raw_mult)
{
    if (apply_ire_scaling) {
        // IRE scaling: subtract black level, multiply by IRE scaling factor
        int32_t adjusted = static_cast<int32_t>(sample) - ire_black;
        int32_t scaled = (adjusted * ire_mult) >> 16;  // Fixed-point 0.16
        return static_cast<uint8_t>(std::max(0, std::min(255, scaled)));
    } else {
        // Raw scaling: simple linear mapping from 16-bit to 8-bit
        int32_t scaled = (static_cast<int32_t>(sample) * raw_mult) >> 16;  // Fixed-point 0.16
        return static_cast<uint8_t>(std::max(0, std::min(255, scaled)));
    }
}

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
        "field", "Field (Clamped)", false, width, height, field_count, dar_correction
    });
    options.push_back(PreviewOption{
        "field_raw", "Field (Raw)", false, width, height, field_count, dar_correction
    });
    
    // Split and frame previews (require at least 2 fields)
    if (field_count >= 2) {
        uint64_t pair_count = field_count / 2;
        uint64_t frame_count = field_count / 2;
        
        options.push_back(PreviewOption{
            "split", "Split (Clamped)", false, width, height * 2, pair_count, dar_correction
        });
        options.push_back(PreviewOption{
            "split_raw", "Split (Raw)", false, width, height * 2, pair_count, dar_correction
        });
        options.push_back(PreviewOption{
            "frame", "Frame (Clamped)", false, width, height * 2, frame_count, dar_correction
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
    PreviewImage result{};
    
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
    
    result.width = static_cast<uint32_t>(descriptor->width);
    result.height = static_cast<uint32_t>(descriptor->height);
    result.rgb_data.resize(result.width * result.height * 3);
    
    // Calculate scaling parameters (fixed-point integer math)
    double blackIRE = video_params->black_16b_ire;
    double whiteIRE = video_params->white_16b_ire;
    double ireRange = whiteIRE - blackIRE;
    int32_t ire_black = static_cast<int32_t>(blackIRE);
    int32_t ire_mult = static_cast<int32_t>((255.0 / ireRange) * 65536.0);  // Fixed-point 0.16
    int32_t raw_mult = static_cast<int32_t>((255.0 / 65535.0) * 65536.0);  // Fixed-point 0.16
    
    // Render field as grayscale
    for (uint32_t y = 0; y < descriptor->height; ++y) {
        const uint16_t* line = representation->get_line(field_id, y);
        if (!line) continue;
        
        for (uint32_t x = 0; x < descriptor->width; ++x) {
            uint8_t gray = scale_16bit_to_8bit(line[x], apply_ire_scaling, ire_black, ire_mult, raw_mult);
            
            size_t offset = (y * result.width + x) * 3;
            result.rgb_data[offset + 0] = gray;
            result.rgb_data[offset + 1] = gray;
            result.rgb_data[offset + 2] = gray;
        }
    }
    
    // Extract dropout regions for this field
    result.dropout_regions = representation->get_dropout_hints(field_id);
    ORC_LOG_DEBUG("PreviewHelpers::render_field_preview: Field {} has {} dropout regions",
                  field_id.value(), result.dropout_regions.size());
    
    return result;
}

PreviewImage render_split_preview(
    const std::shared_ptr<const VideoFieldRepresentation>& representation,
    uint64_t pair_index,
    bool apply_ire_scaling)
{
    PreviewImage result{};
    
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
    
    result.width = static_cast<uint32_t>(desc_first->width);
    result.height = static_cast<uint32_t>(desc_first->height + desc_second->height);
    result.rgb_data.resize(result.width * result.height * 3);
    
    // Calculate scaling parameters (fixed-point integer math)
    double blackIRE = video_params->black_16b_ire;
    double whiteIRE = video_params->white_16b_ire;
    double ireRange = whiteIRE - blackIRE;
    int32_t ire_black = static_cast<int32_t>(blackIRE);
    int32_t ire_mult = static_cast<int32_t>((255.0 / ireRange) * 65536.0);  // Fixed-point 0.16
    int32_t raw_mult = static_cast<int32_t>((255.0 / 65535.0) * 65536.0);  // Fixed-point 0.16
    
    auto render_field = [&](FieldID field_id, uint32_t y_offset) {
        auto descriptor = representation->get_descriptor(field_id);
        if (!descriptor) return;
        
        for (uint32_t y = 0; y < descriptor->height; ++y) {
            const uint16_t* line = representation->get_line(field_id, y);
            if (!line) continue;
            
            for (uint32_t x = 0; x < descriptor->width; ++x) {
                uint8_t gray = scale_16bit_to_8bit(line[x], apply_ire_scaling, ire_black, ire_mult, raw_mult);
                
                size_t offset = ((y + y_offset) * result.width + x) * 3;
                result.rgb_data[offset + 0] = gray;
                result.rgb_data[offset + 1] = gray;
                result.rgb_data[offset + 2] = gray;
            }
        }
    };
    
    // Render first field on top, second field on bottom
    render_field(first_field, 0);
    render_field(second_field, static_cast<uint32_t>(desc_first->height));
    
    // Extract dropout regions from both fields for split view
    auto dropouts_first = representation->get_dropout_hints(first_field);
    auto dropouts_second = representation->get_dropout_hints(second_field);
    
    ORC_LOG_DEBUG("PreviewHelpers::render_split_preview: Field {} has {} dropouts, Field {} has {} dropouts",
                  first_field.value(), dropouts_first.size(), second_field.value(), dropouts_second.size());
    
    // First field dropouts go in top half (no adjustment needed)
    result.dropout_regions = dropouts_first;
    
    // Second field dropouts go in bottom half (offset line numbers by first field height)
    for (auto& region : dropouts_second) {
        region.line += static_cast<uint32_t>(desc_first->height);
        result.dropout_regions.push_back(region);
    }
    
    return result;
}

PreviewImage render_frame_preview(
    const std::shared_ptr<const VideoFieldRepresentation>& representation,
    uint64_t frame_index,
    bool apply_ire_scaling)
{
    auto start_time = std::chrono::high_resolution_clock::now();
    PreviewImage result{};
    
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
    
    result.width = static_cast<uint32_t>(desc_first->width);
    result.height = static_cast<uint32_t>(desc_first->height + desc_second->height);
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
        
        for (uint32_t x = 0; x < result.width; ++x) {
            uint8_t gray = scale_16bit_to_8bit(line[x], apply_ire_scaling, ire_black, ire_mult, raw_mult);
            rgb_line[x * 3 + 0] = gray;
            rgb_line[x * 3 + 1] = gray;
            rgb_line[x * 3 + 2] = gray;
        }
    }
    auto weave_end = std::chrono::high_resolution_clock::now();
    auto weave_ms = std::chrono::duration_cast<std::chrono::milliseconds>(weave_end - weave_start).count();
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    ORC_LOG_DEBUG("PreviewHelpers::render_frame_preview: frame {} rendered in {} ms ({}x{} px) - weave: {}ms ({} get_line calls)",
                 frame_index, duration_ms, result.width, result.height, weave_ms, get_line_calls);
    
    // Extract dropout regions from both fields
    auto dropouts_first = representation->get_dropout_hints(first_field);
    auto dropouts_second = representation->get_dropout_hints(second_field);
    
    ORC_LOG_DEBUG("PreviewHelpers::render_frame_preview: Field {} has {} dropouts, Field {} has {} dropouts",
                  first_field.value(), dropouts_first.size(), second_field.value(), dropouts_second.size());
    
    // Adjust line numbers for interlaced frame display
    for (auto& region : dropouts_first) {
        // First field lines map to even/odd frame lines depending on first_field_on_even_lines
        region.line = first_field_on_even_lines ? (region.line * 2) : (region.line * 2 + 1);
        result.dropout_regions.push_back(region);
    }
    
    for (auto& region : dropouts_second) {
        // Second field lines map to odd/even frame lines depending on first_field_on_even_lines
        region.line = first_field_on_even_lines ? (region.line * 2 + 1) : (region.line * 2);
        result.dropout_regions.push_back(region);
    }
    
    return result;
}

PreviewImage render_standard_preview(
    const std::shared_ptr<const VideoFieldRepresentation>& representation,
    const std::string& option_id,
    uint64_t index,
    PreviewNavigationHint hint)
{
    (void)hint;  // Unused for now
    if (!representation) {
        PreviewImage result{};
        return result;
    }
    
    // Check if option_id has a channel suffix (_y, _c, _yc)
    // If so, delegate to the channel-aware renderer
    if (option_id.find("_yc") != std::string::npos ||
        (option_id.find("_y") != std::string::npos && option_id.find("_yc") == std::string::npos) ||
        option_id.find("_c") != std::string::npos) {
        
        // Parse channel from suffix
        RenderChannel channel = RenderChannel::COMPOSITE;
        std::string base_option = option_id;
        
        // Check _yc first (before _y or _c)
        if (option_id.find("_yc") != std::string::npos) {
            channel = RenderChannel::COMPOSITE_YC;
            size_t pos = option_id.find("_yc");
            base_option = option_id.substr(0, pos);
            if (pos + 3 < option_id.size() && option_id.substr(pos + 3) == "_raw") {
                base_option += "_raw";
            }
        } else if (option_id.find("_y") != std::string::npos) {
            channel = RenderChannel::LUMA_ONLY;
            size_t pos = option_id.find("_y");
            base_option = option_id.substr(0, pos);
            if (pos + 2 < option_id.size() && option_id.substr(pos + 2) == "_raw") {
                base_option += "_raw";
            }
        } else if (option_id.find("_c") != std::string::npos) {
            channel = RenderChannel::CHROMA_ONLY;
            size_t pos = option_id.find("_c");
            base_option = option_id.substr(0, pos);
            if (pos + 2 < option_id.size() && option_id.substr(pos + 2) == "_raw") {
                base_option += "_raw";
            }
        }
        
        return render_standard_preview_with_channel(representation, base_option, index, channel, hint);
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
    PreviewImage result{};
    return result;
}

// Helper to get field data for a specific channel
static std::vector<uint16_t> get_field_for_channel(
    const VideoFieldRepresentation* representation,
    FieldID field_id,
    RenderChannel channel)
{
    if (!representation || !representation->has_field(field_id)) {
        return {};
    }
    
    if (channel == RenderChannel::LUMA_ONLY && representation->has_separate_channels()) {
        return representation->get_field_luma(field_id);
    } else if (channel == RenderChannel::CHROMA_ONLY && representation->has_separate_channels()) {
        return representation->get_field_chroma(field_id);
    } else if (channel == RenderChannel::COMPOSITE_YC && representation->has_separate_channels()) {
        // Combine Y+C
        auto y_data = representation->get_field_luma(field_id);
        auto c_data = representation->get_field_chroma(field_id);
        if (y_data.size() != c_data.size()) {
            return {};
        }
        std::vector<uint16_t> combined(y_data.size());
        for (size_t i = 0; i < y_data.size(); ++i) {
            uint32_t sum = static_cast<uint32_t>(y_data[i]) + static_cast<uint32_t>(c_data[i]);
            combined[i] = static_cast<uint16_t>(std::min(sum, 65535u));
        }
        return combined;
    } else {
        // Composite or unknown - use standard get_field
        return representation->get_field(field_id);
    }
}

// Split preview with channel selection
static PreviewImage render_split_preview_with_channel(
    const VideoFieldRepresentation* representation,
    uint64_t pair_index,
    bool apply_ire_scaling,
    RenderChannel channel)
{
    PreviewImage result{};
    
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
    
    result.width = static_cast<uint32_t>(desc_first->width);
    result.height = static_cast<uint32_t>(desc_first->height + desc_second->height);
    result.rgb_data.resize(result.width * result.height * 3);
    
    // Get field data for the selected channel
    auto first_data = get_field_for_channel(representation, first_field, channel);
    auto second_data = get_field_for_channel(representation, second_field, channel);
    
    if (first_data.empty() || second_data.empty()) {
        return result;
    }
    
    // Calculate scaling parameters
    double blackIRE = video_params->black_16b_ire;
    double whiteIRE = video_params->white_16b_ire;
    double ireRange = whiteIRE - blackIRE;
    const double ire_scale = 255.0 / ireRange;
    const double raw_scale = 255.0 / 65535.0;
    
    // Render first field
    for (uint32_t y = 0; y < desc_first->height; ++y) {
        for (uint32_t x = 0; x < desc_first->width; ++x) {
            size_t sample_idx = y * desc_first->width + x;
            if (sample_idx >= first_data.size()) continue;
            
            double sample = static_cast<double>(first_data[sample_idx]);
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
    
    // Render second field
    for (uint32_t y = 0; y < desc_second->height; ++y) {
        for (uint32_t x = 0; x < desc_second->width; ++x) {
            size_t sample_idx = y * desc_second->width + x;
            if (sample_idx >= second_data.size()) continue;
            
            double sample = static_cast<double>(second_data[sample_idx]);
            uint8_t gray;
            
            if (apply_ire_scaling) {
                double scaled = std::max(0.0, std::min(255.0, (sample - blackIRE) * ire_scale));
                gray = static_cast<uint8_t>(scaled);
            } else {
                gray = static_cast<uint8_t>(sample * raw_scale);
            }
            
            size_t offset = ((y + static_cast<uint32_t>(desc_first->height)) * result.width + x) * 3;
            result.rgb_data[offset + 0] = gray;
            result.rgb_data[offset + 1] = gray;
            result.rgb_data[offset + 2] = gray;
        }
    }
    
    // Extract dropout regions from both fields
    auto dropouts_first = representation->get_dropout_hints(first_field);
    auto dropouts_second = representation->get_dropout_hints(second_field);
    
    result.dropout_regions = dropouts_first;
    for (auto& region : dropouts_second) {
        region.line += static_cast<uint32_t>(desc_first->height);
        result.dropout_regions.push_back(region);
    }
    
    return result;
}

// Frame preview with channel selection
static PreviewImage render_frame_preview_with_channel(
    const VideoFieldRepresentation* representation,
    uint64_t frame_index,
    bool apply_ire_scaling,
    RenderChannel channel)
{
    PreviewImage result{};
    
    if (!representation) {
        return result;
    }
    
    // Determine field indices based on parity
    FieldID first_field(frame_index * 2);
    FieldID second_field(frame_index * 2 + 1);
    
    auto parity_hint = representation->get_field_parity_hint(FieldID(0));
    if (parity_hint.has_value() && !parity_hint->is_first_field) {
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
    
    result.width = static_cast<uint32_t>(desc_first->width);
    result.height = static_cast<uint32_t>(desc_first->height + desc_second->height);
    result.rgb_data.resize(result.width * result.height * 3);
    
    // Get field data for the selected channel
    auto first_data = get_field_for_channel(representation, first_field, channel);
    auto second_data = get_field_for_channel(representation, second_field, channel);
    
    if (first_data.empty() || second_data.empty()) {
        return result;
    }
    
    // Calculate scaling parameters
    double blackIRE = video_params->black_16b_ire;
    double whiteIRE = video_params->white_16b_ire;
    double ireRange = whiteIRE - blackIRE;
    const double ire_scale = 255.0 / ireRange;
    const double raw_scale = 255.0 / 65535.0;
    
    // Determine field weaving order
    auto first_parity = representation->get_field_parity_hint(first_field);
    auto second_parity = representation->get_field_parity_hint(second_field);
    
    bool first_is_top = true;
    if (first_parity.has_value() && second_parity.has_value()) {
        first_is_top = first_parity->is_first_field;
    }
    
    // Weave fields into frame
    for (uint32_t y = 0; y < result.height; ++y) {
        bool use_first_field = (y % 2 == 0) ? first_is_top : !first_is_top;
        
        const auto& field_data = use_first_field ? first_data : second_data;
        uint32_t field_line = y / 2;
        size_t field_width = use_first_field ? desc_first->width : desc_second->width;
        
        for (uint32_t x = 0; x < result.width; ++x) {
            size_t sample_idx = field_line * field_width + x;
            if (sample_idx >= field_data.size()) continue;
            
            double sample = static_cast<double>(field_data[sample_idx]);
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
    
    // For frames, dropout regions need to be adjusted for interlaced layout
    auto dropouts_first = representation->get_dropout_hints(first_field);
    auto dropouts_second = representation->get_dropout_hints(second_field);
    
    for (auto& region : dropouts_first) {
        region.line = region.line * 2 + (first_is_top ? 0 : 1);
        result.dropout_regions.push_back(region);
    }
    for (auto& region : dropouts_second) {
        region.line = region.line * 2 + (first_is_top ? 1 : 0);
        result.dropout_regions.push_back(region);
    }
    
    return result;
}

PreviewImage render_standard_preview_with_channel(
    const std::shared_ptr<const VideoFieldRepresentation>& representation,
    const std::string& option_id,
    uint64_t index,
    RenderChannel channel,
    PreviewNavigationHint hint)
{
    (void)hint;  // Unused for now
    
    if (!representation) {
        return PreviewImage{};
    }
    
    bool apply_ire_scaling = (option_id.find("_raw") == std::string::npos);
    
    // For field previews, use channel-aware rendering
    if (option_id == "field" || option_id == "field_raw") {
        return render_field_grayscale(representation.get(), FieldID(index), channel, apply_ire_scaling);
    }
    
    // For split and frame, use channel-aware rendering
    if (option_id == "split" || option_id == "split_raw") {
        return render_split_preview_with_channel(representation.get(), index, apply_ire_scaling, channel);
    }
    
    if (option_id == "frame" || option_id == "frame_raw") {
        return render_frame_preview_with_channel(representation.get(), index, apply_ire_scaling, channel);
    }
    
    ORC_LOG_WARN("PreviewHelpers: Unknown preview option '{}'", option_id);
    return PreviewImage{};
}

PreviewImage render_field_grayscale(
    const VideoFieldRepresentation* representation,
    FieldID field_id,
    RenderChannel channel,
    bool apply_ire_scaling)
{
    if (!representation || !representation->has_field(field_id)) {
        return PreviewImage{};
    }
    
    auto video_params = representation->get_video_parameters();
    if (!video_params) {
        return PreviewImage{};
    }
    
    uint32_t width = video_params->field_width;
    uint32_t height = video_params->field_height;
    
    // Get field data based on channel selection
    std::vector<uint16_t> field_data;
    
    if (channel == RenderChannel::LUMA_ONLY && representation->has_separate_channels()) {
        // YC source - render Y channel
        field_data = representation->get_field_luma(field_id);
    } else if (channel == RenderChannel::CHROMA_ONLY && representation->has_separate_channels()) {
        // YC source - render C channel
        field_data = representation->get_field_chroma(field_id);
    } else if (channel == RenderChannel::COMPOSITE_YC && representation->has_separate_channels()) {
        // YC source - combine Y+C for visualization
        auto y_data = representation->get_field_luma(field_id);
        auto c_data = representation->get_field_chroma(field_id);
        
        if (y_data.size() != c_data.size()) {
            return PreviewImage{};
        }
        
        field_data.resize(y_data.size());
        for (size_t i = 0; i < y_data.size(); ++i) {
            // Simple addition for visualization (clamp to uint16_t range)
            uint32_t sum = static_cast<uint32_t>(y_data[i]) + static_cast<uint32_t>(c_data[i]);
            field_data[i] = static_cast<uint16_t>(std::min(sum, 65535u));
        }
    } else {
        // Composite source or default - render normal field data
        field_data = representation->get_field(field_id);
    }
    
    if (field_data.empty()) {
        return PreviewImage{};
    }
    
    // Calculate scaling parameters (fixed-point integer math)
    double blackIRE = video_params->black_16b_ire;
    double whiteIRE = video_params->white_16b_ire;
    double ireRange = whiteIRE - blackIRE;
    int32_t ire_black = static_cast<int32_t>(blackIRE);
    int32_t ire_mult = static_cast<int32_t>((255.0 / ireRange) * 65536.0);  // Fixed-point 0.16
    int32_t raw_mult = static_cast<int32_t>((255.0 / 65535.0) * 65536.0);  // Fixed-point 0.16
    
    // Convert to 8-bit grayscale with proper scaling, then to RGB888
    std::vector<uint8_t> rgb_data(width * height * 3);
    for (size_t i = 0; i < field_data.size(); ++i) {
        uint8_t gray = scale_16bit_to_8bit(field_data[i], apply_ire_scaling, ire_black, ire_mult, raw_mult);
        
        // Replicate grayscale to RGB
        rgb_data[i * 3 + 0] = gray;  // R
        rgb_data[i * 3 + 1] = gray;  // G
        rgb_data[i * 3 + 2] = gray;  // B
    }
    
    // Create preview image
    PreviewImage result;
    result.width = width;
    result.height = height;
    result.rgb_data = std::move(rgb_data);
    
    return result;
}

} // namespace PreviewHelpers
} // namespace orc
