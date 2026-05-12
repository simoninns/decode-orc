/*
 * File:        snr_analysis_sink_deps.cpp
 * Module:      orc-core
 * Purpose:     SNRAnalysisSinkStage dependency implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "snr_analysis_sink_deps.h"

#include <cmath>
#include <fstream>
#include <utility>

namespace orc
{
    void SNRAnalysisSinkStageDeps::init(
        TriggerProgressCallback progress_callback,
        std::atomic<bool>* cancel_requested)
    {
        progress_callback_ = std::move(progress_callback);
        cancel_requested_ = cancel_requested;
    }

    SNRAnalysisComputeResult SNRAnalysisSinkStageDeps::compute_and_analyze(
        VideoFieldRepresentation* representation,
        IObservationContext& observation_context,
        SNRAnalysisComputeOptions options)
    {
        if (!representation) {
            return {false, "Input representation is null", {}, 0};
        }

        (void)options.max_frames;
        (void)options.output_path;
        (void)options.write_csv;

        SNRAnalysisComputeResult result;
        result.success = true;
        result.message = "SNR analysis complete";

        auto range = representation->field_range();
        if (range.size() == 0) {
            logger_.warn("SNRAnalysisSinkDeps: No fields available");
            result.total_frames = 0;
            return result;
        }

        const size_t total_fields = range.size();

        const size_t TARGET_DATA_POINTS = 1000;
        size_t fields_per_bin = 2;
        if (total_fields > TARGET_DATA_POINTS * 2) {
            fields_per_bin = (total_fields + TARGET_DATA_POINTS - 1) / TARGET_DATA_POINTS;
            if (fields_per_bin % 2 != 0) {
                fields_per_bin++;
            }
        }

        logger_.debug(
            "SNRAnalysisSinkDeps: {} total fields, binning by {} fields per data point",
            total_fields,
            fields_per_bin);

        FrameSNRStats current_bin{};
        size_t fields_in_bin = 0;
        [[maybe_unused]] size_t first_field_in_bin = 0;
        size_t last_field_in_bin = 0;

        const bool include_white = options.snr_mode == SNRAnalysisMode::WHITE || options.snr_mode == SNRAnalysisMode::BOTH;
        const bool include_black = options.snr_mode == SNRAnalysisMode::BLACK || options.snr_mode == SNRAnalysisMode::BOTH;

        for (size_t i = 0; i < total_fields; ++i) {
            if (cancel_requested_ && cancel_requested_->load()) {
                logger_.warn("SNRAnalysisSinkDeps: Cancel requested at field {}", i);
                result.success = false;
                result.message = "Cancelled by user";
                result.frame_stats.clear();
                result.total_frames = 0;
                return result;
            }

            FieldID fid(range.start.value() + i);
            auto descriptor = representation->get_descriptor(fid);
            if (!descriptor) {
                continue;
            }

            if (fields_in_bin == 0) {
                first_field_in_bin = i;
            }
            last_field_in_bin = i;

            if (include_white) {
                white_snr_observer_.process_field(*representation, fid, observation_context);
            }
            if (include_black) {
                black_psnr_observer_.process_field(*representation, fid, observation_context);
            }

            try {
                if (include_white) {
                    auto white_snr_opt = observation_context.get(fid, "white_snr", "snr_db");
                    if (white_snr_opt) {
                        try {
                            double val = std::get<double>(*white_snr_opt);
                            current_bin.white_snr += val;
                            current_bin.has_white_snr = true;
                            current_bin.white_snr_count++;
                        } catch (const std::exception& e) {
                            logger_.trace("SNRAnalysisSinkDeps: Failed to extract white_snr: {}", e.what());
                        }
                    }
                }

                if (include_black) {
                    auto black_psnr_opt = observation_context.get(fid, "black_psnr", "psnr_db");
                    if (black_psnr_opt) {
                        try {
                            double val = std::get<double>(*black_psnr_opt);
                            current_bin.black_psnr += val;
                            current_bin.has_black_psnr = true;
                            current_bin.black_psnr_count++;
                        } catch (const std::exception& e) {
                            logger_.trace("SNRAnalysisSinkDeps: Failed to extract black_psnr: {}", e.what());
                        }
                    }
                }
            } catch (const std::exception& e) {
                logger_.trace(
                    "SNRAnalysisSinkDeps: Exception reading observations for field {}: {}",
                    fid.value(),
                    e.what());
            }

            fields_in_bin++;

            if (fields_in_bin >= fields_per_bin) {
                if (current_bin.has_white_snr || current_bin.has_black_psnr) {
                    if (current_bin.has_white_snr && current_bin.white_snr_count > 0) {
                        current_bin.white_snr /= current_bin.white_snr_count;
                    }
                    if (current_bin.has_black_psnr && current_bin.black_psnr_count > 0) {
                        current_bin.black_psnr /= current_bin.black_psnr_count;
                    }

                    FieldID last_fid(range.start.value() + last_field_in_bin);
                    int32_t bin_frame_number = static_cast<int32_t>((last_fid.value() / 2) + 1);
                    current_bin.frame_number = bin_frame_number;
                    current_bin.has_data = true;
                    logger_.debug(
                        "SNRAnalysisSinkDeps: Bucket {} - field indices {}-{}, field IDs {}-{}, frame {}: white_snr={:.2f}dB ({} fields), black_psnr={:.2f}dB ({} fields)",
                        result.frame_stats.size(),
                        first_field_in_bin,
                        last_field_in_bin,
                        range.start.value() + first_field_in_bin,
                        range.start.value() + last_field_in_bin,
                        bin_frame_number,
                        current_bin.has_white_snr ? current_bin.white_snr : 0.0,
                        current_bin.white_snr_count,
                        current_bin.has_black_psnr ? current_bin.black_psnr : 0.0,
                        current_bin.black_psnr_count);
                    result.frame_stats.push_back(current_bin);
                }

                current_bin = FrameSNRStats();
                fields_in_bin = 0;
            }

            if (progress_callback_) {
                progress_callback_(i + 1, total_fields, "Processing field " + std::to_string(i));
            }
        }

        if (fields_in_bin > 0 && (current_bin.has_white_snr || current_bin.has_black_psnr)) {
            if (current_bin.has_white_snr && current_bin.white_snr_count > 0) {
                current_bin.white_snr /= current_bin.white_snr_count;
            }
            if (current_bin.has_black_psnr && current_bin.black_psnr_count > 0) {
                current_bin.black_psnr /= current_bin.black_psnr_count;
            }

            FieldID last_fid(range.start.value() + last_field_in_bin);
            int32_t bin_frame_number = static_cast<int32_t>((last_fid.value() / 2) + 1);
            current_bin.frame_number = bin_frame_number;
            current_bin.has_data = true;
            logger_.debug(
                "SNRAnalysisSinkDeps: Final bucket {} - field indices {}-{}, field IDs {}-{}, frame {}: white_snr={:.2f}dB ({} fields), black_psnr={:.2f}dB ({} fields)",
                result.frame_stats.size(),
                first_field_in_bin,
                last_field_in_bin,
                range.start.value() + first_field_in_bin,
                range.start.value() + last_field_in_bin,
                bin_frame_number,
                current_bin.has_white_snr ? current_bin.white_snr : 0.0,
                current_bin.white_snr_count,
                current_bin.has_black_psnr ? current_bin.black_psnr : 0.0,
                current_bin.black_psnr_count);
            result.frame_stats.push_back(current_bin);
        }

        result.total_frames = static_cast<int32_t>((total_fields + 1) / 2);

        logger_.debug(
            "SNRAnalysisSinkDeps: Computed {} data buckets from {} total fields ({} total frames)",
            result.frame_stats.size(),
            total_fields,
            result.total_frames);

        return result;
    }

    bool SNRAnalysisSinkStageDeps::write_csv(
        const std::string& path,
        const std::vector<FrameSNRStats>& frame_stats)
    {
        if (frame_stats.empty()) {
            logger_.warn("SNRAnalysisSinkDeps: No data to write");
            return false;
        }

        logger_.debug("SNRAnalysisSinkDeps: Writing CSV to: {}", path);

        std::ofstream csv(path, std::ios::out | std::ios::trunc);
        if (!csv.is_open()) {
            logger_.error("SNRAnalysisSinkDeps: Failed to open file for writing: {}", path);
            return false;
        }

        csv << "frame_number,white_snr_db,black_psnr_db\n";
        size_t rows_written = 0;
        for (const auto& fs : frame_stats) {
            if (fs.has_data) {
                csv << fs.frame_number << ','
                    << (fs.has_white_snr ? fs.white_snr : std::nan("")) << ','
                    << (fs.has_black_psnr ? fs.black_psnr : std::nan("")) << '\n';
                rows_written++;
            }
        }

        logger_.debug("SNRAnalysisSinkDeps: Successfully wrote {} data rows to: {}", rows_written, path);
        return true;
    }
} // namespace orc
