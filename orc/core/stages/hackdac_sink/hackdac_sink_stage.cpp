/*
 * File:        hackdac_sink_stage.cpp
 * Module:      orc-core
 * Purpose:     Hackdac sink stage implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "hackdac_sink_stage.h"
#include "buffered_file_io.h"
#include "logging.h"
#include "stage_registry.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace orc {

// Register stage with registry
ORC_REGISTER_STAGE(HackdacSinkStage)

// Force linker to include this object file
void force_link_HackdacSinkStage() {}

HackdacSinkStage::HackdacSinkStage() = default;

NodeTypeInfo HackdacSinkStage::get_node_type_info() const {
    return NodeTypeInfo{
        NodeType::SINK,
        "hackdac_sink",
        "Hackdac Sink",
        "Exports signed 16-bit field data without half-line padding for Hackdac (.hdac) output.",
        1,  // min_inputs
        1,  // max_inputs
        0,  // min_outputs
        0,  // max_outputs
        VideoFormatCompatibility::ALL
    };
}

std::vector<ArtifactPtr> HackdacSinkStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    ObservationContext& observation_context) {
    // Sink stages do not emit artifacts during execute(); trigger() performs the export.
    (void)inputs;
    (void)parameters;
    (void)observation_context;
    return {};
}

std::vector<ParameterDescriptor> HackdacSinkStage::get_parameter_descriptors(
    VideoSystem project_format,
    SourceType source_type) const {
    (void)project_format;
    (void)source_type;

    std::vector<ParameterDescriptor> descriptors;

    // Output path (.hdac)
    descriptors.push_back(ParameterDescriptor{
        "output_path",
        "Hackdac Output Path",
        "Destination .hdac file (signed 16-bit). A companion .txt report will be written next to it.",
        ParameterType::FILE_PATH,
        ParameterConstraints{std::nullopt, std::nullopt, std::string(""), {}, true, std::nullopt},
        ".hdac"
    });

    return descriptors;
}

std::map<std::string, ParameterValue> HackdacSinkStage::get_parameters() const {
    return parameters_;
}

bool HackdacSinkStage::set_parameters(const std::map<std::string, ParameterValue>& params) {
    parameters_ = params;
    return true;
}

HackdacSinkStage::ParsedConfig HackdacSinkStage::parse_config(
    const std::map<std::string, ParameterValue>& parameters) const {
    ParsedConfig cfg;

    auto out_it = parameters.find("output_path");
    if (out_it == parameters.end() || !std::holds_alternative<std::string>(out_it->second)) {
        throw std::runtime_error("output_path parameter is required and must be a string");
    }
    cfg.output_path = std::get<std::string>(out_it->second);
    if (cfg.output_path.empty()) {
        throw std::runtime_error("output_path cannot be empty");
    }

    // Ensure .hdac extension
    const std::string ext = ".hdac";
    if (cfg.output_path.size() < ext.size() || cfg.output_path.substr(cfg.output_path.size() - ext.size()) != ext) {
        cfg.output_path += ext;
    }

    // Derive report path (replace extension with .txt)
    cfg.report_path = cfg.output_path;
    auto dot_pos = cfg.report_path.find_last_of('.');
    if (dot_pos != std::string::npos) {
        cfg.report_path = cfg.report_path.substr(0, dot_pos);
    }
    cfg.report_path += ".txt";

    return cfg;
}

int16_t HackdacSinkStage::to_signed_sample(uint16_t sample) {
    // Shift unsigned 16-bit domain so that 32768 becomes 0
    int32_t shifted = static_cast<int32_t>(sample) - 32768;
    // Clamp to int16_t range just in case
    if (shifted > std::numeric_limits<int16_t>::max()) return std::numeric_limits<int16_t>::max();
    if (shifted < std::numeric_limits<int16_t>::min()) return std::numeric_limits<int16_t>::min();
    return static_cast<int16_t>(shifted);
}

std::string HackdacSinkStage::system_to_string(VideoSystem system) {
    switch (system) {
        case VideoSystem::PAL: return "PAL";
        case VideoSystem::NTSC: return "NTSC";
        case VideoSystem::PAL_M: return "PAL_M";
        default: return "Unknown";
    }
}

bool HackdacSinkStage::write_report(const std::string& report_path,
                                    VideoSystem resolved_system,
                                    size_t input_line_width,
                                    size_t input_line_count,
                                    size_t half_line_samples,
                                    size_t output_samples_per_field,
                                    size_t processed_fields,
                                    const std::optional<VideoParameters>& video_params) const {
    std::ofstream report(report_path, std::ios::out | std::ios::trunc);
    if (!report.is_open()) {
        ORC_LOG_WARN("HackdacSink: Failed to write report file {}", report_path);
        return false;
    }

    const size_t bytes_per_sample = sizeof(int16_t);
    const size_t bytes_per_field = output_samples_per_field * bytes_per_sample;
    const size_t total_bytes = bytes_per_field * processed_fields;
    const size_t removed_samples_per_field = half_line_samples;
    const size_t removed_bytes_per_field = removed_samples_per_field * bytes_per_sample;
    const size_t total_removed_bytes = removed_bytes_per_field * processed_fields;

    report << "Hackdac sink export report\n";
    report << "Format: headerless stream of 16-bit signed little-endian samples (fields concatenated in capture order)\n";
    report << "Video format: " << system_to_string(resolved_system) << "\n";
    report << "Input line width: " << input_line_width << " samples\n";
    report << "Input lines per field: " << input_line_count << "\n";
    report << "Half-line removed: " << half_line_samples << " samples per field\n";
    report << "Samples per field (output): " << output_samples_per_field << "\n";
    report << "Fields exported: " << processed_fields << "\n";
    report << "Bytes per field: " << bytes_per_field << "\n";
    report << "Total data bytes: " << total_bytes << "\n";
    report << "Removed padding per field: " << removed_samples_per_field << " samples (" << removed_bytes_per_field << " bytes)\n";
    report << "Total removed padding: " << total_removed_bytes << " bytes\n";

    const bool have_levels = video_params &&
                             video_params->blanking_16b_ire >= 0 &&
                             video_params->black_16b_ire >= 0 &&
                             video_params->white_16b_ire >= 0;

    if (have_levels) {
        auto to_signed = [](int32_t value) { return value - 32768; };
        report << "Blanking level (signed 16-bit): " << to_signed(video_params->blanking_16b_ire) << "\n";
        report << "Black level (signed 16-bit): " << to_signed(video_params->black_16b_ire) << "\n";
        report << "White level (signed 16-bit): " << to_signed(video_params->white_16b_ire) << "\n";
    } else {
        report << "Blanking level (signed 16-bit): unknown\n";
        report << "Black level (signed 16-bit): unknown\n";
        report << "White level (signed 16-bit): unknown\n";
    }

    return true;
}

bool HackdacSinkStage::trigger(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    ObservationContext& observation_context) {
    (void)observation_context;

    ORC_LOG_DEBUG("HackdacSink: Trigger started");
    is_processing_.store(true);
    cancel_requested_.store(false);

    try {
        if (inputs.empty()) {
            throw std::runtime_error("No input connected");
        }

        auto vfr = std::dynamic_pointer_cast<VideoFieldRepresentation>(inputs[0]);
        if (!vfr) {
            throw std::runtime_error("Input is not a VideoFieldRepresentation");
        }

        ParsedConfig cfg = parse_config(parameters);

        auto field_range = vfr->field_range();
        if (field_range.size() == 0) {
            throw std::runtime_error("Input has no fields to export");
        }

        // Find first available field descriptor
        std::optional<FieldDescriptor> descriptor;
        FieldID first_field = field_range.start;
        while (first_field < field_range.end && !descriptor) {
            if (vfr->has_field(first_field)) {
                descriptor = vfr->get_descriptor(first_field);
            }
            first_field = first_field + 1;
        }

        if (!descriptor) {
            throw std::runtime_error("Unable to read field descriptor");
        }

        size_t line_width = descriptor->width;
        size_t line_count = descriptor->height;
        if (line_width == 0 || line_count == 0) {
            throw std::runtime_error("Invalid field dimensions");
        }

        size_t half_line_samples = line_width / 2;
        size_t expected_input_samples = line_width * line_count;
        size_t output_samples_per_field = expected_input_samples >= half_line_samples
                                            ? expected_input_samples - half_line_samples
                                            : 0;

        auto video_params = vfr->get_video_parameters();
        VideoSystem resolved_system = VideoSystem::Unknown;
        if (video_params && video_params->is_valid()) {
            resolved_system = video_params->system;
        } else {
            // Use descriptor format as a hint
            if (descriptor->format == VideoFormat::PAL) resolved_system = VideoSystem::PAL;
            if (descriptor->format == VideoFormat::NTSC) resolved_system = VideoSystem::NTSC;
        }

        BufferedFileWriter<int16_t> writer(16 * 1024 * 1024);
        if (!writer.open(cfg.output_path)) {
            throw std::runtime_error("Failed to open output file: " + cfg.output_path);
        }

        uint64_t total_fields = field_range.size();
        uint64_t processed_fields = 0;

        for (FieldID fid = field_range.start; fid < field_range.end; fid = fid + 1) {
            if (cancel_requested_.load()) {
                writer.close();
                last_status_ = "Cancelled by user";
                ORC_LOG_WARN("HackdacSink: {}", last_status_);
                is_processing_.store(false);
                return false;
            }

            if (!vfr->has_field(fid)) {
                continue;
            }

            auto field_data = vfr->get_field(fid);
            if (field_data.empty()) {
                ORC_LOG_WARN("HackdacSink: Field {} is empty, writing zeros", fid.value());
                field_data.resize(expected_input_samples, 0);
            }

            if (field_data.size() < half_line_samples) {
                throw std::runtime_error("Field data too short to remove half-line padding");
            }

            size_t usable_samples = field_data.size() - half_line_samples;
            std::vector<int16_t> signed_data;
            signed_data.reserve(usable_samples);
            for (size_t i = 0; i < usable_samples; ++i) {
                signed_data.push_back(to_signed_sample(field_data[i]));
            }

            writer.write(signed_data);
            processed_fields++;

            if (progress_callback_ && processed_fields % 10 == 0) {
                progress_callback_(processed_fields, total_fields,
                                   "Exporting field " + std::to_string(processed_fields) + "/" + std::to_string(total_fields));
            }
        }

        writer.close();

        // Write companion report
        write_report(cfg.report_path, resolved_system, line_width, line_count,
                 half_line_samples, output_samples_per_field, processed_fields, video_params);

        last_status_ = "Success: " + std::to_string(processed_fields) + " fields exported";
        ORC_LOG_INFO("HackdacSink: {}", last_status_);
        is_processing_.store(false);
        return true;

    } catch (const std::exception& e) {
        last_status_ = std::string("Error: ") + e.what();
        ORC_LOG_ERROR("HackdacSink: {}", e.what());
        is_processing_.store(false);
        return false;
    }
}

std::string HackdacSinkStage::get_trigger_status() const {
    return last_status_;
}

} // namespace orc
