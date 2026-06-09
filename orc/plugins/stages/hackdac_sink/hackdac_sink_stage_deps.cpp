/*
 * File:        hackdac_sink_stage_deps.cpp
 * Module:      orc-core
 * Purpose:     HackdacSinkStage dependency implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "hackdac_sink_stage_deps.h"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <limits>

#include "buffered_file_io.h"
#include "logging.h"

namespace orc {
void HackdacSinkStageDeps::init(TriggerProgressCallback progress_callback,
                                std::atomic<bool>* cancel_requested) {
  progress_callback_ = std::move(progress_callback);
  cancel_requested_ = cancel_requested;
}

int16_t HackdacSinkStageDeps::to_signed_sample(uint16_t sample) {
  int32_t shifted = static_cast<int32_t>(sample) - 32768;
  if (shifted > std::numeric_limits<int16_t>::max()) {
    return std::numeric_limits<int16_t>::max();
}
  if (shifted < std::numeric_limits<int16_t>::min()) {
    return std::numeric_limits<int16_t>::min();
}
  return static_cast<int16_t>(shifted);
}

std::string HackdacSinkStageDeps::system_to_string(VideoSystem system) {
  switch (system) {
    case VideoSystem::PAL:
      return "PAL";
    case VideoSystem::NTSC:
      return "NTSC";
    case VideoSystem::PAL_M:
      return "PAL_M";
    default:
      return "Unknown";
  }
}

bool HackdacSinkStageDeps::write_report(
    const std::string& report_path, VideoSystem resolved_system,
    size_t input_line_width, size_t processed_fields,
    const std::optional<SourceParameters>& video_params) const {
  std::ofstream report(report_path, std::ios::out | std::ios::trunc);
  if (!report.is_open()) {
    ORC_LOG_WARN("HackdacSinkDeps: Failed to write report file {}",
                 report_path);
    return false;
  }

  size_t first_field_height =
      calculate_standard_field_height(resolved_system, true);
  size_t second_field_height =
      calculate_standard_field_height(resolved_system, false);
  size_t first_field_samples = input_line_width * first_field_height;
  size_t second_field_samples = input_line_width * second_field_height;

  const size_t bytes_per_sample = sizeof(int16_t);
  const size_t first_field_bytes = first_field_samples * bytes_per_sample;
  const size_t second_field_bytes = second_field_samples * bytes_per_sample;

  size_t first_fields = (processed_fields + 1) / 2;
  size_t second_fields = processed_fields / 2;
  const size_t total_bytes =
      (first_fields * first_field_bytes) + (second_fields * second_field_bytes);

  report << "HackDAC sink export report\n";
  report << "Format: headerless stream of 16-bit signed little-endian samples "
            "(VFR representation, fields concatenated in capture order)\n";
  report << "Video format: " << system_to_string(resolved_system) << "\n";
  report << "Line width: " << input_line_width << " samples\n";
  report << "\n";
  report << "VFR field structure (alternating pattern):\n";
  report << "  First field: " << first_field_height
         << " lines = " << first_field_samples
         << " samples = " << first_field_bytes << " bytes\n";
  report << "  Second field: " << second_field_height
         << " lines = " << second_field_samples
         << " samples = " << second_field_bytes << " bytes\n";
  report << "\n";
  report << "Export statistics:\n";
  report << "  Fields exported: " << processed_fields << "\n";
  report << "    First fields: " << first_fields << "\n";
  report << "    Second fields: " << second_fields << "\n";
  report << "Total data bytes: " << total_bytes << "\n";

  const bool have_levels =
      video_params && video_params->blanking_16b_ire >= 0 &&
      video_params->black_16b_ire >= 0 && video_params->white_16b_ire >= 0;

  if (have_levels) {
    auto to_signed = [](int32_t value) { return value - 32768; };
    report << "Blanking level (signed 16-bit): "
           << to_signed(video_params->blanking_16b_ire) << "\n";
    report << "Black level (signed 16-bit): "
           << to_signed(video_params->black_16b_ire) << "\n";
    report << "White level (signed 16-bit): "
           << to_signed(video_params->white_16b_ire) << "\n";
  } else {
    report << "Blanking level (signed 16-bit): unknown\n";
    report << "Black level (signed 16-bit): unknown\n";
    report << "White level (signed 16-bit): unknown\n";
  }

  return true;
}

HackdacSinkExportResult HackdacSinkStageDeps::export_hackdac(
    const VideoFieldRepresentation* representation,
    const HackdacSinkExportOptions& options) {
  auto field_range = representation->field_range();
  if (field_range.size() == 0) {
    return {false, 0, "Error: Input has no fields to export"};
  }

  std::optional<FieldDescriptor> descriptor;
  FieldID first_field = field_range.start;
  while (first_field < field_range.end && !descriptor) {
    if (representation->has_field(first_field)) {
      descriptor = representation->get_descriptor(first_field);
    }
    first_field = first_field + 1;
  }

  if (!descriptor) {
    return {false, 0, "Error: Unable to read field descriptor"};
  }

  size_t line_width = descriptor->width;
  size_t line_count = descriptor->height;
  if (line_width == 0 || line_count == 0) {
    return {false, 0, "Error: Invalid field dimensions"};
  }

  size_t vfr_samples_per_field = line_width * line_count;

  auto video_params = representation->get_video_parameters();
  VideoSystem resolved_system = VideoSystem::Unknown;
  if (video_params && video_params->is_valid()) {
    resolved_system = video_params->system;
  } else {
    if (descriptor->format == VideoFormat::PAL) {
      resolved_system = VideoSystem::PAL;
}
    if (descriptor->format == VideoFormat::NTSC) {
      resolved_system = VideoSystem::NTSC;
}
  }

  BufferedFileWriter<int16_t> writer(static_cast<size_t>(16 * 1024 * 1024));
  if (!writer.open(options.output_path)) {
    return {false, 0,
            "Error: Failed to open output file: " + options.output_path};
  }

  uint64_t total_fields = field_range.size();
  uint64_t processed_fields = 0;

  for (FieldID fid = field_range.start; fid < field_range.end; fid = fid + 1) {
    if (cancel_requested_ && cancel_requested_->load()) {
      writer.close();
      return {false, 0, "Cancelled by user"};
    }

    if (!representation->has_field(fid)) {
      continue;
    }

    auto field_data = representation->get_field(fid);
    if (field_data.empty()) {
      ORC_LOG_WARN("HackdacSinkDeps: Field {} is empty, writing zeros",
                   fid.value());
      field_data.resize(vfr_samples_per_field, 0);
    }

    std::vector<int16_t> signed_data;
    signed_data.reserve(field_data.size());
    for (size_t i = 0; i < field_data.size(); ++i) {
      signed_data.push_back(to_signed_sample(field_data[i]));
    }

    writer.write(signed_data);
    processed_fields++;

    if (progress_callback_ && processed_fields % 10 == 0) {
      progress_callback_(processed_fields, total_fields,
                         "Exporting field " + std::to_string(processed_fields) +
                             "/" + std::to_string(total_fields));
    }
  }

  writer.close();
  write_report(options.report_path, resolved_system, line_width,
               processed_fields, video_params);

  return {true, static_cast<size_t>(processed_fields),
          "Success: " + std::to_string(processed_fields) + " fields exported"};
}
}  // namespace orc
