/*
 * File:        daphne_vbi_sink_stage_deps.cpp
 * Module:      orc-core
 * Purpose:     Generate .VBI binary files, dependencies
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Matt Ownby
 */

#include "daphne_vbi_sink_stage_deps.h"

#include <orc/plugin/orc_stage_services.h>

#include <algorithm>  // for sort
#include <cstddef>
#include <utility>

#include "biphase_observer.h"
#include "daphne_vbi_writer_util.h"
#include "file_io_interface.h"
#include "logging.h"
#include "observer.h"
#include "white_flag_observer.h"

namespace orc {
void DaphneVBISinkStageDeps::init(TriggerProgressCallback progress_callback,
                                  std::atomic<bool>* pIsProcessing,
                                  std::atomic<bool>* pCancelRequested) {
  progress_callback_ = std::move(progress_callback);
  pIsProcessing_ = pIsProcessing;
  pCancelRequested_ = pCancelRequested;
}

bool DaphneVBISinkStageDeps::write_vbi(
    const VideoFieldRepresentation* representation, const std::string& vbi_path,
    IObservationContext& observation_context) {
  // Ensure the path has .vbi extension
  std::string final_vbi_path = vbi_path;
  const std::string tbc_ext = ".vbi";
  if (vbi_path.length() < tbc_ext.length() ||
      vbi_path.compare(vbi_path.length() - tbc_ext.length(), tbc_ext.length(),
                       tbc_ext) != 0) {
    final_vbi_path += ".vbi";
    ORC_LOG_DEBUG("Added .vbi extension: {}", final_vbi_path);
  }

  // Get field count early for progress reporting
  auto range = representation->field_range();
  size_t field_count = range.size();

  // Show initial progress
  if (progress_callback_) {
    progress_callback_(0, field_count, "Preparing export...");
  }

  try {
    ORC_LOG_DEBUG("Opening VBI file for writing: {}", final_vbi_path);

    // Open VBI file with buffered writer (1MB buffer chosen arbitrarily)
    std::shared_ptr<IFileWriter<uint8_t>> vbi_writer;
    if (stage_services_) {
      class FileWriter8Adapter final : public IFileWriter<uint8_t> {
       public:
        explicit FileWriter8Adapter(std::shared_ptr<IFileWriterUint8> impl)
            : impl_(std::move(impl)) {}

        using IFileWriter<uint8_t>::open;
        bool open(const std::string& filepath,
                  std::ios::openmode mode
                  [[maybe_unused]]) override {
          path_ = filepath;
          return impl_ && impl_->open(filepath);
        }

        void write(const uint8_t* data, size_t count) override {
          if (impl_) impl_->write(data, count);
        }
        void write(const std::vector<uint8_t>& data) override {
          if (impl_) impl_->write(data);
        }
        void flush() override {
          if (impl_) impl_->flush();
        }
        void close() override {
          if (impl_) impl_->close();
        }
        uint64_t bytes_written() const override { return 0; }
        bool is_open() const override { return true; }
        const std::string& filepath() const override { return path_; }

       private:
        std::shared_ptr<IFileWriterUint8> impl_;
        std::string path_;
      };

      auto writer8 =
          stage_services_->create_buffered_file_writer_uint8(static_cast<size_t>(1 * 1024 * 1024));
      if (writer8) {
        vbi_writer = std::make_shared<FileWriter8Adapter>(writer8);
      }
    }
    if (!vbi_writer) {
      ORC_LOG_ERROR("Failed to create VBI writer service");
      return false;
    }
    if (!vbi_writer->open(final_vbi_path)) {
      ORC_LOG_ERROR("Failed to open VBI file for writing: {}", final_vbi_path);
      return false;
    }

    std::shared_ptr<DaphneVBIWriterUtil> daphne_vbi_writer_util =
        std::make_shared<DaphneVBIWriterUtil>();
    daphne_vbi_writer_util->init(vbi_writer.get());

    daphne_vbi_writer_util
        ->write_header();  // header is required at the beginning of .VBI file

    // Build sorted list of field IDs
    std::vector<FieldID> field_ids;
    field_ids.reserve(field_count);
    for (FieldID field_id = range.start; field_id < range.end;
         field_id = field_id + 1) {
      if (representation->has_field(field_id)) {
        field_ids.push_back(field_id);
      }
    }
    std::sort(field_ids.begin(), field_ids.end());

    ORC_LOG_DEBUG("Processing {} fields in single pass", field_ids.size());

    // Create vector of observers; we only care about biphase and white flag
    std::vector<std::shared_ptr<Observer>> observers;
    observers.push_back(std::make_shared<BiphaseObserver>());
    observers.push_back(std::make_shared<WhiteFlagObserver>());

    ORC_LOG_DEBUG("Instantiated {} observers for VBI data extraction",
                  observers.size());

    size_t fields_processed = 0;

    // Single pass: populate observations and write VBI for each field
    for (FieldID field_id : field_ids) {
      // Check for cancellation
      if (pCancelRequested_->load()) {
        vbi_writer->close();
        ORC_LOG_WARN("DaphneVBISink: Export cancelled by user");
        pIsProcessing_->store(false);
        return false;
      }

      // ===== Write VBI data =====
      auto descriptor = representation->get_descriptor(field_id);
      if (!descriptor) {
        ORC_LOG_WARN("No descriptor for field {}, skipping", field_id.value());
        continue;
      }

      // ===== Run observers to populate observation context =====
      for (const auto& observer : observers) {
        observer->process_field(*representation, field_id, observation_context);
      }

      // Write observations to VBI file
      daphne_vbi_writer_util->write_observations(field_id, observation_context);

      fields_processed++;

      // Update progress callback every 10 fields
      if (fields_processed % 10 == 0) {
        if (progress_callback_) {
          progress_callback_(fields_processed, field_count,
                             "Exporting field " +
                                 std::to_string(fields_processed) + "/" +
                                 std::to_string(field_count));
        }
      }

      // Log progress every 50 fields
      if (fields_processed % 50 == 0) {
        ORC_LOG_DEBUG("Exported {}/{} fields ({:.1f}%)", fields_processed,
                      field_count, (fields_processed * 100.0) / field_count);
      }
    }

    vbi_writer->close();

    ORC_LOG_DEBUG("Successfully exported {} fields", fields_processed);
    return true;

  } catch (const std::exception& e) {
    ORC_LOG_ERROR("Exception during export: {}", e.what());
    return false;
  }
}
}  // namespace orc
