/*
 * File:        closed_caption_observer.cpp
 * Module:      orc-core
 * Purpose:     Closed caption observer implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include <closed_caption_observer.h>
#include <orc/stage/cvbs_signal_constants.h>
#include <orc/stage/field_id.h>
#include <orc/stage/observation/observation_context.h>
#include <orc/stage/video_frame_representation.h>
#include <orc/support/logging.h>
#include <orc/support/vbi_utilities.h>

namespace orc {

void ClosedCaptionObserver::process_frame(
    const VideoFrameRepresentation& representation, FrameID frame_id,
    IObservationContext& context) {
  ORC_LOG_DEBUG("ClosedCaptionObserver::process_frame called for frame {}",
                frame_id);

  auto vp_opt = representation.get_video_parameters();
  if (!vp_opt.has_value()) {
    ORC_LOG_DEBUG("Frame {}: No video parameters available", frame_id);
    return;
  }
  const auto& vp = vp_opt.value();

  size_t f1_lines = field1_lines(vp.system);
  size_t line_width = static_cast<size_t>(vp.frame_width_nominal);

  // Colour burst end derived from video system constant.
  size_t colorburst_end =
      static_cast<size_t>(colour_burst_range(vp.system).second);

  for (size_t field_idx = 0; field_idx < 2; ++field_idx) {
    FieldID derived_fid(frame_id * 2 + field_idx);
    size_t line_offset = (field_idx == 0) ? 0 : f1_lines;
    size_t field_height = (field_idx == 0)
                              ? f1_lines
                              : static_cast<size_t>(vp.frame_height) - f1_lines;

    // Closed captions are NTSC only; field 2 (field_idx=1) is skipped as
    // captions appear on VFR field 1 (the top spatial / even-scan field).
    if (vp.system == VideoSystem::NTSC && field_idx == 1) {
      ORC_LOG_DEBUG("Field {}: Skipping field 2 (no NTSC captions)",
                    derived_fid.value());
      context.set(derived_fid, "closed_caption", "present", false);
      continue;
    }

    // Line 21 for NTSC, line 22 for PAL (0-based: 20, 21)
    size_t line_num = (vp.system == VideoSystem::NTSC) ? 20 : 21;

    if (line_num >= field_height) {
      ORC_LOG_DEBUG("Field {}: Line {} >= field height {}", derived_fid.value(),
                    line_num, field_height);
      context.set(derived_fid, "closed_caption", "present", false);
      continue;
    }

    const int16_t* line_data =
        representation.get_line(frame_id, line_offset + line_num);
    if (line_data == nullptr) {
      ORC_LOG_DEBUG("Field {}: get_line({}) returned nullptr",
                    derived_fid.value(), line_offset + line_num);
      context.set(derived_fid, "closed_caption", "present", false);
      continue;
    }

    // Debug: check if line data has any non-zero values
    if (frame_id < 10) {
      std::string sample_debug;
      for (size_t i = 100; i < 130 && i < line_width; i += 5) {
        sample_debug += std::to_string(line_data[i]) + " ";
      }
      ORC_LOG_DEBUG("Field {}: Line {} samples[100-130 step 5]: {}",
                    derived_fid.value(), line_num, sample_debug);
    }

    // Calculate zero crossing from actual line data (min/max midpoint)
    int16_t min_sample = 32767;
    int16_t max_sample = -32768;
    for (size_t i = 0; i < line_width; ++i) {
      if (line_data[i] < min_sample) min_sample = line_data[i];
      if (line_data[i] > max_sample) max_sample = line_data[i];
    }
    int16_t zero_crossing = static_cast<int16_t>(
        (static_cast<int32_t>(min_sample) + static_cast<int32_t>(max_sample)) /
        2);

    if (frame_id < 3) {
      ORC_LOG_DEBUG("Field {}: Line {} min={}, max={}, zero_crossing={}",
                    derived_fid.value(), line_num, min_sample, max_sample,
                    zero_crossing);
    }

    // Bit clock is 32 x fH [CTA-608-E p14]
    double samples_per_bit = static_cast<double>(line_width) / 32.0;

    DecodedCaption decoded;
    bool success = decode_line(line_data, line_width, zero_crossing,
                               colorburst_end, samples_per_bit, decoded);

    context.set(derived_fid, "closed_caption", "present", success);
    if (success) {
      context.set(derived_fid, "closed_caption", "data0",
                  static_cast<int32_t>(decoded.data0));
      context.set(derived_fid, "closed_caption", "data1",
                  static_cast<int32_t>(decoded.data1));
      context.set(derived_fid, "closed_caption", "parity0_valid",
                  decoded.parity_valid0);
      context.set(derived_fid, "closed_caption", "parity1_valid",
                  decoded.parity_valid1);

      ORC_LOG_DEBUG(
          "ClosedCaptionObserver: Field {} CC=[{:#04x}, {:#04x}] parity=({}, "
          "{})",
          derived_fid.value(), decoded.data0, decoded.data1,
          decoded.parity_valid0, decoded.parity_valid1);
    }
  }
}

std::vector<ObservationKey> ClosedCaptionObserver::get_provided_observations()
    const {
  return {
      {"closed_caption", "present", ObservationType::BOOL,
       "Closed caption data decoded", true},
      {"closed_caption", "data0", ObservationType::INT32,
       "First EIA-608 byte (7-bit + parity)", true},
      {"closed_caption", "data1", ObservationType::INT32,
       "Second EIA-608 byte (7-bit + parity)", true},
      {"closed_caption", "parity0_valid", ObservationType::BOOL,
       "Parity validity for first byte", true},
      {"closed_caption", "parity1_valid", ObservationType::BOOL,
       "Parity validity for second byte", true},
  };
}

bool ClosedCaptionObserver::decode_line(const int16_t* line_data,
                                        size_t sample_count,
                                        int16_t zero_crossing,
                                        size_t colorburst_end,
                                        double samples_per_bit,
                                        DecodedCaption& decoded) const {
  ORC_LOG_DEBUG(
      "decode_line: sample_count={}, zero_crossing={}, colorburst_end={}, "
      "samples_per_bit={}",
      sample_count, zero_crossing, colorburst_end, samples_per_bit);

  auto transition_map =
      vbi_utils::get_transition_map(line_data, sample_count, zero_crossing);

  ORC_LOG_DEBUG("decode_line: transition_map size={}", transition_map.size());

  // Find 00 start bits (1.5-bit low period)
  double x = static_cast<double>(colorburst_end) + (2.0 * samples_per_bit);
  double x_limit = static_cast<double>(sample_count) - (17.0 * samples_per_bit);
  double last_one = x;

  while ((x - last_one) < (1.5 * samples_per_bit)) {
    if (x >= x_limit) {
      ORC_LOG_DEBUG(
          "decode_line: Failed to find 00 start bits (x={}, x_limit={})", x,
          x_limit);
      return false;
    }
    if (transition_map[static_cast<size_t>(x)]) last_one = x;
    x += 1.0;
  }

  // Find 1 start bit
  double x_before_search = x;
  if (!vbi_utils::find_transition(transition_map, true, x, x_limit)) {
    ORC_LOG_DEBUG("decode_line: Failed to find 1 start bit at x={}",
                  x_before_search);
    size_t start_pos = std::max(0, static_cast<int>(x_before_search) - 10);
    size_t end_pos = std::min(transition_map.size(),
                              static_cast<size_t>(x_before_search) + 50);
    std::string map_debug;
    for (size_t i = start_pos; i < end_pos; ++i) {
      map_debug += (transition_map[i] ? '1' : '0');
    }
    ORC_LOG_DEBUG("decode_line: transition_map[{}-{}]: {}", start_pos,
                  end_pos - 1, map_debug);
    return false;
  }

  // Skip start bit, move to first data bit
  x += 1.5 * samples_per_bit;

  // Decode first byte (7 bits + parity)
  uint8_t byte0 = 0;
  for (int i = 0; i < 7; ++i) {
    byte0 >>= 1;
    if (transition_map[static_cast<size_t>(x)]) byte0 += 64;
    x += samples_per_bit;
  }
  uint8_t parity0 = transition_map[static_cast<size_t>(x)] ? 1 : 0;
  x += samples_per_bit;

  // Decode second byte
  uint8_t byte1 = 0;
  for (int i = 0; i < 7; ++i) {
    byte1 >>= 1;
    if (transition_map[static_cast<size_t>(x)]) byte1 += 64;
    x += samples_per_bit;
  }
  uint8_t parity1 = transition_map[static_cast<size_t>(x)] ? 1 : 0;

  decoded.data0 = byte0;
  decoded.data1 = byte1;

  // Check parity: legacy tool checks isEvenParity(byte) && parityBit != 1
  decoded.parity_valid0 = !(vbi_utils::is_even_parity(byte0) && parity0 != 1);
  decoded.parity_valid1 = !(vbi_utils::is_even_parity(byte1) && parity1 != 1);

  return true;
}

}  // namespace orc
