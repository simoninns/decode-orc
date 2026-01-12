/*
 * File:        audio_sink_stage.cpp
 * Module:      orc-core
 * Purpose:     Analogue Audio Sink Stage - writes PCM audio to WAV file
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "audio_sink_stage.h"
#include "logging.h"
#include "tbc_metadata.h"
#include "buffered_file_io.h"
#include <stage_registry.h>
#include <stdexcept>
#include <fstream>
#include <cstring>

namespace orc {

// Register this stage with the registry
ORC_REGISTER_STAGE(AudioSinkStage)

// Force linker to include this object file
void force_link_AudioSinkStage() {}

AudioSinkStage::AudioSinkStage() = default;

NodeTypeInfo AudioSinkStage::get_node_type_info() const {
    return NodeTypeInfo{
        NodeType::SINK,
        "AudioSink",
        "Analogue Audio Sink",
        "Extracts analogue audio PCM data and writes to WAV file",
        1, 1,  // One input
        0, 0,  // No outputs (sink)
        VideoFormatCompatibility::ALL
    };
}

std::vector<ArtifactPtr> AudioSinkStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters
) {
    // Sink stages don't produce outputs in execute()
    // Actual work happens in trigger()
    (void)inputs;
    (void)parameters;
    return {};
}

std::vector<ParameterDescriptor> AudioSinkStage::get_parameter_descriptors(VideoSystem project_format, SourceType source_type) const {
    (void)project_format;
    (void)source_type;
    std::vector<ParameterDescriptor> descriptors;
    
    // output_path parameter
    {
        ParameterDescriptor desc;
        desc.name = "output_path";
        desc.display_name = "Output WAV File";
        desc.description = "Path to output WAV audio file";
        desc.type = ParameterType::FILE_PATH;
        desc.constraints.required = true;
        desc.constraints.default_value = std::string("");
        desc.file_extension_hint = ".wav";
        descriptors.push_back(desc);
    }
    
    return descriptors;
}

std::map<std::string, ParameterValue> AudioSinkStage::get_parameters() const {
    return parameters_;
}

bool AudioSinkStage::set_parameters(const std::map<std::string, ParameterValue>& params) {
    parameters_ = params;
    return true;
}

bool AudioSinkStage::write_wav_header(std::ofstream& out, uint32_t num_samples, 
                                     uint32_t sample_rate, uint16_t num_channels, 
                                     uint16_t bits_per_sample) {
    uint32_t byte_rate = sample_rate * num_channels * (bits_per_sample / 8);
    uint16_t block_align = num_channels * (bits_per_sample / 8);
    uint32_t data_size = num_samples * num_channels * (bits_per_sample / 8);
    uint32_t file_size = 36 + data_size;  // RIFF header overhead is 36 bytes
    
    // RIFF header
    out.write("RIFF", 4);
    out.write(reinterpret_cast<const char*>(&file_size), 4);
    out.write("WAVE", 4);
    
    // fmt chunk
    out.write("fmt ", 4);
    uint32_t fmt_size = 16;  // PCM format chunk size
    out.write(reinterpret_cast<const char*>(&fmt_size), 4);
    
    uint16_t audio_format = 1;  // PCM
    out.write(reinterpret_cast<const char*>(&audio_format), 2);
    out.write(reinterpret_cast<const char*>(&num_channels), 2);
    out.write(reinterpret_cast<const char*>(&sample_rate), 4);
    out.write(reinterpret_cast<const char*>(&byte_rate), 4);
    out.write(reinterpret_cast<const char*>(&block_align), 2);
    out.write(reinterpret_cast<const char*>(&bits_per_sample), 2);
    
    // data chunk header
    out.write("data", 4);
    out.write(reinterpret_cast<const char*>(&data_size), 4);
    
    return out.good();
}

bool AudioSinkStage::trigger(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters
) {
    is_processing_.store(true);
    cancel_requested_.store(false);
    
    try {
        // Validate inputs
        if (inputs.empty()) {
            throw std::runtime_error("Audio sink requires one input (VideoFieldRepresentation)");
        }
        
        auto vfr = std::dynamic_pointer_cast<VideoFieldRepresentation>(inputs[0]);
        if (!vfr) {
            throw std::runtime_error("Input must be a VideoFieldRepresentation");
        }
        
        // Check if VFR has audio
        if (!vfr->has_audio()) {
            throw std::runtime_error("Input VFR does not have audio data (no PCM file specified in source?)");
        }
        
        // Get parameters
        auto output_path_it = parameters.find("output_path");
        if (output_path_it == parameters.end()) {
            throw std::runtime_error("output_path parameter is required");
        }
        std::string output_path = std::get<std::string>(output_path_it->second);
        
        ORC_LOG_INFO("AudioSink: Writing audio to {}", output_path);
        
        // Calculate field range
        auto field_range = vfr->field_range();
        FieldID start_field = field_range.start;
        FieldID end_field = field_range.end;
        
        uint64_t total_fields = end_field.value() - start_field.value();
        ORC_LOG_DEBUG("  Processing {} fields", total_fields);
        
        // First pass: count total samples
        uint64_t total_samples = 0;
        for (FieldID fid = start_field; fid < end_field; ++fid) {
            total_samples += vfr->get_audio_sample_count(fid);
        }
        
        ORC_LOG_DEBUG("  Total audio samples: {} ({:.2f} seconds at 44.1kHz)", 
                    total_samples, total_samples / 44100.0);
        
        if (total_samples == 0) {
            throw std::runtime_error("No audio samples found in field range");
        }
        
        // Open output file with buffered writer (4MB buffer for optimal performance)
        BufferedFileWriter<int16_t> writer(4 * 1024 * 1024);
        if (!writer.open(output_path)) {
            throw std::runtime_error("Failed to open output file: " + output_path);
        }
        
        // Write WAV header using a temporary ofstream
        // (header is small, no need for buffering)
        {
            std::ofstream header_out(output_path, std::ios::binary);
            const uint32_t sample_rate = 44100;
            const uint16_t num_channels = 2;  // Stereo
            const uint16_t bits_per_sample = 16;
            
            if (!write_wav_header(header_out, total_samples, sample_rate, num_channels, bits_per_sample)) {
                throw std::runtime_error("Failed to write WAV header");
            }
            header_out.close();
        }
        
        // Reopen with buffered writer in append mode to write audio data
        writer.open(output_path, std::ios::binary | std::ios::app);
        
        // Second pass: write audio data using buffered writer
        uint64_t samples_written = 0;
        uint64_t frames_written = 0;
        for (FieldID fid = start_field; fid < end_field; ++fid) {
            if (cancel_requested_.load()) {
                writer.close();
                last_status_ = "Cancelled by user";
                ORC_LOG_WARN("AudioSink: {}", last_status_);
                is_processing_.store(false);
                return false;
            }
            
            // Get audio samples for this field
            auto samples = vfr->get_audio_samples(fid);
            if (!samples.empty()) {
                // Write samples using buffered writer (automatically batches writes)
                writer.write(samples);
                samples_written += samples.size();  // Total int16_t values written
                frames_written += samples.size() / 2;  // Stereo frames (each frame = L+R sample pair)
            }
            
            // Update progress
            uint64_t current_field = fid.value() - start_field.value();
            if (current_field % 10 == 0 && progress_callback_) {
                progress_callback_(current_field, total_fields, 
                                 "Writing audio field " + std::to_string(current_field) + "/" + std::to_string(total_fields));
            }
            if (current_field % 100 == 0) {
                double progress = static_cast<double>(current_field) / total_fields * 100.0;
                ORC_LOG_DEBUG("AudioSink: Progress {:.1f}%", progress);
            }
        }
        
        writer.close();
        
        ORC_LOG_INFO("AudioSink: Successfully wrote {} frames ({} channel samples) to {}", 
                    frames_written, samples_written, output_path);
        ORC_LOG_DEBUG("  Expected frames: {}, Actual frames: {}, Match: {}", 
                     total_samples, frames_written, total_samples == frames_written ? "YES" : "NO");
        
        last_status_ = "Success: " + std::to_string(frames_written) + " samples written";
        is_processing_.store(false);
        return true;
        
    } catch (const std::exception& e) {
        last_status_ = std::string("Error: ") + e.what();
        ORC_LOG_ERROR("AudioSink: {}", e.what());
        is_processing_.store(false);
        return false;
    }
}

std::string AudioSinkStage::get_trigger_status() const {
    return last_status_;
}

} // namespace orc
