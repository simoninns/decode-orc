/*
 * File:        burst_level_analysis_sink_deps.cpp
 * Module:      orc-core
 * Purpose:     BurstLevelAnalysisSinkStage dependency implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "burst_level_analysis_sink_deps.h"

#include "burst_level_observer.h"
#include "logging.h"

#include <cmath>
#include <fstream>
#include <utility>

namespace orc
{
    void BurstLevelAnalysisSinkStageDeps::init(
        TriggerProgressCallback progress_callback,
        std::atomic<bool>* cancel_requested)
    {
        progress_callback_ = std::move(progress_callback);
        cancel_requested_ = cancel_requested;
    }

    BurstAnalysisComputeResult BurstLevelAnalysisSinkStageDeps::compute_and_analyze(
        VideoFieldRepresentation* representation,
        IObservationContext& observation_context,
        BurstAnalysisComputeOptions options)
    {
        if (!representation) {
            return {false, "Input representation is null", {}, 0};
        }

        (void)options.max_frames;

        BurstAnalysisComputeResult result;
        result.success = true;
        result.message = "Burst level analysis complete";

        auto range = representation->field_range();
        if (range.size() == 0) {
            logger_.warn("BurstLevelAnalysisSinkDeps: No fields available");
            result.total_frames = 0;
            return result;
        }

        size_t total_fields = range.size();

        const size_t TARGET_DATA_POINTS = 1000;
        size_t fields_per_bin = 2;
        if (total_fields > TARGET_DATA_POINTS * 2) {
            fields_per_bin = (total_fields + TARGET_DATA_POINTS - 1) / TARGET_DATA_POINTS;
            if (fields_per_bin % 2 != 0) {
                fields_per_bin++;
            }
        }

        logger_.debug(
            "BurstLevelAnalysisSinkDeps: {} total fields, binning by {} fields per data point",
            total_fields,
            fields_per_bin);

        BurstLevelObserver burst_observer;

        FrameBurstLevelStats current_bin{};
        size_t fields_in_bin = 0;
        [[maybe_unused]] size_t first_field_in_bin = 0;
        size_t last_field_in_bin = 0;

        for (size_t i = 0; i < total_fields; ++i) {
            if (cancel_requested_ && cancel_requested_->load()) {
                logger_.warn("BurstLevelAnalysisSinkDeps: Cancel requested at field {}", i);
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

            burst_observer.process_field(*representation, fid, observation_context);

            try {
                auto burst_level_opt = observation_context.get(fid, "burst_level", "median_burst_ire");
                if (burst_level_opt) {
                    try {
                        double field_burst = std::get<double>(*burst_level_opt);
                        current_bin.median_burst_ire += field_burst;
                        current_bin.has_data = true;
                        logger_.trace(
                            "BurstLevelAnalysisSinkDeps: Read burst_level for field {} = {:.2f} IRE",
                            fid.value(),
                            field_burst);
                    } catch (const std::exception& e) {
                        logger_.warn("BurstLevelAnalysisSinkDeps: Failed to extract burst_level value: {}", e.what());
                    }
                }
            } catch (const std::exception& e) {
                logger_.warn(
                    "BurstLevelAnalysisSinkDeps: Exception reading observations for field {}: {}",
                    fid.value(),
                    e.what());
            }

            current_bin.field_count++;
            fields_in_bin++;

            if (fields_in_bin >= fields_per_bin) {
                if (current_bin.field_count > 0 && current_bin.has_data) {
                    current_bin.median_burst_ire /= static_cast<double>(current_bin.field_count);
                    FieldID last_fid(range.start.value() + last_field_in_bin);
                    int32_t bin_frame_number = static_cast<int32_t>((last_fid.value() / 2) + 1);
                    current_bin.frame_number = bin_frame_number;
                    logger_.info(
                        "BurstLevelAnalysisSinkDeps: Bucket {} - field indices {}-{}, field IDs {}-{}, frame {}: median_burst_ire={:.2f} IRE ({} fields)",
                        result.frame_stats.size(),
                        first_field_in_bin,
                        last_field_in_bin,
                        range.start.value() + first_field_in_bin,
                        range.start.value() + last_field_in_bin,
                        bin_frame_number,
                        current_bin.median_burst_ire,
                        current_bin.field_count);
                    result.frame_stats.push_back(current_bin);
                }

                current_bin = FrameBurstLevelStats();
                fields_in_bin = 0;
            }

            if (progress_callback_) {
                progress_callback_(i + 1, total_fields, "Processing field " + std::to_string(i));
            }
        }

        if (fields_in_bin > 0 && current_bin.field_count > 0 && current_bin.has_data) {
            current_bin.median_burst_ire /= static_cast<double>(current_bin.field_count);
            FieldID last_fid(range.start.value() + last_field_in_bin);
            int32_t bin_frame_number = static_cast<int32_t>((last_fid.value() / 2) + 1);
            current_bin.frame_number = bin_frame_number;
            logger_.info(
                "BurstLevelAnalysisSinkDeps: Final bucket {} - field indices {}-{}, field IDs {}-{}, frame {}: median_burst_ire={:.2f} IRE ({} fields)",
                result.frame_stats.size(),
                first_field_in_bin,
                last_field_in_bin,
                range.start.value() + first_field_in_bin,
                range.start.value() + last_field_in_bin,
                bin_frame_number,
                current_bin.median_burst_ire,
                current_bin.field_count);
            result.frame_stats.push_back(current_bin);
        }

        result.total_frames = static_cast<int32_t>((total_fields + 1) / 2);

        logger_.debug(
            "BurstLevelAnalysisSinkDeps: Computed {} data buckets from {} total fields ({} total frames)",
            result.frame_stats.size(),
            total_fields,
            result.total_frames);

        return result;
    }

    bool BurstLevelAnalysisSinkStageDeps::write_csv(
        const std::string& path,
        const std::vector<FrameBurstLevelStats>& frame_stats)
    {
        if (frame_stats.empty()) {
            logger_.warn("BurstLevelAnalysisSinkDeps: No data to write");
            return false;
        }

        logger_.debug("BurstLevelAnalysisSinkDeps: Writing CSV to: {}", path);

        std::ofstream csv(path, std::ios::out | std::ios::trunc);
        if (!csv.is_open()) {
            logger_.error("BurstLevelAnalysisSinkDeps: Failed to open file for writing: {}", path);
            return false;
        }

        csv << "frame_number,median_burst_ire\n";
        size_t rows_written = 0;
        for (const auto& fs : frame_stats) {
            csv << fs.frame_number << ',' << (fs.has_data ? fs.median_burst_ire : std::nan("")) << '\n';
            rows_written++;
        }

        logger_.debug("BurstLevelAnalysisSinkDeps: Successfully wrote {} data rows to: {}", rows_written, path);
        return true;
    }
} // namespace orc
