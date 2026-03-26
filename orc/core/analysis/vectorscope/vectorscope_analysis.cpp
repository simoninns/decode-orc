/*
 * File:        vectorscope_analysis.cpp
 * Module:      orc-core
 * Purpose:     Vectorscope analysis tool implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "vectorscope_analysis.h"
#include "../analysis_registry.h"
#include "../../include/video_field_representation.h"
#include "../../stages/chroma_sink/decoders/componentframe.h"
#include "logging.h"

#include <algorithm>
#include <cmath>

namespace {

double clamp_normalized(double value)
{
    if (value > 1.0) {
        return 1.0;
    }
    if (value < -1.0) {
        return -1.0;
    }
    return value;
}

} // namespace

namespace orc {

std::string VectorscopeAnalysisTool::id() const {
    return "vectorscope";
}

std::string VectorscopeAnalysisTool::name() const {
    return "Vectorscope";
}

std::string VectorscopeAnalysisTool::description() const {
    return "Display U/V color components on a vectorscope for decoded chroma output";
}

std::string VectorscopeAnalysisTool::category() const {
    return "Visualization";
}

std::vector<ParameterDescriptor> VectorscopeAnalysisTool::parameters() const {
    // No batch parameters - this is a live visualization tool
    return {};
}

bool VectorscopeAnalysisTool::canAnalyze(AnalysisSourceType source_type) const {
    // Works with any source that has been chroma decoded
    (void)source_type;
    return true;
}

bool VectorscopeAnalysisTool::isApplicableToStage(const std::string& stage_name) const {
    // Vectorscope is exposed via preview views, not Stage Tools.
    (void)stage_name;
    return false;
}

AnalysisResult VectorscopeAnalysisTool::analyze(const AnalysisContext& ctx,
                                                AnalysisProgress* progress) {
    (void)ctx;  // Currently unused
    AnalysisResult result;
    
    // This is a live visualization tool, not a batch analysis
    // The GUI will call extractFromRGB() directly for each field
    // This method exists to satisfy the AnalysisTool interface
    
    if (progress) {
        progress->setStatus("Vectorscope is a live visualization tool");
        progress->setProgress(100);
    }
    
    result.status = AnalysisResult::Success;
    result.summary = "Vectorscope visualization active";
    
    ORC_LOG_DEBUG("Vectorscope analysis called (live tool, no batch processing)");
    
    return result;
}

bool VectorscopeAnalysisTool::canApplyToGraph() const {
    // Live visualization, nothing to apply
    return false;
}

bool VectorscopeAnalysisTool::applyToGraph(AnalysisResult& result,
                                          const Project& project,
                                          NodeID node_id) {
    (void)result;
    (void)project;
    (void)node_id;
    
    // Live visualization, nothing to apply
    return false;
}

int VectorscopeAnalysisTool::estimateDurationSeconds(const AnalysisContext& ctx) const {
    (void)ctx;
    
    // Live tool, instantaneous
    return 0;
}

VectorscopeData VectorscopeAnalysisTool::extractFromRGB(
    const uint16_t* rgb_data,
    uint32_t width,
    uint32_t height,
    uint64_t field_number,
    uint32_t subsample,
    uint8_t field_id) {
    
    VectorscopeData data;
    data.width = width;
    data.height = height;
    data.field_number = field_number;
    
    if (!rgb_data || width == 0 || height == 0 || subsample == 0) {
        return data;
    }
    
    // Reserve space for samples (with subsampling)
    size_t estimated_samples = (width / subsample) * (height / subsample);
    data.samples.reserve(estimated_samples);
    
    // Extract U/V from RGB
    for (uint32_t y = 0; y < height; y += subsample) {
        for (uint32_t x = 0; x < width; x += subsample) {
            size_t pixel_index = (y * width + x) * 3;
            
            uint16_t r = rgb_data[pixel_index + 0];
            uint16_t g = rgb_data[pixel_index + 1];
            uint16_t b = rgb_data[pixel_index + 2];
            
            UVSample uv = rgb_to_uv(r, g, b);
            uv.field_id = field_id;  // Track which field this sample came from
            data.samples.push_back(uv);
        }
    }
    
    ORC_LOG_DEBUG("Extracted {} U/V samples from field {} ({}x{}, subsample={}, field_id={})",
                 data.samples.size(), field_number, width, height, subsample, field_id);
    
    return data;
}

VectorscopeData VectorscopeAnalysisTool::extractFromInterlacedRGB(
    const uint16_t* rgb_data,
    uint32_t width,
    uint32_t height,
    uint64_t field_number,
    uint32_t subsample) {
    
    VectorscopeData data;
    data.width = width;
    data.height = height;
    data.field_number = field_number;
    
    if (!rgb_data || width == 0 || height == 0 || subsample == 0) {
        return data;
    }
    
    // Reserve space for samples from both fields (with subsampling)
    size_t estimated_samples = (width / subsample) * (height / subsample);
    data.samples.reserve(estimated_samples);
    
    // Process both fields separately
    // Field 0 (first/odd field): even lines (0, 2, 4, ...)
    // Field 1 (second/even field): odd lines (1, 3, 5, ...)
    for (uint8_t field_id = 0; field_id < 2; field_id++) {
        // Process every (2 * subsample)th line starting from field_id
        for (uint32_t y = field_id; y < height; y += (2 * subsample)) {
            for (uint32_t x = 0; x < width; x += subsample) {
                size_t pixel_index = (y * width + x) * 3;
                
                uint16_t r = rgb_data[pixel_index + 0];
                uint16_t g = rgb_data[pixel_index + 1];
                uint16_t b = rgb_data[pixel_index + 2];
                
                UVSample uv = rgb_to_uv(r, g, b);
                uv.field_id = field_id;  // Tag which field this sample came from
                data.samples.push_back(uv);
            }
        }
    }
    
    ORC_LOG_DEBUG("Extracted {} U/V samples from interlaced frame {} ({}x{}, subsample={}, both fields)",
                 data.samples.size(), field_number, width, height, subsample);
    
    return data;
}

VectorscopeData VectorscopeAnalysisTool::extractFromComponentFrame(
    const ::ComponentFrame& frame,
    const ::orc::SourceParameters& video_parameters,
    uint64_t field_number,
    uint32_t subsample) {
    
    VectorscopeData data;
    const int32_t width = frame.getWidth();
    const int32_t height = frame.getHeight();
    data.field_number = field_number;
    
    if (width == 0 || height == 0 || subsample == 0) {
        return data;
    }

    int32_t x_start = 0;
    int32_t x_end = width;
    int32_t y_start = 0;
    int32_t y_end = height;

    if (video_parameters.active_video_start >= 0 &&
        video_parameters.active_video_end > video_parameters.active_video_start &&
        video_parameters.active_video_end <= width) {
        x_start = video_parameters.active_video_start;
        x_end = video_parameters.active_video_end;
    }

    if (video_parameters.first_active_frame_line >= 0 &&
        video_parameters.last_active_frame_line > video_parameters.first_active_frame_line &&
        video_parameters.last_active_frame_line <= height) {
        y_start = video_parameters.first_active_frame_line;
        y_end = video_parameters.last_active_frame_line;
    }

    data.width = static_cast<uint32_t>(x_end - x_start);
    data.height = static_cast<uint32_t>(y_end - y_start);
    
    // Reserve space for samples from the active picture area only.
    const size_t active_width = static_cast<size_t>(x_end - x_start);
    const size_t active_height = static_cast<size_t>(y_end - y_start);
    size_t estimated_samples = (active_width / subsample) * (active_height / subsample);
    data.samples.reserve(estimated_samples);
    
    // Process both fields separately
    // Field 0 (first/odd field): even lines (0, 2, 4, ...)
    // Field 1 (second/even field): odd lines (1, 3, 5, ...)
    for (uint8_t field_id = 0; field_id < 2; field_id++) {
        int32_t first_y = y_start;
        if ((first_y & 1) != field_id) {
            ++first_y;
        }

        // Process every (2 * subsample)th line starting from field_id
        for (int32_t y = first_y; y < y_end; y += (2 * static_cast<int32_t>(subsample))) {
            const double* uLine = frame.u(y);
            const double* vLine = frame.v(y);
            
            for (int32_t x = x_start; x < x_end; x += static_cast<int32_t>(subsample)) {
                // U and V are already in the native decoder format (doubles)
                // They represent the actual chroma signal levels
                UVSample uv;
                uv.u = uLine[x];
                uv.v = vLine[x];
                uv.field_id = field_id;  // Tag which field this sample came from
                data.samples.push_back(uv);
            }
        }
    }
    
    ORC_LOG_DEBUG("Extracted {} native U/V samples from ComponentFrame field {} (active {}x{} within {}x{}, subsample={}, both fields)",
                 data.samples.size(), field_number, data.width, data.height, width, height, subsample);
    
    return data;
}

VectorscopeData VectorscopeAnalysisTool::extractFromColourFrameCarrier(
    const ColourFrameCarrier& carrier,
    uint64_t field_number,
    uint32_t subsample,
    bool active_area_only) {

    VectorscopeData data;
    data.field_number = field_number;
    data.system = carrier.system;
    data.white_16b_ire = static_cast<int32_t>(carrier.white_16b_ire);
    data.black_16b_ire = static_cast<int32_t>(carrier.black_16b_ire);

    if (!carrier.is_valid() || subsample == 0) {
        return data;
    }

    uint32_t x_start = 0;
    uint32_t x_end = carrier.width;
    uint32_t y_start = 0;
    uint32_t y_end = carrier.height;

    if (active_area_only) {
        if (carrier.active_x_end > carrier.active_x_start && carrier.active_x_end <= carrier.width) {
            x_start = carrier.active_x_start;
            x_end = carrier.active_x_end;
        }

        if (carrier.active_y_end > carrier.active_y_start && carrier.active_y_end <= carrier.height) {
            y_start = carrier.active_y_start;
            y_end = carrier.active_y_end;
        }
    }

    data.width = x_end - x_start;
    data.height = y_end - y_start;

    const size_t sample_width = static_cast<size_t>(x_end - x_start);
    const size_t sample_height = static_cast<size_t>(y_end - y_start);
    const size_t estimated_samples = (sample_width / subsample) * (sample_height / subsample);
    data.samples.reserve(estimated_samples);

    for (uint8_t field_id = 0; field_id < 2; ++field_id) {
        uint32_t first_y = y_start;
        if ((first_y & 1U) != field_id) {
            ++first_y;
        }

        for (uint32_t y = first_y; y < y_end; y += (2 * subsample)) {
            const size_t line_offset = static_cast<size_t>(y) * static_cast<size_t>(carrier.width);

            for (uint32_t x = x_start; x < x_end; x += subsample) {
                const size_t sample_index = line_offset + static_cast<size_t>(x);
                UVSample uv;
                uv.u = carrier.u_plane[sample_index];
                uv.v = carrier.v_plane[sample_index];
                uv.field_id = field_id;
                data.samples.push_back(uv);
            }
        }
    }

    ORC_LOG_DEBUG(
        "Extracted {} U/V samples from colour preview carrier field {} ({} area {}x{} within {}x{}, subsample={}, both fields)",
        data.samples.size(),
        field_number,
        active_area_only ? "active" : "full",
        data.width,
        data.height,
        carrier.width,
        carrier.height,
        subsample);

    return data;
}

VectorscopeData VectorscopeAnalysisTool::extractFromCompositeRepresentation(
    const VideoFieldRepresentation& representation,
    const SourceParameters& video_parameters,
    uint64_t first_field_index,
    const std::optional<uint64_t>& second_field_index,
    uint64_t field_number,
    uint32_t subsample,
    bool active_area_only)
{
    VectorscopeData data;
    data.field_number = field_number;
    data.system = video_parameters.system;
    data.white_16b_ire = video_parameters.white_16b_ire;
    data.black_16b_ire = video_parameters.black_16b_ire;

    if (subsample == 0 || video_parameters.field_width <= 0 || video_parameters.field_height <= 0) {
        return data;
    }

    const auto process_field = [&](uint64_t field_index, uint8_t field_id) {
        const FieldID fid(field_index);
        auto descriptor_opt = representation.get_descriptor(fid);
        if (!descriptor_opt.has_value() || descriptor_opt->width == 0 || descriptor_opt->height == 0) {
            return;
        }

        const size_t width = descriptor_opt->width;
        const size_t height = descriptor_opt->height;

        size_t x_start = 0;
        size_t x_end = width;
        size_t y_start = 0;
        size_t y_end = height;

        if (active_area_only) {
            const int32_t active_x0 = std::max(0, video_parameters.active_video_start);
            const int32_t active_x1 = std::max(active_x0, video_parameters.active_video_end);
            x_start = static_cast<size_t>(std::min<int32_t>(active_x0, static_cast<int32_t>(width)));
            x_end = static_cast<size_t>(std::min<int32_t>(active_x1, static_cast<int32_t>(width)));

            const int32_t first_active_field_line = std::max(0, video_parameters.first_active_field_line);
            const int32_t last_active_field_line = std::max(first_active_field_line, video_parameters.last_active_field_line);
            if (video_parameters.first_active_field_line >= 0 && video_parameters.last_active_field_line > video_parameters.first_active_field_line) {
                y_start = static_cast<size_t>(std::min<int32_t>(first_active_field_line, static_cast<int32_t>(height)));
                y_end = static_cast<size_t>(std::min<int32_t>(last_active_field_line, static_cast<int32_t>(height)));
            }
        }

        if (x_start >= x_end || y_start >= y_end) {
            return;
        }

        const bool has_chroma_channel = representation.has_separate_channels();
        const double black = static_cast<double>(video_parameters.black_16b_ire);
        const double white = static_cast<double>(video_parameters.white_16b_ire);
        const double blank = static_cast<double>(video_parameters.blanking_16b_ire >= 0
            ? video_parameters.blanking_16b_ire
            : video_parameters.black_16b_ire);
        const double ire_range = std::max(1.0, white - black);

        const bool approx_4fsc = video_parameters.fsc > 0.0
            && video_parameters.sample_rate > 0.0
            && std::abs((video_parameters.sample_rate / video_parameters.fsc) - 4.0) < 1.0e-3;
        const double phase_step = (video_parameters.fsc > 0.0 && video_parameters.sample_rate > 0.0)
            ? (2.0 * M_PI * video_parameters.fsc / video_parameters.sample_rate)
            : 0.0;

        for (size_t y = y_start; y < y_end; y += static_cast<size_t>(subsample)) {
            const uint16_t* line = has_chroma_channel
                ? representation.get_line_chroma(fid, y)
                : representation.get_line(fid, y);
            if (!line) {
                continue;
            }

            for (size_t x = x_start; x < x_end; x += static_cast<size_t>(subsample)) {
                const double c = static_cast<double>(line[x]) - blank;

                double sin_ref = 0.0;
                double cos_ref = 0.0;
                if (approx_4fsc) {
                    switch (x & 3U) {
                        case 0U:
                            sin_ref = 1.0;
                            cos_ref = 0.0;
                            break;
                        case 1U:
                            sin_ref = 0.0;
                            cos_ref = -1.0;
                            break;
                        case 2U:
                            sin_ref = -1.0;
                            cos_ref = 0.0;
                            break;
                        default:
                            sin_ref = 0.0;
                            cos_ref = 1.0;
                            break;
                    }
                } else if (phase_step > 0.0) {
                    const double phase = phase_step * static_cast<double>(x);
                    sin_ref = std::sin(phase);
                    cos_ref = std::cos(phase);
                } else {
                    continue;
                }

                double u = (2.0 * c * sin_ref) / ire_range;
                double v = (2.0 * c * cos_ref) / ire_range;

                if (video_parameters.system == VideoSystem::PAL || video_parameters.system == VideoSystem::PAL_M) {
                    if (((y + (field_index & 1U)) & 1U) != 0U) {
                        v = -v;
                    }
                }

                UVSample sample;
                sample.u = clamp_normalized(u) * 32767.0;
                sample.v = clamp_normalized(v) * 32767.0;
                sample.field_id = field_id;
                data.samples.push_back(sample);
            }
        }
    };

    process_field(first_field_index, 0);
    if (second_field_index.has_value()) {
        process_field(*second_field_index, 1);
    }

    data.width = (video_parameters.active_video_end > video_parameters.active_video_start && active_area_only)
        ? static_cast<uint32_t>(video_parameters.active_video_end - video_parameters.active_video_start)
        : static_cast<uint32_t>(std::max(0, video_parameters.field_width));

    if (active_area_only && video_parameters.first_active_frame_line >= 0
        && video_parameters.last_active_frame_line > video_parameters.first_active_frame_line) {
        data.height = static_cast<uint32_t>(video_parameters.last_active_frame_line - video_parameters.first_active_frame_line);
    } else {
        const uint32_t first_h = static_cast<uint32_t>(std::max(0, video_parameters.field_height));
        data.height = second_field_index.has_value() ? (first_h * 2U) : first_h;
    }

    ORC_LOG_DEBUG(
        "Extracted {} composite vectorscope samples from field {}{} ({} area, subsample={})",
        data.samples.size(),
        first_field_index,
        second_field_index.has_value() ? std::string("+") + std::to_string(*second_field_index) : std::string(),
        active_area_only ? "active" : "full",
        subsample);

    return data;
}

// Register the tool
REGISTER_ANALYSIS_TOOL(VectorscopeAnalysisTool)

// Force linker to include this object file
void force_link_VectorscopeAnalysisTool() {}

} // namespace orc
