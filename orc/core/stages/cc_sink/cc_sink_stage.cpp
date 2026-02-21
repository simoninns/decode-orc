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
#include "eia608_decoder.h"
#include "observation_context.h"
#include <stage_registry.h>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <memory>

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
        
        // Instantiate closed caption observer to extract CC data
        auto cc_observer = std::make_unique<ClosedCaptionObserver>();
        
        // Get field count to iterate over
        size_t field_count = vfr->field_count();
        if (field_count == 0) {
            throw std::runtime_error("Input has no fields");
        }
        
        // Process all fields with CC observer to populate context
        for (size_t i = 0; i < field_count; ++i) {
            FieldID field_id(static_cast<int32_t>(i + 1));
            if (vfr->has_field(field_id)) {
                cc_observer->process_field(*vfr, field_id, observation_context);
            }
            
            if (cancel_requested_.load()) {
                is_processing_.store(false);
                return false;
            }
            
            if (progress_callback_) {
                progress_callback_(i + 1, field_count, "Processing closed captions...");
            }
        }
        
        // Export based on format
        bool success = false;
        if (format == CCExportFormat::SCC) {
            ORC_LOG_INFO("Exporting closed captions to SCC format: {}", output_path);
            success = export_scc(vfr.get(), output_path, video_format, observation_context);
        } else {
            ORC_LOG_INFO("Exporting closed captions to plain text format: {}", output_path);
            success = export_plain_text(vfr.get(), output_path, video_format, observation_context);
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

// Export to Scenarist SCC V1.0 format
bool CCSinkStage::export_scc(const VideoFieldRepresentation* vfr,
                             const std::string& output_path,
                             VideoFormat format,
                             const ObservationContext& observation_context) {
    try {
        std::ofstream file(output_path);
        if (!file.is_open()) {
            ORC_LOG_ERROR("Failed to open output file: {}", output_path);
            return false;
        }
        
        // Write SCC header
        file << "Scenarist_SCC V1.0";
        
        // Process all fields
        bool caption_in_progress = false;
        std::string debug_caption;
        size_t field_count = vfr->field_count();
        int32_t processed_fields = 0;
        
        for (size_t i = 0; i < field_count; ++i) {
            FieldID field_id(static_cast<int32_t>(i + 1));
            if (!vfr->has_field(field_id)) {
                continue;
            }
            
            processed_fields++;
            
            // Extract CC data from observations
            int32_t data0 = -1;
            int32_t data1 = -1;
            
            // Check if CC data is present for this field
            auto present_obs = observation_context.get(field_id, "closed_caption", "present");
            if (present_obs && std::holds_alternative<bool>(*present_obs) && std::get<bool>(*present_obs)) {
                // Get CC data bytes
                auto data0_obs = observation_context.get(field_id, "closed_caption", "data0");
                auto data1_obs = observation_context.get(field_id, "closed_caption", "data1");
                
                if (data0_obs && data1_obs) {
                    // Check parity validity
                    auto parity0_obs = observation_context.get(field_id, "closed_caption", "parity0_valid");
                    auto parity1_obs = observation_context.get(field_id, "closed_caption", "parity1_valid");
                    
                    bool parity0_valid = parity0_obs && std::holds_alternative<bool>(*parity0_obs) ? std::get<bool>(*parity0_obs) : false;
                    bool parity1_valid = parity1_obs && std::holds_alternative<bool>(*parity1_obs) ? std::get<bool>(*parity1_obs) : false;
                    
                    // Process if at least one byte has valid parity
                    if (parity0_valid || parity1_valid) {
                        data0 = sanity_check_data(std::get<int32_t>(*data0_obs));
                        data1 = sanity_check_data(std::get<int32_t>(*data1_obs));
                    }
                }
            }
            
            if (processed_fields <= 10) {
                ORC_LOG_DEBUG("SCC Field {}: data0={:#04x}, data1={:#04x}", 
                              field_id.value(), data0, data1);
            }
            
            // Sometimes random data is passed through; sanity check makes sure
            // each new caption starts with data0 = 0x14
            if (!caption_in_progress && data0 > 0) {
                if (data0 != 0x14) {
                    data0 = 0;
                    data1 = 0;
                }
            }
            
            // Check if data is valid
            if (data0 == -1 || data1 == -1) {
                // Invalid - skip
            } else {
                // Valid
                if (data0 > 0 || data1 > 0) {
                    if (!caption_in_progress) {
                        // Start of new caption
                        std::string timestamp = generate_timestamp(static_cast<int32_t>(field_id.value()), format);
                        file << "\n\n" << timestamp << "\t";
                        
                        debug_caption = "Caption at " + timestamp + " : [";
                        caption_in_progress = true;
                    }
                    
                    // Output the 2 bytes as hex (e.g., 0x14 0x41 becomes "1441 ")
                    file << std::hex << std::setfill('0') << std::setw(2) << data0
                         << std::setfill('0') << std::setw(2) << data1 << " ";
                    
                    // Add to debug output
                    if (is_control_code(static_cast<uint8_t>(data0))) {
                        debug_caption += " ";  // Control code - show as space
                    } else {
                        char chars[3] = {static_cast<char>(data0), static_cast<char>(data1), 0};
                        debug_caption += std::string(chars);
                    }
                    
                } else {
                    // No CC data for this frame
                    if (caption_in_progress) {
                        debug_caption += "]";
                        ORC_LOG_DEBUG("{}", debug_caption);
                    }
                    caption_in_progress = false;
                }
            }
        }
        
        // Add trailing whitespace
        file << "\n\n";
        file.close();
        
        ORC_LOG_INFO("Exported SCC format closed captions");
        return true;
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("Error exporting SCC: {}", e.what());
        return false;
    }
}

// Export to plain text format
bool CCSinkStage::export_plain_text(const VideoFieldRepresentation* vfr,
                                   const std::string& output_path,
                                   VideoFormat format,
                                   const ObservationContext& observation_context) {
    try {
        std::ofstream file(output_path);
        if (!file.is_open()) {
            ORC_LOG_ERROR("Failed to open output file: {}", output_path);
            return false;
        }
        
        // Create EIA608Decoder to properly decode CC stream
        EIA608Decoder decoder;
        
        // Calculate frame rate for timing
        double frames_per_second = (format == VideoFormat::PAL) ? 25.0 : 29.97;
        
        // Iterate through fields and feed CC data to decoder
        size_t field_count = vfr->field_count();
        
        for (size_t i = 0; i < field_count; ++i) {
            FieldID field_id(static_cast<int32_t>(i + 1));
            if (!vfr->has_field(field_id)) {
                continue;
            }
            
            // Check if CC data is present for this field
            auto present_obs = observation_context.get(field_id, "closed_caption", "present");
            if (!present_obs || !std::holds_alternative<bool>(*present_obs) || !std::get<bool>(*present_obs)) {
                continue;  // No CC data on this field
            }
            
            // Get CC data bytes
            auto data0_obs = observation_context.get(field_id, "closed_caption", "data0");
            auto data1_obs = observation_context.get(field_id, "closed_caption", "data1");
            
            if (!data0_obs || !data1_obs) {
                continue;  // Incomplete CC data
            }
            
            int32_t data0 = std::get<int32_t>(*data0_obs);
            int32_t data1 = std::get<int32_t>(*data1_obs);
            
            // Check parity validity
            auto parity0_obs = observation_context.get(field_id, "closed_caption", "parity0_valid");
            auto parity1_obs = observation_context.get(field_id, "closed_caption", "parity1_valid");
            
            bool parity0_valid = parity0_obs && std::holds_alternative<bool>(*parity0_obs) ? std::get<bool>(*parity0_obs) : false;
            bool parity1_valid = parity1_obs && std::holds_alternative<bool>(*parity1_obs) ? std::get<bool>(*parity1_obs) : false;
            
            // Process if at least one byte has valid parity
            if (!parity0_valid && !parity1_valid) {
                continue;
            }
            
            // Sanity check the bytes
            uint8_t byte1 = static_cast<uint8_t>(sanity_check_data(data0));
            uint8_t byte2 = static_cast<uint8_t>(sanity_check_data(data1));
            
            // Calculate timestamp for this field
            double timestamp = (field_id.value() / 2.0) / frames_per_second;
            
            // Feed to decoder
            decoder.process_bytes(timestamp, byte1, byte2);
        }
        
        // Get all caption cues from decoder
        auto cues = decoder.get_cues();
        
        ORC_LOG_INFO("Extracted {} caption cues", cues.size());
        
        // Write cues to file with timestamps
        for (const auto& cue : cues) {
            // Convert seconds to timecode
            int frame_number = static_cast<int>(cue.start_time * frames_per_second * 2.0);  // *2 for fields
            std::string timestamp = generate_timestamp(frame_number, format);
            
            file << "\n[" << timestamp << "]\n";
            file << cue.text << "\n";
        }
        
        file.close();
        return true;
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("Error exporting plain text: {}", e.what());
        return false;
    }
}

} // namespace orc
