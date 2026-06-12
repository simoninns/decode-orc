/*
 * File:        frame_descriptor.h
 * Module:      orc-core
 * Purpose:     Per-frame metadata descriptor for CVBS_U10_4FSC frames
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <common_types.h>
#include <frame_id.h>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace orc {

// Metadata describing a single decoded frame in the CVBS_U10_4FSC domain.
//
// colour_frame_index encodes the frame's position in the colour subcarrier
// reference sequence:
//   -1  : unknown or unmeasurable (e.g., burst absent)
//    1–4: position within the PAL / PAL_M 4-frame sequence (EBU Tech. 3280-E
//          §1.1.1 / ITU-R BT.1700-1 Annex 1 Part B)
//    0–1: position within the NTSC 2-frame A/B sequence (SMPTE 244M-2003 §3.2)
struct FrameDescriptor {
  FrameID frame_id = 0;
  VideoSystem system = VideoSystem::Unknown;

  // Total frame height in lines (625 PAL, 525 NTSC/PAL_M).
  size_t height = 0;

  // Total flat-buffer sample count for this frame.
  size_t samples_total = 0;

  // Nominal samples per line (1135 PAL, 910 NTSC, 909 PAL_M).
  // PAL frames may have up to 4 lines with one extra sample; this field
  // holds the base/nominal value.
  size_t samples_per_line_nominal = 0;

  // Colour-frame sequence index. -1 when unknown or unmeasurable.
  int colour_frame_index = -1;

  // VBI-derived frame number, if available.
  std::optional<int32_t> frame_number;

  // VITC/LTC timecode, if available.
  std::optional<uint32_t> timecode;

  // NTSC-J non-standard black level in the CVBS_U10_4FSC 10-bit domain.
  // Present only when the source metadata carries an explicit black-level
  // override (e.g., a Japanese NTSC disc mastered at 0 IRE).
  std::optional<int32_t> black_level_override;

  // True for synthetic frames inserted by FrameMapStage to fill sequence gaps.
  bool is_padding_frame = false;
};

}  // namespace orc
