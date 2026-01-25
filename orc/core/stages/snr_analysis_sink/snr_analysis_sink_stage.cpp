/*
 * File:        snr_analysis_sink_stage.cpp
 * Module:      orc-core
 * Purpose:     SNR Analysis Sink Stage implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "snr_analysis_sink_stage.h"
#include "logging.h"
#include "stage_registry.h"
#include "white_snr_observer.h"
#include "black_psnr_observer.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <numeric>
#include <stdexcept>

namespace orc {

// Register stage with registry
ORC_REGISTER_STAGE(SNRAnalysisSinkStage)

// Force linker to include this object file
void force_link_SNRAnalysisSinkStage() {}

SNRAnalysisSinkStage::SNRAnalysisSinkStage() = default;

NodeTypeInfo SNRAnalysisSinkStage::get_node_type_info() const {
    return NodeTypeInfo{
        NodeType::ANALYSIS_SINK,
        "snr_analysis_sink",
        "SNR Analysis Sink",
        "Computes SNR/PSNR statistics and optionally writes CSV. Trigger to update dataset.",
        1,  // min_inputs
        1,  // max_inputs
        0,  // min_outputs
        0,  // max_outputs
        VideoFormatCompatibility::ALL
    };
}

std::vector<ArtifactPtr> SNRAnalysisSinkStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    ObservationContext& observation_context) {
    (void)inputs;
    (void)parameters;
    (void)observation_context;
    return {};
}

std::vector<ParameterDescriptor> SNRAnalysisSinkStage::get_parameter_descriptors(
    VideoSystem project_format,
    SourceType source_type) const {
    (void)project_format;
    (void)source_type;

    std::vector<ParameterDescriptor> descriptors;

    descriptors.push_back(ParameterDescriptor{
        "output_path",
        "CSV Output Path",
        "Destination CSV file for SNR metrics. Leave empty to skip file output.",
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
        "Select white, black, or both SNR metrics.",
        ParameterType::STRING,
        ParameterConstraints{std::nullopt, std::nullopt, std::string("both"), {"white", "black", "both"}, true, std::nullopt}
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

std::map<std::string, ParameterValue> SNRAnalysisSinkStage::get_parameters() const {
    return parameters_;
}

bool SNRAnalysisSinkStage::set_parameters(const std::map<std::string, ParameterValue>& params) {
    parameters_ = params;
    return true;
}

SNRAnalysisSinkStage::ParsedConfig SNRAnalysisSinkStage::parse_config(
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

    auto mode_it = parameters.find("mode");
    if (mode_it != parameters.end() && std::holds_alternative<std::string>(mode_it->second)) {
        auto m = std::get<std::string>(mode_it->second);
        if (m == "white") cfg.mode = SNRAnalysisMode::WHITE;
        else if (m == "black") cfg.mode = SNRAnalysisMode::BLACK;
        else cfg.mode = SNRAnalysisMode::BOTH;
    }

    auto max_it = parameters.find("max_frames");
    if (max_it != parameters.end() && std::holds_alternative<uint32_t>(max_it->second)) {
        cfg.max_frames = static_cast<size_t>(std::get<uint32_t>(max_it->second));
    }

    return cfg;
}

bool SNRAnalysisSinkStage::trigger(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    ObservationContext& observation_context) {

    ORC_LOG_DEBUG("SNRAnalysisSink: Trigger started");
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
                ORC_LOG_WARN("SNRAnalysisSink: Failed to write CSV to {}", cfg.output_path);
            }
        }

        last_status_ = "SNR analysis complete";
        has_results_ = true;
        is_processing_.store(false);
        return true;

    } catch (const std::exception& e) {
        last_status_ = std::string("Error: ") + e.what();
        ORC_LOG_ERROR("SNRAnalysisSink: Trigger failed: {}", e.what());
        is_processing_.store(false);
        return false;
    }
}

void SNRAnalysisSinkStage::compute_stats(const VideoFieldRepresentation& vfr, const ParsedConfig& cfg, const ObservationContext& observation_context) {
    (void)cfg;  // Reserved for future configuration options
    frame_stats_.clear();
    total_frames_ = 0;

    auto range = vfr.field_range();
    if (range.size() == 0) {
        ORC_LOG_WARN("SNRAnalysisSink: No fields available");
        return;
    }

    size_t total_fields = range.size();
    
    // Determine binning: aim for ~1000 data points maximum
    const size_t TARGET_DATA_POINTS = 1000;
    size_t fields_per_bin = 1;
    if (total_fields > TARGET_DATA_POINTS * 2) {
        fields_per_bin = (total_fields + TARGET_DATA_POINTS - 1) / TARGET_DATA_POINTS;
    }
    
    ORC_LOG_DEBUG("SNRAnalysisSink: {} total fields, binning by {} fields per data point", 
                  total_fields, fields_per_bin);

    // Create mutable copy of observation context to populate observations
    ObservationContext mutable_context = observation_context;
    
    // Run observers on each field to populate SNR observations
    WhiteSNRObserver white_snr_observer;
    BlackPSNRObserver black_psnr_observer;
    
    FrameSNRStats current_bin;
    size_t fields_in_bin = 0;
    int32_t output_frame_number = 1;  // Sequential output frame counter for graph X-axis

    for (size_t i = 0; i < total_fields; ++i) {
        if (cancel_requested_.load()) {
            ORC_LOG_WARN("SNRAnalysisSink: Cancel requested at field {}", i);
            break;
        }

        FieldID fid(range.start.value() + i);
        auto descriptor = vfr.get_descriptor(fid);
        if (!descriptor) {
            continue;
        }

        // Run observers on this field to populate observations
        white_snr_observer.process_field(vfr, fid, mutable_context);
        black_psnr_observer.process_field(vfr, fid, mutable_context);

        try {
            // Read SNR values from observation context
            auto white_snr_opt = mutable_context.get(fid, "white_snr", "snr_db");
            if (white_snr_opt) {
                try {
                    double val = std::get<double>(*white_snr_opt);
                    current_bin.white_snr += val;
                    current_bin.has_white_snr = true;
                } catch (const std::exception& e) {
                    ORC_LOG_TRACE("SNRAnalysisSink: Failed to extract white_snr: {}", e.what());
                }
            }

            auto black_psnr_opt = mutable_context.get(fid, "black_psnr", "psnr_db");
            if (black_psnr_opt) {
                try {
                    double val = std::get<double>(*black_psnr_opt);
                    current_bin.black_psnr += val;
                    current_bin.has_black_psnr = true;
                } catch (const std::exception& e) {
                    ORC_LOG_TRACE("SNRAnalysisSink: Failed to extract black_psnr: {}", e.what());
                }
            }
        } catch (const std::exception& e) {
            ORC_LOG_TRACE("SNRAnalysisSink: Exception reading observations for field {}: {}", fid.value(), e.what());
        }

        fields_in_bin++;

        // When bin is full, output it and reset
        if (fields_in_bin >= fields_per_bin) {
            if (current_bin.has_white_snr || current_bin.has_black_psnr) {
                if (current_bin.has_white_snr) current_bin.white_snr /= fields_in_bin;
                if (current_bin.has_black_psnr) current_bin.black_psnr /= fields_in_bin;
                current_bin.frame_number = output_frame_number;
                current_bin.has_data = true;
                ORC_LOG_DEBUG("SNRAnalysisSink: Bucket {} - output_frame {}: white_snr={:.2f}dB, black_psnr={:.2f}dB ({} fields)",
                              frame_stats_.size(), output_frame_number,
                              current_bin.has_white_snr ? current_bin.white_snr : 0.0,
                              current_bin.has_black_psnr ? current_bin.black_psnr : 0.0,
                              fields_in_bin);
                frame_stats_.push_back(current_bin);
                output_frame_number++;  // Increment for next data point
            }
            
            current_bin = FrameSNRStats();
            fields_in_bin = 0;
        }

        if (progress_callback_) {
            progress_callback_(i + 1, total_fields, "Processing field " + std::to_string(i));
        }
    }

    // Output final partial bin if it has data
    if (fields_in_bin > 0 && (current_bin.has_white_snr || current_bin.has_black_psnr)) {
        if (current_bin.has_white_snr) current_bin.white_snr /= fields_in_bin;
        if (current_bin.has_black_psnr) current_bin.black_psnr /= fields_in_bin;
        current_bin.frame_number = output_frame_number;
        current_bin.has_data = true;
        ORC_LOG_DEBUG("SNRAnalysisSink: Final bucket {} - output_frame {}: white_snr={:.2f}dB, black_psnr={:.2f}dB ({} fields)",
                      frame_stats_.size(), output_frame_number,
                      current_bin.has_white_snr ? current_bin.white_snr : 0.0,
                      current_bin.has_black_psnr ? current_bin.black_psnr : 0.0,
                      fields_in_bin);
        frame_stats_.push_back(current_bin);
    }

    // Set total_frames to the count of data points
    total_frames_ = static_cast<int32_t>(frame_stats_.size());
    ORC_LOG_DEBUG("SNRAnalysisSink: Computed {} data buckets from {} total fields", total_frames_, total_fields);
}

bool SNRAnalysisSinkStage::write_csv(const std::string& path) const {
    if (frame_stats_.empty()) {
        ORC_LOG_WARN("SNRAnalysisSink: No data to write");
        return false;
    }

    ORC_LOG_DEBUG("SNRAnalysisSink: Writing CSV to: {}", path);

    std::ofstream csv(path, std::ios::out | std::ios::trunc);
    if (!csv.is_open()) {
        ORC_LOG_ERROR("SNRAnalysisSink: Failed to open file for writing: {}", path);
        return false;
    }

    csv << "frame_number,white_snr_db,black_psnr_db\n";
    size_t rows_written = 0;
    for (const auto& fs : frame_stats_) {
        // Only write frames that have data (matching what's shown in graphs)
        if (fs.has_data) {
            csv << fs.frame_number << ','
                << (fs.has_white_snr ? fs.white_snr : std::nan("")) << ','
                << (fs.has_black_psnr ? fs.black_psnr : std::nan("")) << '\n';
            rows_written++;
        }
    }

    ORC_LOG_DEBUG("SNRAnalysisSink: Successfully wrote {} data rows to: {}", rows_written, path);
    return true;
}

} // namespace orc
