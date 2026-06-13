/*
 * File:        cc_sink_stage_deps.cpp
 * Module:      orc-core
 * Purpose:     CCSinkStage dependency implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "cc_sink_stage_deps.h"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

// VideoFormat / video_format_from_system are defined in video_field_representation.h
#include "video_field_representation.h"

namespace orc {
void CCSinkStageDeps::init(TriggerProgressCallback progress_callback,
                           std::atomic<bool>* cancel_requested) {
  progress_callback_ = std::move(progress_callback);
  cancel_requested_ = cancel_requested;
}

CCExportResult CCSinkStageDeps::export_cc(
    VideoFrameRepresentation* representation,
    IObservationContext& observation_context, CCExportOptions options) {
  if (!representation) {
    return {false, "Input representation is null", 0};
  }

  if (options.output_path.empty()) {
    return {false, "output_path parameter is required", 0};
  }

  (void)options.write_csv;
  (void)observation_context;

  const auto frame_rng = representation->frame_range();
  if (frame_rng.count() == 0) {
    return {false, "Input has no frames", 0};
  }

  auto descriptor = representation->get_frame_descriptor(frame_rng.first);
  if (!descriptor.has_value()) {
    return {false, "Cannot determine video format", 0};
  }
  const VideoFormat video_format = video_format_from_system(descriptor->system);

  const uint64_t total_frames = frame_rng.count();
  for (FrameID fid = frame_rng.first; fid <= frame_rng.last; ++fid) {
    if (cancel_requested_ && cancel_requested_->load()) {
      return {false, "Cancelled by user", 0};
    }
    if (progress_callback_) {
      const uint64_t done = fid - frame_rng.first + 1;
      progress_callback_(done, total_frames, "Processing closed captions...");
    }
  }

  int32_t cc_frames_exported = 0;
  bool success = false;

  if (options.export_format == CCExportFormat::SCC) {
    logger_.info("CCSinkDeps: Exporting closed captions to SCC format: {}",
                 options.output_path);
    success = export_scc(representation, options.output_path, video_format,
                         observation_context, cc_frames_exported);
  } else {
    logger_.info(
        "CCSinkDeps: Exporting closed captions to plain text format: {}",
        options.output_path);
    success =
        export_plain_text(representation, options.output_path, video_format,
                          observation_context, cc_frames_exported);
  }

  if (!success) {
    return {false, "Failed to export closed captions", cc_frames_exported};
  }

  return {true, "Exported " + std::to_string(cc_frames_exported) + " CC frames",
          cc_frames_exported};
}

std::string CCSinkStageDeps::generate_timestamp(int32_t field_index,
                                                VideoFormat format) const {
  double frame_index = static_cast<double>((field_index - 1) / 2);  // NOLINT(bugprone-integer-division)

  const double frames_per_second = (format == VideoFormat::PAL) ? 25.0 : 29.97;
  const double frames_per_minute = frames_per_second * 60.0;
  const double frames_per_hour = frames_per_minute * 60.0;

  const int32_t hh = static_cast<int32_t>(frame_index / frames_per_hour);
  frame_index -= static_cast<double>(hh) * frames_per_hour;
  const int32_t mm = static_cast<int32_t>(frame_index / frames_per_minute);
  frame_index -= static_cast<double>(mm) * frames_per_minute;
  const int32_t ss = static_cast<int32_t>(frame_index / frames_per_second);
  frame_index -= static_cast<double>(ss) * frames_per_second;
  const int32_t ff = static_cast<int32_t>(frame_index);

  std::ostringstream oss;
  oss << std::setfill('0') << std::setw(2) << hh << ":" << std::setfill('0')
      << std::setw(2) << mm << ":" << std::setfill('0') << std::setw(2) << ss
      << ":" << std::setfill('0') << std::setw(2) << ff;

  return oss.str();
}

uint8_t CCSinkStageDeps::apply_odd_parity(uint8_t byte) const {
  uint8_t val = byte & 0x7F;
  int count = 0;
  uint8_t tmp = val;
  while (tmp) {
    count += static_cast<int>(tmp & 1U);
    tmp >>= 1;
  }
  if (count % 2 == 0) {
    val |= 0x80;
  }
  return val;
}

int32_t CCSinkStageDeps::sanity_check_data(int32_t data_byte) const {
  if (data_byte == -1) {
    return -1;
  }

  if (data_byte >= 0x10 && data_byte <= 0x1F) {
    return data_byte;
  }

  if (data_byte >= 0x20 && data_byte <= 0x7E) {
    return data_byte;
  }

  return 0;
}

bool CCSinkStageDeps::is_control_code(uint8_t byte) const {
  return byte >= 0x10 && byte <= 0x1F;
}

bool CCSinkStageDeps::is_printable_char(uint8_t byte) const {
  return byte >= 0x20 && byte <= 0x7E;
}

bool CCSinkStageDeps::export_scc(
    const VideoFrameRepresentation* representation,
    const std::string& output_path, VideoFormat format,
    const IObservationContext& observation_context,
    int32_t& cc_frames_exported) {
  (void)representation;
  (void)format;
  (void)observation_context;
  try {
    std::ofstream file(output_path);
    if (!file.is_open()) {
      logger_.error("CCSinkDeps: Failed to open output file: {}", output_path);
      return false;
    }
    file << "Scenarist_SCC V1.0\n\n";
    file.close();
    cc_frames_exported = 0;
    logger_.info("CCSinkDeps: Exported SCC file (no CC observer data in VFrameR)");
    return true;
  } catch (const std::exception& e) {
    logger_.error("CCSinkDeps: Error exporting SCC: {}", e.what());
    return false;
  }
}

bool CCSinkStageDeps::export_plain_text(
    const VideoFrameRepresentation* representation,
    const std::string& output_path, VideoFormat format,
    const IObservationContext& observation_context,
    int32_t& cc_frames_exported) {
  (void)representation;
  (void)format;
  (void)observation_context;
  try {
    std::ofstream file(output_path);
    if (!file.is_open()) {
      logger_.error("CCSinkDeps: Failed to open output file: {}", output_path);
      return false;
    }
    file.close();
    cc_frames_exported = 0;
    logger_.info("CCSinkDeps: Exported plain text file (no CC observer data in VFrameR)");
    return true;
  } catch (const std::exception& e) {
    logger_.error("CCSinkDeps: Error exporting plain text: {}", e.what());
    return false;
  }
}
}  // namespace orc
