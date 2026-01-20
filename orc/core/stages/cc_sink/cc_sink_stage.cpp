/*
 * File:        cc_sink_stage.cpp
 * Module:      orc-core
 * Purpose:     Closed Caption Sink Stage - exports CC data to SCC or plain text
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "cc_sink_stage.h"
#include "logging.h"
#include "closed_caption_observer.h"
#include "observation_history.h"
#include "eia608_decoder.h"
#include <stage_registry.h>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace orc {

// Register this stage with the registry
ORC_REGISTER_STAGE(CCSinkStage)

// Force linker to include this object file
void force_link_CCSinkStage() {}

CCSinkStage::CCSinkStage() = default;

NodeTypeInfo CCSinkStage::get_node_type_info() const {
    return NodeTypeInfo{
        NodeType::SINK,
        "CCSink",
        "Closed Caption Sink",
        "Exports closed caption data to SCC or plain text format",
        1, 1,  // One input
        0, 0,  // No outputs (sink)
        VideoFormatCompatibility::ALL
    };
}

std::vector<ArtifactPtr> CCSinkStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    ObservationContext& observation_context
) {
    // Sink stages don't produce outputs in execute()
    // Actual work happens in trigger()
    (void)inputs;
    (void)parameters;
    (void)observation_context;
    return {};
}

std::vector<ParameterDescriptor> CCSinkStage::get_parameter_descriptors(VideoSystem project_format, SourceType source_type) const {
    (void)project_format;
    (void)source_type;
    std::vector<ParameterDescriptor> descriptors;
    
    // output_path parameter
    {
        ParameterDescriptor desc;
        desc.name = "output_path";
        desc.display_name = "Output File";
        desc.description = "Path to output closed caption file";
        desc.type = ParameterType::FILE_PATH;
        desc.constraints.required = true;
        desc.constraints.default_value = std::string("");
        descriptors.push_back(desc);
    }
    
    // format parameter
    {
        ParameterDescriptor desc;
        desc.name = "format";
        desc.display_name = "Export Format";
        desc.description = "Output format: Scenarist SCC V1.0 or plain text";
        desc.type = ParameterType::STRING;
        desc.constraints.required = true;
        desc.constraints.allowed_strings = {"Scenarist SCC", "Plain Text"};
        desc.constraints.default_value = std::string("Scenarist SCC");
        descriptors.push_back(desc);
    }
    
    return descriptors;
}

std::map<std::string, ParameterValue> CCSinkStage::get_parameters() const {
    return parameters_;
}

bool CCSinkStage::set_parameters(const std::map<std::string, ParameterValue>& params) {
    parameters_ = params;
    return true;
}

bool CCSinkStage::trigger(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    ObservationContext& observation_context) {
    (void)observation_context; // Observations not yet used in trigger
    is_processing_.store(true);
    cancel_requested_.store(false);
    
    try {
        // Validate inputs
        if (inputs.empty()) {
            throw std::runtime_error("CC sink requires one input (VideoFieldRepresentation)");
        }
        
        auto vfr = std::dynamic_pointer_cast<VideoFieldRepresentation>(inputs[0]);
        if (!vfr) {
            throw std::runtime_error("Input must be a VideoFieldRepresentation");
        }
        
        // Get parameters
        auto output_path_it = parameters.find("output_path");
        if (output_path_it == parameters.end()) {
            throw std::runtime_error("output_path parameter is required");
        }
        std::string output_path = std::get<std::string>(output_path_it->second);
        
        auto format_it = parameters.find("format");
        CCExportFormat format = CCExportFormat::SCC;
        if (format_it != parameters.end()) {
            std::string format_str = std::get<std::string>(format_it->second);
            if (format_str == "Plain Text") {
                format = CCExportFormat::PLAIN_TEXT;
            }
        }
        
        // Get video format
        auto descriptor = vfr->get_descriptor(FieldID(1));
        if (!descriptor.has_value()) {
            throw std::runtime_error("Cannot determine video format");
        }
        VideoFormat video_format = descriptor->format;
        
        // Export based on format
        bool success = false;
        if (format == CCExportFormat::SCC) {
            ORC_LOG_INFO("Exporting closed captions to SCC format: {}", output_path);
            success = export_scc(vfr.get(), output_path, video_format);
        } else {
            ORC_LOG_INFO("Exporting closed captions to plain text format: {}", output_path);
            success = export_plain_text(vfr.get(), output_path, video_format);
        }
        
        is_processing_.store(false);
        
        if (!success) {
            throw std::runtime_error("Failed to export closed captions");
        }
        
        ORC_LOG_INFO("Closed caption export completed successfully");
        return true;
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("CC sink error: {}", e.what());
        is_processing_.store(false);
        return false;
    }
}

std::string CCSinkStage::get_trigger_status() const {
    if (is_processing_.load()) {
        return "Exporting closed captions...";
    }
    return "Idle";
}

// Helper: Generate SCC format timestamp (HH:MM:SS:FF)
std::string CCSinkStage::generate_timestamp(int32_t field_index, VideoFormat format) const {
    // Convert to 0-based count of frames
    double frame_index = static_cast<double>((field_index - 1) / 2);
    
    // Set constants for timecode calculations
    // We generate non-drop timecode (:ff not ;ff), so clock counts at 29.97 FPS for NTSC
    const double frames_per_second = (format == VideoFormat::PAL) ? 25.0 : 29.97;
    const double frames_per_minute = frames_per_second * 60.0;
    const double frames_per_hour = frames_per_minute * 60.0;
    
    // Calculate timecode components
    const int32_t hh = static_cast<int32_t>(frame_index / frames_per_hour);
    frame_index -= static_cast<double>(hh) * frames_per_hour;
    const int32_t mm = static_cast<int32_t>(frame_index / frames_per_minute);
    frame_index -= static_cast<double>(mm) * frames_per_minute;
    const int32_t ss = static_cast<int32_t>(frame_index / frames_per_second);
    frame_index -= static_cast<double>(ss) * frames_per_second;
    const int32_t ff = static_cast<int32_t>(frame_index);
    
    // Format as HH:MM:SS:FF
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << hh << ":"
        << std::setfill('0') << std::setw(2) << mm << ":"
        << std::setfill('0') << std::setw(2) << ss << ":"
        << std::setfill('0') << std::setw(2) << ff;
    
    return oss.str();
}

// Helper: Sanity check CC data byte
int32_t CCSinkStage::sanity_check_data(int32_t data_byte) const {
    // Already marked as invalid?
    if (data_byte == -1) return -1;
    
    // Is it in the valid command byte range?
    if (data_byte >= 0x10 && data_byte <= 0x1F) {
        return data_byte;  // Valid command byte
    }
    
    // Valid 7-bit ASCII range?
    if (data_byte >= 0x20 && data_byte <= 0x7E) {
        return data_byte;  // Valid character byte
    }
    
    // Invalid byte
    return 0;
}

// Helper: Check if byte is a control code
bool CCSinkStage::is_control_code(uint8_t byte) const {
    return (byte >= 0x10 && byte <= 0x1F);
}

// Helper: Check if byte is a printable character
bool CCSinkStage::is_printable_char(uint8_t byte) const {
    return (byte >= 0x20 && byte <= 0x7E);
}

// Export to Scenarist SCC V1.0 format (disabled)
bool CCSinkStage::export_scc(const VideoFieldRepresentation* vfr,
                             const std::string& output_path,
                             VideoFormat format) {
    (void)vfr;
    (void)output_path;
    (void)format;
    ORC_LOG_WARN("CCSink: SCC export disabled (legacy observers removed)");
    return false;
}

// Export to plain text format using EIA608Decoder for proper caption parsing (disabled)
bool CCSinkStage::export_plain_text(const VideoFieldRepresentation* vfr,
                                   const std::string& output_path,
                                   VideoFormat format) {
    (void)vfr;
    (void)output_path;
    (void)format;
    ORC_LOG_WARN("CCSink: Plain text CC export disabled (legacy observers removed)");
    return false;
}

} // namespace orc
