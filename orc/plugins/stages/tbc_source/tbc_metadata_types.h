/*
 * File:        tbc_metadata_types.h
 * Module:      orc-stage-plugin-tbc-source
 * Purpose:     Shared TBC field metadata value types
 *
 * Defines the data structures that describe TBC field metadata — shared
 * between the tbc_source stage (reader) and the ld_sink stage (writer).
 * Both stages include this header.  All other pipeline code should be
 * oblivious to these types.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include <orc/stage/orc_source_parameters.h>
#include <orc/stage/video_metadata_types.h>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace orc {

/**
 * @brief VITC (Vertical Interval Timecode) data
 */
struct VitcData {
  bool in_use = false;
  std::array<int32_t, 8> vitc_data = {0, 0, 0, 0, 0, 0, 0, 0};
};

/**
 * @brief NTSC-specific field data
 */
struct NtscData {
  bool in_use = false;
  bool is_fm_code_data_valid = false;
  int32_t fm_code_data = 0;
  bool field_flag = false;
  bool is_video_id_data_valid = false;
  int32_t video_id_data = 0;
  bool white_flag = false;
};

/**
 * @brief Closed Caption data
 */
struct ClosedCaptionData {
  bool in_use = false;
  int32_t data0 = -1;
  int32_t data1 = -1;
};

/**
 * @brief VITS (Vertical Interval Test Signals) metrics
 */
struct VitsMetrics {
  bool in_use = false;
  double white_snr = 0.0;
  double black_psnr = 0.0;
};

/**
 * @brief Dropout information for a single dropout region within a field
 */
struct DropoutInfo {
  uint32_t line = 0;          ///< 0-based line number (converted from 1-based)
  uint32_t start_sample = 0;  ///< Start sample within line
  uint32_t end_sample = 0;    ///< End sample within line (exclusive)
};

/**
 * @brief Collection of dropout regions for a field
 */
struct DropoutData {
  std::vector<DropoutInfo> dropouts;
};

/**
 * @brief Complete metadata for a single TBC field
 */
struct FieldMetadata {
  int32_t seq_no = 0;  // Sequence number (primary key in DB)

  std::optional<bool> is_first_field;
  // Colour-sequence phase for one field.  NOT populated when reading a source
  // TBC (input phase metadata is ignored — phase is measured from the burst via
  // the colour_frame_phase observer); the ld_sink writer sets it from that
  // observer measurement when exporting a TBC.
  std::optional<int32_t> field_phase_id;
  std::optional<double> median_burst_ire;

  std::optional<int32_t> audio_samples;
  std::optional<int32_t> decode_faults;
  std::optional<double> disk_location;
  std::optional<int32_t> efm_t_values;
  std::optional<int32_t> ac3rf_symbols;
  std::optional<int64_t> file_location;
  std::optional<int32_t> sync_confidence;
  std::optional<bool> is_pad;

  // Cumulative byte offsets for O(1) random access
  std::optional<uint64_t> audio_byte_start;
  std::optional<uint64_t> audio_byte_end;
  std::optional<uint64_t> efm_byte_start;
  std::optional<uint64_t> efm_byte_end;
  std::optional<uint64_t> ac3rf_byte_start;
  std::optional<uint64_t> ac3rf_byte_end;

  VitsMetrics vits_metrics;
  VbiData vbi;
  NtscData ntsc;
  VitcData vitc;
  ClosedCaptionData closed_caption;
  std::vector<DropoutInfo> dropouts;
};

/**
 * @brief PCM audio parameters
 */
struct PcmAudioParameters {
  double sample_rate = -1.0;
  bool is_little_endian = false;
  bool is_signed = false;
  int32_t bits = -1;

  bool is_valid() const { return sample_rate > 0 && bits > 0; }
};

/**
 * @brief Signal levels in the ld-decode 16-bit domain.
 *
 * Only meaningful at the tbc_source stage (reading) and ld_sink stage
 * (writing).  All other pipeline code works in the CVBS_U10_4FSC 10-bit
 * domain via SourceParameters.
 */
struct TbcDomainLevels {
  int32_t blanking_16b = 0;  ///< 0 IRE blanking in ld-decode 16-bit domain
  int32_t white_16b = 0;     ///< 100 IRE white in ld-decode 16-bit domain

  bool is_valid() const { return white_16b > blanking_16b; }
};

}  // namespace orc
