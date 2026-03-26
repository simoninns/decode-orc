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
#include "../../stages/chroma_sink/decoders/componentframe.h"
#include "logging.h"

namespace orc {

std::string VectorscopeAnalysisTool::id() const {
    return "vectorscope";
}

std::string VectorscopeAnalysisTool::name() const {
    return "Component Vectorscope";
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

// Register the tool
REGISTER_ANALYSIS_TOOL(VectorscopeAnalysisTool)

// Force linker to include this object file
void force_link_VectorscopeAnalysisTool() {}

} // namespace orc
