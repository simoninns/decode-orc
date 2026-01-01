/*
 * File:        vectorscope_analysis.cpp
 * Module:      orc-core
 * Purpose:     Vectorscope analysis tool implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "vectorscope_analysis.h"
#include "../analysis_registry.h"
#include "logging.h"

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
    // Only applicable to chroma decoder sink; match by registered stage name
    return stage_name == "chroma_sink";
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

bool VectorscopeAnalysisTool::applyToGraph(const AnalysisResult& result,
                                          Project& project,
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

// Register the tool
REGISTER_ANALYSIS_TOOL(VectorscopeAnalysisTool)

} // namespace orc
