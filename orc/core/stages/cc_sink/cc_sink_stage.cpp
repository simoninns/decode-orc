/*
 * File:        cc_sink_stage.cpp
 * Module:      orc-core
 * Purpose:     Closed Caption Sink Stage - exports CC data to SCC or plain text
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "cc_sink_stage.h"
#include "logging.h"
#include "closed_caption_observer.h"
#include "observation_history.h"
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
    const std::map<std::string, ParameterValue>& parameters
) {
    // Sink stages don't produce outputs in execute()
    // Actual work happens in trigger()
    (void)inputs;
    (void)parameters;
    return {};
}

std::vector<ParameterDescriptor> CCSinkStage::get_parameter_descriptors(VideoSystem project_format) const {
    (void)project_format;
    std::vector<ParameterDescriptor> descriptors;
    
    // output_path parameter
    {
        ParameterDescriptor desc;
        desc.name = "output_path";
        desc.display_name = "Output File";
        desc.description = "Path to output closed caption file";
        desc.type = ParameterType::FILE_PATH;
        desc.constraints.required = true;
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
    const std::map<std::string, ParameterValue>& parameters
) {
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

// Export to Scenarist SCC V1.0 format
bool CCSinkStage::export_scc(const VideoFieldRepresentation* vfr,
                             const std::string& output_path,
                             VideoFormat format) {
    // Open output file
    std::ofstream file(output_path);
    if (!file.is_open()) {
        ORC_LOG_ERROR("Could not open output file: {}", output_path);
        return false;
    }
    
    // Write SCC V1.0 header
    file << "Scenarist_SCC V1.0";
    
    // Create CC observer to extract data
    auto cc_observer = std::make_shared<ClosedCaptionObserver>();
    ObservationHistory history;
    
    // Get field range
    auto field_range = vfr->field_range();
    int32_t total_fields = (field_range.end.value() - field_range.start.value());
    
    ORC_LOG_DEBUG("SCC export: Processing {} fields from {} to {}", 
                  total_fields, field_range.start.value(), field_range.end.value());
    
    // Process all fields
    bool caption_in_progress = false;
    std::string debug_caption;
    int32_t processed_fields = 0;
    
    for (FieldID field_id = field_range.start; field_id < field_range.end; field_id = field_id + 1) {
        if (cancel_requested_.load()) {
            ORC_LOG_INFO("CC export cancelled by user");
            file.close();
            return false;
        }
        
        // Report progress
        if (progress_callback_ && processed_fields % 100 == 0) {
            progress_callback_(processed_fields, total_fields, "Exporting closed captions...");
        }
        processed_fields++;
        
        // Run CC observer on this field
        int32_t data0 = -1;
        int32_t data1 = -1;
        
        auto observations = cc_observer->process_field(*vfr, field_id, history);
        if (processed_fields <= 10) {
            ORC_LOG_DEBUG("SCC Field {}: {} observations from observer", field_id.value(), observations.size());
        }
        
        for (const auto& obs : observations) {
            if (obs->observation_type() == "ClosedCaption") {
                auto* cc_obs = dynamic_cast<ClosedCaptionObservation*>(obs.get());
                if (cc_obs) {
                    if (processed_fields <= 10) {
                        ORC_LOG_DEBUG("SCC Field {}: CC obs - data0={:#04x}, data1={:#04x}, confidence={}", 
                                      field_id.value(), cc_obs->data0, cc_obs->data1, 
                                      static_cast<int>(cc_obs->confidence));
                    }
                    if (cc_obs->confidence != ConfidenceLevel::NONE) {
                        data0 = sanity_check_data(cc_obs->data0);
                        data1 = sanity_check_data(cc_obs->data1);
                    }
                }
                break;
            }
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
                    std::string timestamp = generate_timestamp(field_id.value(), format);
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
    
    return true;
}

// Export to plain text format (strip control codes)
bool CCSinkStage::export_plain_text(const VideoFieldRepresentation* vfr,
                                   const std::string& output_path,
                                   VideoFormat format) {
    // Open output file
    std::ofstream file(output_path);
    if (!file.is_open()) {
        ORC_LOG_ERROR("Could not open output file: {}", output_path);
        return false;
    }
    
    // Create CC observer to extract data
    auto cc_observer = std::make_shared<ClosedCaptionObserver>();
    ObservationHistory history;
    
    // Get field range
    auto field_range = vfr->field_range();
    int32_t total_fields = (field_range.end.value() - field_range.start.value());
    
    ORC_LOG_DEBUG("Plain text export: Processing {} fields from {} to {}", 
                  total_fields, field_range.start.value(), field_range.end.value());
    
    // Process all fields
    bool caption_in_progress = false;
    int32_t processed_fields = 0;
    int32_t last_control_data0 = -1;
    int32_t last_control_data1 = -1;
    bool last_output_was_space = false;
    
    for (FieldID field_id = field_range.start; field_id < field_range.end; field_id = field_id + 1) {
        if (cancel_requested_.load()) {
            ORC_LOG_INFO("CC export cancelled by user");
            file.close();
            return false;
        }
        
        // Report progress
        if (progress_callback_ && processed_fields % 100 == 0) {
            progress_callback_(processed_fields, total_fields, "Exporting closed captions...");
        }
        processed_fields++;
        
        // Run CC observer on this field
        int32_t data0 = -1;
        int32_t data1 = -1;
        
        auto observations = cc_observer->process_field(*vfr, field_id, history);
        if (processed_fields <= 10) {
            ORC_LOG_DEBUG("Plain text Field {}: {} observations from observer", field_id.value(), observations.size());
        }
        
        for (const auto& obs : observations) {
            if (obs->observation_type() == "ClosedCaption") {
                auto* cc_obs = dynamic_cast<ClosedCaptionObservation*>(obs.get());
                if (cc_obs) {
                    if (processed_fields <= 10) {
                        ORC_LOG_DEBUG("Plain text Field {}: CC obs - data0={:#04x}, data1={:#04x}, confidence={}", 
                                      field_id.value(), cc_obs->data0, cc_obs->data1, 
                                      static_cast<int>(cc_obs->confidence));
                    }
                    if (cc_obs->confidence != ConfidenceLevel::NONE) {
                        data0 = sanity_check_data(cc_obs->data0);
                        data1 = sanity_check_data(cc_obs->data1);
                    }
                }
                break;
            }
        }
        
        // Check if data is valid
        if (data0 == -1 || data1 == -1) {
            // Invalid - skip
            continue;
        }
        
        // Check for null data
        if (data0 == 0 && data1 == 0) {
            // No CC data for this frame
            if (caption_in_progress) {
                file << "\n";  // End caption with newline
            }
            caption_in_progress = false;
            continue;
        }
        
        // Debug: Log all non-zero data
        ORC_LOG_DEBUG("CC field {}: data0=0x{:02x}, data1=0x{:02x}", field_id.value(), data0, data1);
        
        // Following the ld-analyse reference implementation:
        // Check for non-display control codes (0x10-0x1F in data0)
        if (data0 >= 0x10 && data0 <= 0x1F) {
            // Check if this is a repeated control code (command repeat)
            if (data0 == last_control_data0 && data1 == last_control_data1) {
                // Command repeat - ignore
                ORC_LOG_DEBUG("  -> Control code repeat, skipping");
                continue;
            }
            
            ORC_LOG_DEBUG("  -> Control code 0x{:02x}, data1=0x{:02x}", data0, data1);
            last_control_data0 = data0;
            last_control_data1 = data1;
            
            // Decode control codes to determine if we should output spacing
            // Only specific commands output spaces/newlines in plain text
            if (data1 >= 0x20 && data1 <= 0x7F) {
                // Check for miscellaneous control codes: (data0 & 0x76) == 0x14
                if ((data0 & 0x76) == 0x14) {
                    int32_t commandGroup = (data0 & 0x02) >> 1;
                    int32_t commandType = (data1 & 0x0F);
                    
                    if (commandGroup == 0) {
                        // Normal miscellaneous commands
                        switch (commandType) {
                        case 0:  // Resume caption loading
                        case 2:  // Reserved 1
                        case 4:  // Delete to end of row
                            // These output a space, but avoid consecutive spaces
                            if (!last_output_was_space) {
                                if (!caption_in_progress) {
                                    std::string timestamp = generate_timestamp(field_id.value(), format);
                                    file << "\n[" << timestamp << "]\n";
                                    caption_in_progress = true;
                                }
                                file << " ";
                                last_output_was_space = true;
                            }
                            break;
                        case 15: // End of caption (flip memories)
                            // Output newline
                            file << "\n";
                            caption_in_progress = false;
                            last_output_was_space = false;
                            break;
                        default:
                            // Other commands don't output anything
                            break;
                        }
                    }
                    // Tab offset commands (commandGroup == 1) don't output anything for plain text
                }
                // Midrow commands and preamble address codes don't output anything for plain text
            }
            continue;
        }
        
        // Reset control code tracking when we get normal text
        last_control_data0 = -1;
        last_control_data1 = -1;
        
        // Mask off parity bit (bit 7) to get 7-bit ASCII value
        uint8_t char0 = static_cast<uint8_t>(data0) & 0x7F;
        uint8_t char1 = static_cast<uint8_t>(data1) & 0x7F;
        
        // Create text output from the two characters
        std::string text;
        if (char0 >= 0x20 && char0 < 0x7F) {
            // Skip space if last output was already a space (collapse multiple spaces)
            if (char0 != ' ' || !last_output_was_space) {
                text += static_cast<char>(char0);
            }
        }
        if (char1 >= 0x20 && char1 < 0x7F) {
            // Skip space if last output was already a space (collapse multiple spaces)
            if (char1 != ' ' || !last_output_was_space) {
                text += static_cast<char>(char1);
            }
        }
        
        // Only output if we have printable text
        if (!text.empty()) {
            if (!caption_in_progress) {
                // Start of new caption
                std::string timestamp = generate_timestamp(field_id.value(), format);
                file << "\n[" << timestamp << "]\n";
                caption_in_progress = true;
            }
            file << text;
            // Track whether last character was a space
            last_output_was_space = !text.empty() && text.back() == ' ';
        }
    }
    
    file << "\n";
    file.close();
    
    return true;
}

} // namespace orc
