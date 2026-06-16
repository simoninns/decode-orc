/*
 * File:        colour_frame_phase_observer.cpp
 * Module:      orc-core
 * Purpose:     Colour-frame sequence index observer
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "colour_frame_phase_observer.h"

#include <cvbs_signal_constants.h>

#include <cmath>

#include "../include/field_id.h"
#include "../include/frame_line_util.h"
#include "../include/logging.h"
#include "../include/observation_context.h"
#include "../include/video_frame_representation.h"

namespace orc {

namespace {

// Number of burst samples to demodulate per frame.
constexpr int kBurstCount = 40;

// Amplitude threshold below which the burst is considered absent.
// Far below the spec minimum (~50 ADU for PAL); 20 ADU catches
// blank-tape and pre-programme leader without false-triggering.
constexpr double kMinBurstAmplitude = 20.0;

// Demodulate the burst at abs_offset and return the carrier angle in [0,360).
// Returns -1.0 when the burst amplitude is below kMinBurstAmplitude.
double measure_burst_angle(const int16_t* frame_data, size_t abs_offset,
                           int32_t blanking_10bit) {
  const int phase_base = static_cast<int>(abs_offset % 4);
  const int16_t* burst_ptr = frame_data + abs_offset;

  // Quadrature demodulation at 4FSC: cos/sin values cycle
  // {1,0,-1,0}/{0,1,0,-1}.
  double I = 0.0;
  double Q = 0.0;
  for (int n = 0; n < kBurstCount; ++n) {
    const double ac = static_cast<double>(burst_ptr[n]) - blanking_10bit;
    switch ((phase_base + n) % 4) {
      case 0:
        I += ac;
        break;
      case 1:
        Q += ac;
        break;
      case 2:
        I -= ac;
        break;
      case 3:
        Q -= ac;
        break;
      default:
        break;
    }
  }

  const double amplitude = std::sqrt(I * I + Q * Q);
  if (amplitude < kMinBurstAmplitude) {
    return -1.0;
  }

  double angle_deg = std::atan2(Q, I) * (180.0 / M_PI);
  if (angle_deg < 0.0) {
    angle_deg += 360.0;
  }
  return angle_deg;
}

// Map a measured burst angle to the colour-frame sequence index.
// Returns -1 when angle_deg < 0 (absent/weak burst).
//
// EBU Tech. 3280-E §1.1.1 (PAL); SMPTE 244M-2003 §3.2 (NTSC);
// ITU-R BT.1700-1 Annex 1 Part B (PAL_M).
int angle_to_colour_frame_index(double angle_deg, VideoSystem system) {
  if (angle_deg < 0.0) {
    return -1;
  }

  if (system == VideoSystem::NTSC) {
    // SMPTE 244M-2003 §3.2: Frame A (~180°) → index 0; Frame B (~0°) → index 1.
    return (angle_deg >= 90.0 && angle_deg < 270.0) ? 0 : 1;
  }

  const int sector = static_cast<int>(angle_deg / 90.0) % 4;

  if (system == VideoSystem::PAL) {
    // EBU Tech. 3280-E §1.1.1: PAL 4-frame sequence.
    // Measured angle = −burst_phase; consecutive frames: 45°,135°,225°,315°.
    // sector [0°,90°)→1, [90°,180°)→2, [180°,270°)→3, [270°,360°)→4.
    static constexpr int kPalMap[4] = {1, 2, 3, 4};
    return kPalMap[sector];
  }

  // ITU-R BT.1700-1 Annex 1 Part B: PAL_M 4-frame sequence (+90°/frame).
  // sector [0°,90°)→1, [90°,180°)→4, [180°,270°)→3, [270°,360°)→2.
  static constexpr int kPalMMap[4] = {1, 4, 3, 2};
  return kPalMMap[sector];
}

}  // namespace

void ColourFramePhaseObserver::process_frame(
    const VideoFrameRepresentation& representation, FrameID frame_id,
    IObservationContext& context) {
  auto vp_opt = representation.get_video_parameters();
  if (!vp_opt.has_value()) {
    ORC_LOG_TRACE("ColourFramePhaseObserver: No video parameters for frame {}",
                  frame_id);
    return;
  }
  const auto& vp = vp_opt.value();

  const int16_t* frame_data = representation.get_frame(frame_id);
  if (!frame_data) {
    ORC_LOG_TRACE("ColourFramePhaseObserver: No frame data for frame {}",
                  frame_id);
    return;
  }

  // EBU Tech. 3280-E §1.2: PAL colour burst at samples 93..132.
  // SMPTE 244M-2003 §4.2.1: NTSC colour burst at samples 74..109.
  // ITU-R BT.1700-1 Annex 1 Part B: PAL_M uses same window as NTSC.
  constexpr int kRefLine = 9;  // 0-based frame-flat line index
  int burst_start = 0;
  size_t line_start = 0;

  switch (vp.system) {
    case VideoSystem::PAL:
      burst_start = 93;
      line_start = frame_line_sample_offset(
          VideoSystem::PAL, static_cast<size_t>(kPalSamplesPerLineNominal),
          static_cast<size_t>(kRefLine));
      break;
    case VideoSystem::NTSC:
      burst_start = 74;
      line_start = frame_line_sample_offset(
          VideoSystem::NTSC, static_cast<size_t>(kNtscSamplesPerLine),
          static_cast<size_t>(kRefLine));
      break;
    case VideoSystem::PAL_M:
      burst_start = 74;
      line_start = frame_line_sample_offset(
          VideoSystem::PAL_M, static_cast<size_t>(kPalMSamplesPerLine),
          static_cast<size_t>(kRefLine));
      break;
    default:
      return;
  }

  const size_t abs_offset = line_start + static_cast<size_t>(burst_start);
  const double angle =
      measure_burst_angle(frame_data, abs_offset, vp.blanking_level);
  const int colour_index = angle_to_colour_frame_index(angle, vp.system);

  // Store keyed by the first field of this frame.
  const FieldID fid(frame_id * 2);
  context.set(fid, "colour_frame_phase", "colour_frame_index",
              static_cast<int32_t>(colour_index));

  ORC_LOG_DEBUG("ColourFramePhaseObserver: Frame {} colour_frame_index={}",
                frame_id, colour_index);
}

}  // namespace orc
