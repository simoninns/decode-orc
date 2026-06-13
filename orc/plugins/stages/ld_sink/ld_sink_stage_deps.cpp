/*
 * File:        ld_sink_stage_deps.cpp
 * Module:      orc-core
 * Purpose:     ld-decode Sink Stage dependencies implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "ld_sink_stage_deps.h"

#include <orc/plugin/orc_stage_services.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

#include "cvbs_signal_constants.h"
#include "dropout_util.h"
#include "file_io_interface.h"
#include "logging.h"
#include <common_types.h>

namespace orc {

void LDSinkStageDeps::init(TriggerProgressCallback progress_callback,
                           std::atomic<bool>* pIsProcessing,
                           std::atomic<bool>* pCancelRequested) {
  progress_callback_ = std::move(progress_callback);
  pIsProcessing_ = pIsProcessing;
  pCancelRequested_ = pCancelRequested;
}

namespace {

// Inverse level mapping: CVBS_U10_4FSC int16_t → TBC uint16_t.
// Formula: tbc = round((cvbs − cvbs_blanking) × (tbc_white − tbc_blanking)
//                       / (cvbs_white − cvbs_blanking) + tbc_blanking)
inline uint16_t cvbs_to_tbc(int16_t cvbs, int32_t tbc_blanking,
                             int32_t tbc_white, int32_t cvbs_blanking,
                             int32_t cvbs_white) {
  const double n = static_cast<double>(cvbs - cvbs_blanking) /
                   static_cast<double>(cvbs_white - cvbs_blanking);
  const double tbc =
      n * static_cast<double>(tbc_white - tbc_blanking) + tbc_blanking;
  const int32_t result = static_cast<int32_t>(std::lround(tbc));
  return static_cast<uint16_t>(std::max(0, std::min(65535, result)));
}

// True when frame_line_index is one of the four PAL non-orthogonal lines that
// carry 1136 samples instead of 1135.
inline bool is_pal_extra_sample_line(int32_t frame_line) {
  for (int32_t el : kPalExtraSampleLines) {
    if (el == frame_line) return true;
  }
  return false;
}

// Split a DropoutRun (frame-flat coordinates) into per-field DropoutInfo
// entries and append them to the appropriate output vectors.
// tbc_f1_dropouts ← entries that belong to TBC field 1 (is_first_field=true)
// tbc_f2_dropouts ← entries that belong to TBC field 2
void split_dropout_run(VideoSystem sys, const DropoutRun& run,
                       std::vector<DropoutInfo>& tbc_f1_dropouts,
                       std::vector<DropoutInfo>& tbc_f2_dropouts) {
  if (run.sample_count == 0) return;

  // Walk through the run sample by sample to build per-line entries.
  // Optimisation: advance by line when the run spans whole lines.
  uint64_t offset = run.sample_start;
  uint64_t end_offset = run.sample_start + run.sample_count;

  while (offset < end_offset) {
    auto fls = dropout_util::frame_sample_to_field_line(sys, offset);

    // Determine nominal width of this line (accounting for PAL extra sample).
    int32_t line_width;
    if (sys == VideoSystem::PAL) {
      // frame_sample_to_field_line uses field-local line; reconstruct frame
      // line to check for non-orthogonal status.
      int32_t frame_line =
          (fls.field == 1) ? fls.line
                           : (kPalField1Lines + fls.line);
      line_width = is_pal_extra_sample_line(frame_line)
                       ? kPalMaxSamplesPerLine
                       : (kPalMaxSamplesPerLine - 1);
    } else if (sys == VideoSystem::PAL_M) {
      line_width = kPalMSamplesPerLine;
    } else {
      line_width = kNtscSamplesPerLine;
    }

    // Samples remaining on the current line.
    int32_t samples_on_line = line_width - fls.sample;
    uint64_t run_on_line =
        std::min(static_cast<uint64_t>(samples_on_line), end_offset - offset);

    DropoutInfo di;
    di.line = static_cast<uint32_t>(fls.line);
    di.start_sample = static_cast<uint32_t>(fls.sample);
    di.end_sample =
        static_cast<uint32_t>(fls.sample + static_cast<int32_t>(run_on_line));

    // PAL field mapping: CVBS field 1 → TBC field 2, CVBS field 2 → TBC field 1
    // NTSC/PAL_M:        CVBS field 1 → TBC field 1, CVBS field 2 → TBC field 2
    if (sys == VideoSystem::PAL) {
      if (fls.field == 2) {
        tbc_f1_dropouts.push_back(di);
      } else {
        tbc_f2_dropouts.push_back(di);
      }
    } else {
      if (fls.field == 1) {
        tbc_f1_dropouts.push_back(di);
      } else {
        tbc_f2_dropouts.push_back(di);
      }
    }

    offset += run_on_line;
  }
}

}  // namespace

bool LDSinkStageDeps::write_tbc_and_metadata(
    const VideoFrameRepresentation* representation,
    const std::string& tbc_path,
    IObservationContext& observation_context) {
  (void)observation_context;

  std::string final_tbc_path = tbc_path;
  const std::string tbc_ext = ".tbc";
  if (tbc_path.length() < tbc_ext.length() ||
      tbc_path.compare(tbc_path.length() - tbc_ext.length(), tbc_ext.length(),
                       tbc_ext) != 0) {
    final_tbc_path += ".tbc";
    ORC_LOG_DEBUG("Added .tbc extension: {}", final_tbc_path);
  }

  std::string db_path = final_tbc_path + ".db";

  auto frame_rng = representation->frame_range();
  size_t frame_count = static_cast<size_t>(frame_rng.count());
  size_t expected_field_count = frame_count * 2;

  if (progress_callback_) {
    progress_callback_(0, expected_field_count, "Preparing export...");
  }

  try {
    ORC_LOG_DEBUG("Opening TBC file for writing: {}", final_tbc_path);
    ORC_LOG_DEBUG("Opening metadata database: {}", db_path);

    // Open TBC writer (16 MB buffer).
    std::shared_ptr<IFileWriter<uint16_t>> tbc_writer;
    if (stage_services_) {
      class FileWriter16Adapter final : public IFileWriter<uint16_t> {
       public:
        explicit FileWriter16Adapter(std::shared_ptr<IFileWriterUint16> impl)
            : impl_(std::move(impl)) {}

        using IFileWriter<uint16_t>::open;
        bool open(const std::string& filepath,
                  std::ios::openmode mode [[maybe_unused]]) override {
          path_ = filepath;
          return impl_ && impl_->open(filepath);
        }

        void write(const uint16_t* data, size_t count) override {
          if (impl_) impl_->write(data, count);
        }
        void write(const std::vector<uint16_t>& data) override {
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
        std::shared_ptr<IFileWriterUint16> impl_;
        std::string path_;
      };

      auto writer16 = stage_services_->create_buffered_file_writer_uint16(
          static_cast<size_t>(16 * 1024 * 1024));
      if (writer16) {
        tbc_writer = std::make_shared<FileWriter16Adapter>(writer16);
      }
    }
    if (!tbc_writer) {
      ORC_LOG_ERROR("Failed to create TBC writer service");
      return false;
    }
    if (!tbc_writer->open(final_tbc_path)) {
      ORC_LOG_ERROR("Failed to open TBC file for writing: {}", final_tbc_path);
      return false;
    }

    if (!metadata_writer_->open(db_path)) {
      ORC_LOG_ERROR("Failed to open metadata database for writing: {}", db_path);
      tbc_writer->close();
      return false;
    }

    // Retrieve source parameters for TBC-domain signal levels.
    auto video_params = representation->get_video_parameters();
    if (!video_params) {
      ORC_LOG_ERROR("No video parameters available");
      metadata_writer_->close();
      tbc_writer->close();
      return false;
    }
    video_params->decoder = "ld-decode";

    const int32_t tbc_blanking = kTbcBlanking;
    const int32_t tbc_white = kTbcWhite;
    const VideoSystem sys = video_params->system;

    // CVBS_U10_4FSC normative levels for the inverse mapping.
    int32_t cvbs_blanking, cvbs_white;
    if (sys == VideoSystem::PAL) {
      cvbs_blanking = kPalBlanking;
      cvbs_white = kPalWhite;
    } else {
      cvbs_blanking = kNtscBlanking;
      cvbs_white = kNtscWhite;
    }

    const size_t padded_lines = calculate_padded_field_height(sys);

    // Signal geometry.
    int32_t frame_lines_total, field1_cvbs_line_count, nominal_line_width;
    if (sys == VideoSystem::PAL) {
      frame_lines_total = kPalFrameLines;
      field1_cvbs_line_count = kPalField1Lines;
      nominal_line_width = kPalMaxSamplesPerLine - 1;  // 1135
    } else if (sys == VideoSystem::PAL_M) {
      frame_lines_total = kPalMFrameLines;
      field1_cvbs_line_count = kPalMField1Lines;
      nominal_line_width = kPalMSamplesPerLine;
    } else {
      frame_lines_total = kNtscFrameLines;
      field1_cvbs_line_count = kNtscField1Lines;
      nominal_line_width = kNtscSamplesPerLine;
    }

    video_params->number_of_sequential_fields =
        static_cast<int32_t>(expected_field_count);
    if (!metadata_writer_->write_video_parameters(*video_params)) {
      ORC_LOG_ERROR("Failed to write video parameters");
      metadata_writer_->close();
      tbc_writer->close();
      return false;
    }

    ORC_LOG_DEBUG(
        "LDSink: {} frames → {} fields; blanking={} white={} sys={}",
        frame_count, expected_field_count, tbc_blanking, tbc_white,
        static_cast<int>(sys));

    metadata_writer_->begin_transaction();
    size_t fields_exported = 0;

    for (FrameID frame_id = frame_rng.first; frame_rng.contains(frame_id);
         ++frame_id) {
      if (pCancelRequested_->load()) {
        metadata_writer_->commit_transaction();
        metadata_writer_->close();
        tbc_writer->close();
        ORC_LOG_WARN("LDSink: Export cancelled by user");
        pIsProcessing_->store(false);
        return false;
      }

      auto frame_desc = representation->get_frame_descriptor(frame_id);
      if (!frame_desc || frame_desc->is_padding_frame) {
        // Padding frames: emit two blanking-level fields to keep the file
        // sequential without corrupting frame count.
        size_t blank_field_samples =
            padded_lines * static_cast<size_t>(nominal_line_width);
        std::vector<uint16_t> blank(blank_field_samples,
                                    static_cast<uint16_t>(tbc_blanking));
        for (int f = 0; f < 2; ++f) {
          tbc_writer->write(blank);
          FieldMetadata fm;
          fm.seq_no = static_cast<int32_t>(fields_exported + 1);
          fm.is_first_field = (f == 0);
          metadata_writer_->write_field_metadata(fm);
          ++fields_exported;
        }
        continue;
      }

      // Build per-field dropout lists from the frame-flat DropoutRuns.
      std::vector<DropoutInfo> tbc_f1_dropouts, tbc_f2_dropouts;
      for (const auto& run : representation->get_dropout_hints(frame_id)) {
        split_dropout_run(sys, run, tbc_f1_dropouts, tbc_f2_dropouts);
      }

      // Describe the two TBC fields to extract from this CVBS frame.
      //
      // PAL:       TBC field 1 (is_first_field=true, 312 lines) = CVBS lines [313, 625)
      //            TBC field 2 (is_first_field=false, 313 lines) = CVBS lines [0, 313)
      // NTSC/PAL_M: TBC field 1 (is_first_field=true, 262 lines) = CVBS lines [0, 262)
      //             TBC field 2 (is_first_field=false, 263 lines) = CVBS lines [262, 525)
      struct FieldExtract {
        int32_t cvbs_start;
        int32_t cvbs_end;  // exclusive
        bool is_first_field;
        const std::vector<DropoutInfo>* dropouts;
      };

      std::array<FieldExtract, 2> extract_plan;
      if (sys == VideoSystem::PAL) {
        extract_plan[0] = {field1_cvbs_line_count, frame_lines_total, true,
                           &tbc_f1_dropouts};
        extract_plan[1] = {0, field1_cvbs_line_count, false, &tbc_f2_dropouts};
      } else {
        extract_plan[0] = {0, field1_cvbs_line_count, true, &tbc_f1_dropouts};
        extract_plan[1] = {field1_cvbs_line_count, frame_lines_total, false,
                           &tbc_f2_dropouts};
      }

      for (const auto& ep : extract_plan) {
        const size_t actual_lines =
            static_cast<size_t>(ep.cvbs_end - ep.cvbs_start);

        std::vector<uint16_t> field_buffer;
        field_buffer.reserve(padded_lines *
                             static_cast<size_t>(nominal_line_width));

        // Convert each CVBS line to TBC uint16_t samples.
        for (int32_t fl = ep.cvbs_start; fl < ep.cvbs_end; ++fl) {
          const int16_t* line_data =
              representation->get_line(frame_id, static_cast<size_t>(fl));

          // PAL extra-sample lines have 1136 samples; strip the last one so
          // all TBC lines are exactly nominal_line_width samples wide.
          const size_t read_width = static_cast<size_t>(nominal_line_width);

          if (!line_data) {
            for (size_t s = 0; s < read_width; ++s) {
              field_buffer.push_back(static_cast<uint16_t>(tbc_blanking));
            }
          } else {
            for (size_t s = 0; s < read_width; ++s) {
              field_buffer.push_back(cvbs_to_tbc(line_data[s], tbc_blanking,
                                                 tbc_white, cvbs_blanking,
                                                 cvbs_white));
            }
          }
        }

        // Pad the shorter field (always is_first_field=true) to padded_lines.
        if (actual_lines < padded_lines) {
          const size_t padding_lines = padded_lines - actual_lines;
          for (size_t p = 0; p < padding_lines; ++p) {
            for (int32_t s = 0; s < nominal_line_width; ++s) {
              field_buffer.push_back(static_cast<uint16_t>(tbc_blanking));
            }
          }
        }

        tbc_writer->write(field_buffer);

        FieldMetadata field_meta;
        field_meta.seq_no = static_cast<int32_t>(fields_exported + 1);
        field_meta.is_first_field = ep.is_first_field;
        if (frame_desc->colour_frame_index >= 0) {
          field_meta.field_phase_id = frame_desc->colour_frame_index;
        }
        metadata_writer_->write_field_metadata(field_meta);

        // Write per-field dropout info.
        FieldID export_field_id(fields_exported);
        for (const auto& di : *ep.dropouts) {
          metadata_writer_->write_dropout(export_field_id, di);
        }

        ++fields_exported;
      }

      if (fields_exported % 20 == 0 && progress_callback_) {
        progress_callback_(
            fields_exported, expected_field_count,
            "Exporting field " + std::to_string(fields_exported) + "/" +
                std::to_string(expected_field_count));
      }
    }

    metadata_writer_->commit_transaction();
    metadata_writer_->close();
    tbc_writer->close();

    ORC_LOG_DEBUG("LDSink: Successfully exported {} fields", fields_exported);
    return true;

  } catch (const std::exception& e) {
    ORC_LOG_ERROR("LDSink: Exception during export: {}", e.what());
    return false;
  }
}

}  // namespace orc
