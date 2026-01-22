/*
 * File:        dropout_analysis_sink_stage.cpp
 * Module:      orc-core
 * Purpose:     Dropout Analysis Sink Stage implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "dropout_analysis_sink_stage.h"
#include "logging.h"
#include "stage_registry.h"
#include <fstream>
#include <map>
#include <stdexcept>

namespace orc {

// Register stage with registry
ORC_REGISTER_STAGE(DropoutAnalysisSinkStage)

// Force linker to include this object file
void force_link_DropoutAnalysisSinkStage() {}

DropoutAnalysisSinkStage::DropoutAnalysisSinkStage() = default;

NodeTypeInfo DropoutAnalysisSinkStage::get_node_type_info() const {
    return NodeTypeInfo{
        NodeType::ANALYSIS_SINK,
        "dropout_analysis_sink",
        "Dropout Analysis Sink",
        "Computes dropout statistics and optionally writes CSV. Trigger to update dataset.",
        1,  // min_inputs
        1,  // max_inputs
        0,  // min_outputs
        0,  // max_outputs
        VideoFormatCompatibility::ALL
    };
}

std::vector<ArtifactPtr> DropoutAnalysisSinkStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    ObservationContext& observation_context) {
    (void)inputs;
    (void)parameters;
    (void)observation_context;
    // Sink stages do not emit artifacts during execute(); trigger() performs the work.
    return {};
}

std::vector<ParameterDescriptor> DropoutAnalysisSinkStage::get_parameter_descriptors(
    VideoSystem project_format,
    SourceType source_type) const {
    (void)project_format;
    (void)source_type;

    std::vector<ParameterDescriptor> descriptors;

    descriptors.push_back(ParameterDescriptor{
        "output_path",
        "CSV Output Path",
        "Destination CSV file for dropout metrics. Leave empty to skip file output.",
        ParameterType::FILE_PATH,
        ParameterConstraints{std::nullopt, std::nullopt, std::string(""), {}, false, std::nullopt},
        ".csv"
    });

    descriptors.push_back(ParameterDescriptor{
        "write_csv",
        "Write CSV",
        "Enable writing results to CSV at trigger time.",
        ParameterType::BOOL,
        ParameterConstraints{std::nullopt, std::nullopt, ParameterValue(false), {}, false, std::nullopt}
    });

    descriptors.push_back(ParameterDescriptor{
        "mode",
        "Analysis Mode",
        "Choose full-field or visible-area dropout analysis.",
        ParameterType::STRING,
        ParameterConstraints{std::nullopt, std::nullopt, std::string("full"), {"full", "visible"}, true, std::nullopt}
    });

    descriptors.push_back(ParameterDescriptor{
        "max_frames",
        "Max Frames",
        "Deprecated: data is automatically binned to ~1000 points based on total frames (0 = auto).",
        ParameterType::UINT32,
        ParameterConstraints{ParameterValue(0U), std::nullopt, ParameterValue(0U), {}, false, std::nullopt}
    });

    return descriptors;
}

std::map<std::string, ParameterValue> DropoutAnalysisSinkStage::get_parameters() const {
    return parameters_;
}

bool DropoutAnalysisSinkStage::set_parameters(const std::map<std::string, ParameterValue>& params) {
    parameters_ = params;
    return true;
}

DropoutAnalysisSinkStage::ParsedConfig DropoutAnalysisSinkStage::parse_config(
    const std::map<std::string, ParameterValue>& parameters) const {
    ParsedConfig cfg;

    // output_path
    auto out_it = parameters.find("output_path");
    if (out_it != parameters.end() && std::holds_alternative<std::string>(out_it->second)) {
        cfg.output_path = std::get<std::string>(out_it->second);
    }

    // write_csv
    auto csv_it = parameters.find("write_csv");
    if (csv_it != parameters.end() && std::holds_alternative<bool>(csv_it->second)) {
        cfg.write_csv = std::get<bool>(csv_it->second);
    }

    // mode
    auto mode_it = parameters.find("mode");
    if (mode_it != parameters.end() && std::holds_alternative<std::string>(mode_it->second)) {
        auto m = std::get<std::string>(mode_it->second);
        if (m == "visible") cfg.mode = DropoutAnalysisMode::VISIBLE_AREA;
        else cfg.mode = DropoutAnalysisMode::FULL_FIELD;
    }

    // max_frames
    auto max_it = parameters.find("max_frames");
    if (max_it != parameters.end() && std::holds_alternative<uint32_t>(max_it->second)) {
        cfg.max_frames = static_cast<size_t>(std::get<uint32_t>(max_it->second));
    }

    return cfg;
}

bool DropoutAnalysisSinkStage::trigger(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    ObservationContext& observation_context) {
    (void)observation_context;

    ORC_LOG_DEBUG("DropoutAnalysisSink: Trigger started");
    is_processing_.store(true);
    cancel_requested_.store(false);
    has_results_ = false;
    frame_stats_.clear();
    total_frames_ = 0;

    try {
        if (inputs.empty()) {
            throw std::runtime_error("No input connected");
        }

        auto vfr = std::dynamic_pointer_cast<VideoFieldRepresentation>(inputs[0]);
        if (!vfr) {
            throw std::runtime_error("Input is not a VideoFieldRepresentation");
        }

        ParsedConfig cfg = parse_config(parameters);
        last_mode_ = cfg.mode;

        compute_stats(*vfr, cfg);

        // If cancelled, don't write CSV and mark results as invalid
        if (cancel_requested_.load()) {
            last_status_ = "Cancelled by user";
            has_results_ = false;
            frame_stats_.clear();
            total_frames_ = 0;
            is_processing_.store(false);
            return false;
        }

        if (cfg.write_csv && !cfg.output_path.empty()) {
            if (!write_csv(cfg.output_path)) {
                ORC_LOG_WARN("DropoutAnalysisSink: Failed to write CSV to {}", cfg.output_path);
            }
        }

        last_status_ = "Dropout analysis complete";
        has_results_ = true;
        is_processing_.store(false);
        return true;

    } catch (const std::exception& e) {
        last_status_ = std::string("Error: ") + e.what();
        ORC_LOG_ERROR("DropoutAnalysisSink: Trigger failed: {}", e.what());
        is_processing_.store(false);
        return false;
    }
}

void DropoutAnalysisSinkStage::compute_stats(const VideoFieldRepresentation& vfr, const ParsedConfig& cfg) {
    frame_stats_.clear();
    total_frames_ = 0;

    auto range = vfr.field_range();
    if (range.size() == 0) {
        ORC_LOG_WARN("DropoutAnalysisSink: No fields available");
        return;
    }

    size_t total_fields = range.size();
    auto active_hint = vfr.get_active_line_hint();
    auto video_params = vfr.get_video_parameters();

    struct FrameAccumulation {
        double total_dropout_length = 0.0;
        double dropout_count = 0.0;
        bool has_data = false;
    };

    std::map<int32_t, FrameAccumulation> frame_accum;

    for (size_t i = 0; i < total_fields; ++i) {
        if (cancel_requested_.load()) {
            ORC_LOG_WARN("DropoutAnalysisSink: Cancel requested at field {}", i);
            break;
        }

        FieldID fid(range.start.value() + i);
        auto field_descriptor = vfr.get_descriptor(fid);
        if (!field_descriptor) {
            continue;
        }

        auto dropouts = vfr.get_dropout_hints(fid);

        double field_dropout_length = 0.0;
        size_t field_dropout_count = 0;

        for (const auto& dropout : dropouts) {
            bool include = true;
            
            // For visible area mode, check both line range and sample range
            if (cfg.mode == DropoutAnalysisMode::VISIBLE_AREA) {
                // Check vertical range (lines)
                if (active_hint) {
                    if (static_cast<int32_t>(dropout.line) < active_hint->first_active_field_line ||
                        static_cast<int32_t>(dropout.line) > active_hint->last_active_field_line) {
                        include = false;
                    }
                }
                
                // Check horizontal range (samples) - only count dropouts within active video area
                if (include && video_params && 
                    video_params->active_video_start >= 0 && 
                    video_params->active_video_end >= 0) {
                    // Skip dropouts that are completely outside the active area
                    if (static_cast<int32_t>(dropout.end_sample) <= video_params->active_video_start ||
                        static_cast<int32_t>(dropout.start_sample) >= video_params->active_video_end) {
                        include = false;
                    }
                }
            }
            
            if (include) {
                // Calculate dropout length, clipping to active area if in visible mode
                uint32_t start = dropout.start_sample;
                uint32_t end = dropout.end_sample;
                
                if (cfg.mode == DropoutAnalysisMode::VISIBLE_AREA && video_params &&
                    video_params->active_video_start >= 0 && video_params->active_video_end >= 0) {
                    // Clip to active video range
                    start = std::max(start, static_cast<uint32_t>(video_params->active_video_start));
                    end = std::min(end, static_cast<uint32_t>(video_params->active_video_end));
                }
                
                field_dropout_length += (end - start);
                field_dropout_count++;
            }
        }

        int32_t frame_num = field_descriptor->frame_number.value_or(static_cast<int32_t>((fid.value() / 2) + 1));

        auto& accum = frame_accum[frame_num];
        accum.total_dropout_length += field_dropout_length;
        accum.dropout_count += static_cast<double>(field_dropout_count);
        if (field_dropout_count > 0) accum.has_data = true;

        if (progress_callback_) {
            progress_callback_(i + 1, total_fields, "Processing field " + std::to_string(i));
        }
    }

    if (frame_accum.empty()) {
        ORC_LOG_WARN("DropoutAnalysisSink: No frame data accumulated");
        return;
    }

    const size_t total_frames = frame_accum.size();
    total_frames_ = static_cast<int32_t>(total_frames);

    // Determine binning: aim for ~1000 data points maximum
    const size_t TARGET_DATA_POINTS = 1000;
    size_t frames_per_bin = 1;
    if (total_frames > TARGET_DATA_POINTS * 2) {
        frames_per_bin = (total_frames + TARGET_DATA_POINTS - 1) / TARGET_DATA_POINTS;
    }

    ORC_LOG_DEBUG("DropoutAnalysisSink: {} total frames, binning by {} frames per data point",
                  total_frames, frames_per_bin);

    FrameDropoutStats current_bin{};
    size_t frames_in_bin = 0;
    int32_t bin_start_frame = 0;

    for (const auto& [frame_number, accum] : frame_accum) {
        if (frames_in_bin == 0) {
            bin_start_frame = frame_number;
        }

        current_bin.frame_number = frame_number;
        current_bin.total_dropout_length += accum.total_dropout_length;
        current_bin.dropout_count += accum.dropout_count;
        current_bin.has_data = current_bin.has_data || accum.has_data;

        frames_in_bin++;

        if (frames_in_bin >= frames_per_bin) {
            ORC_LOG_DEBUG("DropoutAnalysisSink: Bucket {} - frames {}-{}: total_dropout_length={:.2f}, dropout_count={:.2f} ({} frames)",
                          frame_stats_.size(), bin_start_frame, frame_number,
                          current_bin.total_dropout_length, current_bin.dropout_count, frames_in_bin);

            frame_stats_.push_back(current_bin);

            current_bin = FrameDropoutStats();
            frames_in_bin = 0;
        }
    }

    // Output final partial bin if any frames were accumulated
    if (frames_in_bin > 0) {
        ORC_LOG_DEBUG("DropoutAnalysisSink: Final bucket {} - frames {}-{}: total_dropout_length={:.2f}, dropout_count={:.2f} ({} frames)",
                      frame_stats_.size(), bin_start_frame, current_bin.frame_number,
                      current_bin.total_dropout_length, current_bin.dropout_count, frames_in_bin);
        frame_stats_.push_back(current_bin);
    }

    ORC_LOG_DEBUG("DropoutAnalysisSink: Computed {} data buckets from {} total frames", frame_stats_.size(), total_frames);
}

bool DropoutAnalysisSinkStage::write_csv(const std::string& path) const {
    if (frame_stats_.empty()) {
        ORC_LOG_WARN("DropoutAnalysisSink: No data to write");
        return false;
    }

    ORC_LOG_DEBUG("DropoutAnalysisSink: Writing CSV to: {}", path);

    std::ofstream csv(path, std::ios::out | std::ios::trunc);
    if (!csv.is_open()) {
        ORC_LOG_ERROR("DropoutAnalysisSink: Failed to open file for writing: {}", path);
        return false;
    }

    csv << "frame_number,total_dropout_length_samples,total_dropout_count\n";
    size_t rows_written = 0;
    for (const auto& fs : frame_stats_) {
        // Only write frames that have data (matching what's shown in graphs)
        if (fs.has_data) {
            csv << fs.frame_number << ','
                << fs.total_dropout_length << ','
                << fs.dropout_count << '\n';
            rows_written++;
        }
    }

    ORC_LOG_DEBUG("DropoutAnalysisSink: Successfully wrote {} data rows to: {}", rows_written, path);
    return true;
}

} // namespace orc
