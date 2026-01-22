/*
 * File:        burst_level_analysis_sink_stage.cpp
 * Module:      orc-core
 * Purpose:     Burst Level Analysis Sink Stage implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "burst_level_analysis_sink_stage.h"
#include "logging.h"
#include "stage_registry.h"
#include "burst_level_observer.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <numeric>
#include <stdexcept>

namespace orc {

// Register stage with registry
ORC_REGISTER_STAGE(BurstLevelAnalysisSinkStage)

// Force linker to include this object file
void force_link_BurstLevelAnalysisSinkStage() {}

BurstLevelAnalysisSinkStage::BurstLevelAnalysisSinkStage() = default;

NodeTypeInfo BurstLevelAnalysisSinkStage::get_node_type_info() const {
    return NodeTypeInfo{
        NodeType::ANALYSIS_SINK,
        "burst_level_analysis_sink",
        "Burst Level Analysis Sink",
        "Computes burst level statistics and optionally writes CSV. Trigger to update dataset.",
        1,
        1,
        0,
        0,
        VideoFormatCompatibility::ALL
    };
}

std::vector<ArtifactPtr> BurstLevelAnalysisSinkStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    ObservationContext& observation_context) {
    (void)inputs;
    (void)parameters;
    (void)observation_context;
    return {};
}

std::vector<ParameterDescriptor> BurstLevelAnalysisSinkStage::get_parameter_descriptors(
    VideoSystem project_format,
    SourceType source_type) const {
    (void)project_format;
    (void)source_type;

    std::vector<ParameterDescriptor> descriptors;

    descriptors.push_back(ParameterDescriptor{
        "output_path",
        "CSV Output Path",
        "Destination CSV file for burst metrics. Leave empty to skip file output.",
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
        "max_frames",
        "Max Frames",
        "Deprecated: data is automatically binned to ~1000 points based on total fields (0 = auto).",
        ParameterType::UINT32,
        ParameterConstraints{ParameterValue(0U), std::nullopt, ParameterValue(0U), {}, false, std::nullopt}
    });

    return descriptors;
}

std::map<std::string, ParameterValue> BurstLevelAnalysisSinkStage::get_parameters() const {
    return parameters_;
}

bool BurstLevelAnalysisSinkStage::set_parameters(const std::map<std::string, ParameterValue>& params) {
    parameters_ = params;
    return true;
}

BurstLevelAnalysisSinkStage::ParsedConfig BurstLevelAnalysisSinkStage::parse_config(
    const std::map<std::string, ParameterValue>& parameters) const {
    ParsedConfig cfg;

    auto out_it = parameters.find("output_path");
    if (out_it != parameters.end() && std::holds_alternative<std::string>(out_it->second)) {
        cfg.output_path = std::get<std::string>(out_it->second);
    }

    auto csv_it = parameters.find("write_csv");
    if (csv_it != parameters.end() && std::holds_alternative<bool>(csv_it->second)) {
        cfg.write_csv = std::get<bool>(csv_it->second);
    }

    auto max_it = parameters.find("max_frames");
    if (max_it != parameters.end() && std::holds_alternative<uint32_t>(max_it->second)) {
        cfg.max_frames = static_cast<size_t>(std::get<uint32_t>(max_it->second));
    }

    return cfg;
}

bool BurstLevelAnalysisSinkStage::trigger(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    ObservationContext& observation_context) {

    ORC_LOG_DEBUG("BurstLevelAnalysisSink: Trigger started");
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
        compute_stats(*vfr, cfg, observation_context);

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
                ORC_LOG_WARN("BurstLevelAnalysisSink: Failed to write CSV to {}", cfg.output_path);
            }
        }

        last_status_ = "Burst level analysis complete";
        has_results_ = true;
        is_processing_.store(false);
        return true;

    } catch (const std::exception& e) {
        last_status_ = std::string("Error: ") + e.what();
        ORC_LOG_ERROR("BurstLevelAnalysisSink: Trigger failed: {}", e.what());
        is_processing_.store(false);
        return false;
    }
}

void BurstLevelAnalysisSinkStage::compute_stats(const VideoFieldRepresentation& vfr, const ParsedConfig& cfg, const ObservationContext& observation_context) {
    (void)cfg;  // Reserved for future configuration options
    frame_stats_.clear();
    total_frames_ = 0;

    auto range = vfr.field_range();
    if (range.size() == 0) {
        ORC_LOG_WARN("BurstLevelAnalysisSink: No fields available");
        return;
    }

    size_t total_fields = range.size();

    // Determine binning: aim for ~1000 data points maximum
    const size_t TARGET_DATA_POINTS = 1000;
    size_t fields_per_bin = 1;
    if (total_fields > TARGET_DATA_POINTS * 2) {
        fields_per_bin = (total_fields + TARGET_DATA_POINTS - 1) / TARGET_DATA_POINTS;
    }

    ORC_LOG_DEBUG("BurstLevelAnalysisSink: {} total fields, binning by {} fields per data point",
                  total_fields, fields_per_bin);

    // Create mutable copy of observation context to populate observations
    ObservationContext mutable_context = observation_context;
    
    // Run observer on each field to populate burst level observations
    BurstLevelObserver burst_observer;

    FrameBurstLevelStats current_bin{};
    size_t fields_in_bin = 0;
    int32_t current_frame = 1;

    for (size_t i = 0; i < total_fields; ++i) {
        if (cancel_requested_.load()) {
            ORC_LOG_WARN("BurstLevelAnalysisSink: Cancel requested at field {}", i);
            break;
        }

        FieldID fid(range.start.value() + i);
        auto descriptor = vfr.get_descriptor(fid);
        if (!descriptor) {
            continue;
        }

        // Run observer on this field to populate observations
        burst_observer.process_field(vfr, fid, mutable_context);

        try {
            auto burst_level_opt = mutable_context.get(fid, "burst_level", "median_burst_ire");
            if (burst_level_opt) {
                try {
                    double field_burst = std::get<double>(*burst_level_opt);
                    current_bin.median_burst_ire += field_burst;
                    current_bin.has_data = true;
                    ORC_LOG_TRACE("BurstLevelAnalysisSink: Read burst_level for field {} = {:.2f} IRE", fid.value(), field_burst);
                } catch (const std::exception& e) {
                    ORC_LOG_WARN("BurstLevelAnalysisSink: Failed to extract burst_level value: {}", e.what());
                }
            }
        } catch (const std::exception& e) {
            ORC_LOG_WARN("BurstLevelAnalysisSink: Exception reading observations for field {}: {}", fid.value(), e.what());
        }

        int32_t frame_num = descriptor->frame_number.value_or(static_cast<int32_t>((fid.value() / 2) + 1));

        current_bin.field_count++;
        current_frame = frame_num;
        fields_in_bin++;

        if (fields_in_bin >= fields_per_bin) {
            if (current_bin.field_count > 0 && current_bin.has_data) {
                current_bin.median_burst_ire /= static_cast<double>(current_bin.field_count);
            }

            current_bin.frame_number = current_frame;
            ORC_LOG_DEBUG("BurstLevelAnalysisSink: Bucket {} - frame {}: median_burst_ire={:.2f} IRE ({} fields)",
                          frame_stats_.size(), current_frame,
                          current_bin.has_data ? current_bin.median_burst_ire : 0.0,
                          current_bin.field_count);
            frame_stats_.push_back(current_bin);

            current_bin = FrameBurstLevelStats();
            fields_in_bin = 0;
        }

        if (progress_callback_) {
            progress_callback_(i + 1, total_fields, "Processing field " + std::to_string(i));
        }
    }

    // Output final partial bin if any fields were accumulated
    if (fields_in_bin > 0) {
        if (current_bin.field_count > 0 && current_bin.has_data) {
            current_bin.median_burst_ire /= static_cast<double>(current_bin.field_count);
        }
        current_bin.frame_number = current_frame;
        ORC_LOG_DEBUG("BurstLevelAnalysisSink: Final bucket {} - frame {}: median_burst_ire={:.2f} IRE ({} fields)",
                      frame_stats_.size(), current_frame,
                      current_bin.has_data ? current_bin.median_burst_ire : 0.0,
                      current_bin.field_count);
        frame_stats_.push_back(current_bin);
    }

    total_frames_ = static_cast<int32_t>(frame_stats_.size());
    ORC_LOG_DEBUG("BurstLevelAnalysisSink: Computed {} data buckets from {} total fields", total_frames_, total_fields);
}

bool BurstLevelAnalysisSinkStage::write_csv(const std::string& path) const {
    if (frame_stats_.empty()) {
        ORC_LOG_WARN("BurstLevelAnalysisSink: No data to write");
        return false;
    }

    ORC_LOG_DEBUG("BurstLevelAnalysisSink: Writing CSV to: {}", path);

    std::ofstream csv(path, std::ios::out | std::ios::trunc);
    if (!csv.is_open()) {
        ORC_LOG_ERROR("BurstLevelAnalysisSink: Failed to open file for writing: {}", path);
        return false;
    }

    csv << "frame_number,median_burst_ire\n";
    size_t rows_written = 0;
    for (const auto& fs : frame_stats_) {
        csv << fs.frame_number << ','
            << (fs.has_data ? fs.median_burst_ire : std::nan("")) << '\n';
        rows_written++;
    }

    ORC_LOG_DEBUG("BurstLevelAnalysisSink: Successfully wrote {} data rows to: {}", rows_written, path);
    return true;
}

} // namespace orc
