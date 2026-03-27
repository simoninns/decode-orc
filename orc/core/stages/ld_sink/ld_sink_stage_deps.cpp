/*
 * File:        ld_sink_stage_deps.cpp
 * Module:      orc-core
 * Purpose:     ld-decode Sink Stage dependencies implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "ld_sink_stage_deps.h"

#include "logging.h"
#include "observer.h"
#include "biphase_observer.h"
#include "closed_caption_observer.h"
#include "fm_code_observer.h"
#include "white_flag_observer.h"
#include "white_snr_observer.h"
#include "black_psnr_observer.h"
#include "burst_level_observer.h"
#include <algorithm>
#include <utility>

namespace orc
{
    void LDSinkStageDeps::init(TriggerProgressCallback progress_callback,
                               std::atomic<bool>* pIsProcessing,
                               std::atomic<bool>* pCancelRequested)
    {
        progress_callback_ = std::move(progress_callback);
        pIsProcessing_ = pIsProcessing;
        pCancelRequested_ = pCancelRequested;
    }

    bool LDSinkStageDeps::write_tbc_and_metadata(
        const VideoFieldRepresentation* representation,
        const std::string& tbc_path,
        IObservationContext& observation_context)
    {
        // Ensure the path has .tbc extension
        std::string final_tbc_path = tbc_path;
        const std::string tbc_ext = ".tbc";
        if (tbc_path.length() < tbc_ext.length() ||
            tbc_path.compare(tbc_path.length() - tbc_ext.length(), tbc_ext.length(), tbc_ext) != 0) {
            final_tbc_path += ".tbc";
            ORC_LOG_DEBUG("Added .tbc extension: {}", final_tbc_path);
        }

        std::string db_path = final_tbc_path + ".db";

        // Get field count early for progress reporting
        auto range = representation->field_range();
        size_t field_count = range.size();

        // Show initial progress
        if (progress_callback_) {
            progress_callback_(0, field_count, "Preparing export...");
        }

        try {
            ORC_LOG_DEBUG("Opening TBC file for writing: {}", final_tbc_path);
            ORC_LOG_DEBUG("Opening metadata database: {}", db_path);

            // Open TBC file with buffered writer (16MB buffer for large field writes)
            std::shared_ptr<IFileWriter<uint16_t>> tbc_writer =
                factories_.create_instance_buffered_file_writer_uint16(16 * 1024 * 1024);
            if (!tbc_writer->open(final_tbc_path)) {
                ORC_LOG_ERROR("Failed to open TBC file for writing: {}", final_tbc_path);
                return false;
            }

            // Open metadata database
            if (!metadata_writer_->open(db_path)) {
                ORC_LOG_ERROR("Failed to open metadata database for writing: {}", db_path);
                tbc_writer->close();
                return false;
            }

            // Get video parameters and write them
            auto video_params = representation->get_video_parameters();
            if (!video_params) {
                ORC_LOG_ERROR("No video parameters available");
                metadata_writer_->close();
                tbc_writer->close();
                return false;
            }
            video_params->decoder = "orc";
            if (!metadata_writer_->write_video_parameters(*video_params)) {
                ORC_LOG_ERROR("Failed to write video parameters");
                metadata_writer_->close();
                tbc_writer->close();
                return false;
            }

            // Build sorted list of field IDs
            std::vector<FieldID> field_ids;
            field_ids.reserve(field_count);
            for (FieldID field_id = range.start; field_id < range.end; field_id = field_id + 1) {
                if (representation->has_field(field_id)) {
                    field_ids.push_back(field_id);
                }
            }
            std::sort(field_ids.begin(), field_ids.end());

            ORC_LOG_DEBUG("Processing {} fields (TBC + metadata) in single pass", field_ids.size());

            // Create vector of observers
            // Note: VideoIdObserver and VitcObserver have been removed from the new architecture
            std::vector<std::shared_ptr<Observer>> observers;
            observers.push_back(std::make_shared<BiphaseObserver>());
            observers.push_back(std::make_shared<ClosedCaptionObserver>());
            observers.push_back(std::make_shared<FmCodeObserver>());
            observers.push_back(std::make_shared<WhiteFlagObserver>());
            observers.push_back(std::make_shared<WhiteSNRObserver>());
            observers.push_back(std::make_shared<BlackPSNRObserver>());
            observers.push_back(std::make_shared<BurstLevelObserver>());

            ORC_LOG_DEBUG("Instantiated {} observers for metadata extraction", observers.size());

            // Begin transaction for metadata writes
            metadata_writer_->begin_transaction();

            size_t fields_processed = 0;

            // Single pass: write TBC data, populate observations, and process metadata for each field
            for (FieldID field_id : field_ids) {
                // Check for cancellation
                if (pCancelRequested_->load()) {
                    metadata_writer_->commit_transaction();
                    metadata_writer_->close();
                    tbc_writer->close();
                    ORC_LOG_WARN("LDSink: Export cancelled by user");
                    pIsProcessing_->store(false);
                    return false;
                }

                // ===== Write TBC data =====
                auto descriptor = representation->get_descriptor(field_id);
                if (!descriptor) {
                    ORC_LOG_WARN("No descriptor for field {}, skipping", field_id.value());
                    continue;
                }

                size_t actual_lines = descriptor->height;  // VFR's standards-compliant height
                size_t line_width = descriptor->width;

                // Get field parity to determine if padding needed
                auto parity_hint = representation->get_field_parity_hint(field_id);
                bool is_first_field = parity_hint.has_value() && parity_hint->is_first_field;

                // Calculate padded height for TBC file format
                size_t padded_lines = calculate_padded_field_height(video_params->system);

                // Buffer for accumulating the entire field before writing
                std::vector<uint16_t> field_buffer;
                field_buffer.reserve(padded_lines * line_width);

                // Accumulate all lines from VFR
                for (size_t line_num = 0; line_num < actual_lines; ++line_num) {
                    const uint16_t* line_data = representation->get_line(field_id, line_num);
                    if (!line_data) {
                        ORC_LOG_WARN("Field {} line {} has no data", field_id.value(), line_num);
                        field_buffer.insert(field_buffer.end(), line_width, 0);
                    } else {
                        field_buffer.insert(field_buffer.end(), line_data, line_data + line_width);
                    }
                }

                // Add padding for first field if needed (TBC file format requirement)
                if (is_first_field && actual_lines < padded_lines) {
                    size_t padding_lines = padded_lines - actual_lines;
                    uint16_t blanking_level = static_cast<uint16_t>(video_params->blanking_16b_ire);

                    ORC_LOG_DEBUG("Adding {} padding lines to first field {} (blanking level {})",
                                  padding_lines, field_id.value(), blanking_level);

                    // Add blanking-level padding lines at end
                    for (size_t i = 0; i < padding_lines; ++i) {
                        field_buffer.insert(field_buffer.end(), line_width, blanking_level);
                    }
                }

                // Write the entire field to TBC (with padding if first field)
                tbc_writer->write(field_buffer);

                // ===== Write metadata =====
                // Create minimal field record
                FieldMetadata field_meta;
                field_meta.seq_no = static_cast<int32_t>(field_id.value() + 1);  // seq_no is 1-based

                // Use parity hint (already fetched above for padding logic)
                if (parity_hint.has_value()) {
                    field_meta.is_first_field = parity_hint->is_first_field;
                } else {
                    field_meta.is_first_field = false;
                }

                // Check for field phase HINT
                auto phase_hint = representation->get_field_phase_hint(field_id);
                if (phase_hint.has_value()) {
                    field_meta.field_phase_id = phase_hint->field_phase_id;
                }

                // Populate observation context with exported field information
                try {
                    observation_context.set(field_id, "export", "seq_no", static_cast<int64_t>(field_meta.seq_no));
                    // Parity may be absent; treat as optional observation
                    observation_context.set(field_id, "export", "is_first_field", static_cast<bool>(field_meta.is_first_field));
                } catch (const std::exception& e) {
                    ORC_LOG_WARN("LDSink: Failed to write observations for field {}: {}", field_id.value(), e.what());
                }

                metadata_writer_->write_field_metadata(field_meta);

                // ===== Run observers to populate observation context =====
                for (const auto& observer : observers) {
                    observer->process_field(*representation, field_id, observation_context);
                }

                // Write observations to metadata
                metadata_writer_->write_observations(field_id, observation_context);

                // Write dropout hints
                auto dropout_hints = representation->get_dropout_hints(field_id);
                for (const auto& hint : dropout_hints) {
                    DropoutInfo dropout;
                    dropout.line = hint.line;
                    dropout.start_sample = hint.start_sample;
                    dropout.end_sample = hint.end_sample;
                    metadata_writer_->write_dropout(field_id, dropout);
                }

                fields_processed++;

                // Update progress callback every 10 fields
                if (fields_processed % 10 == 0) {
                    if (progress_callback_) {
                        progress_callback_(fields_processed, field_count,
                                         "Exporting field " + std::to_string(fields_processed) + "/" + std::to_string(field_count));
                    }
                }

                // Log progress every 50 fields
                if (fields_processed % 50 == 0) {
                    ORC_LOG_DEBUG("Exported {}/{} fields ({:.1f}%)", fields_processed, field_count,
                                (fields_processed * 100.0) / field_count);
                }
            }

            // Commit metadata transaction and close files
            metadata_writer_->commit_transaction();
            metadata_writer_->close();
            tbc_writer->close();

            ORC_LOG_DEBUG("Successfully exported {} fields", fields_processed);
            return true;

        } catch (const std::exception& e) {
            ORC_LOG_ERROR("Exception during export: {}", e.what());
            return false;
        }
    }
} // namespace orc
