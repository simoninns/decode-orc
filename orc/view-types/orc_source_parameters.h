/*
 * File:        orc_source_parameters.h
 * Module:      orc-view-types
 * Purpose:     Source metadata types
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include <common_types.h>  // For VideoSystem enum

#include <array>
#include <cstdint>
#include <string>

namespace orc {

// ============================================================================
// SourceParameters — CVBS_U10_4FSC signal geometry and level domain
// ============================================================================
// This struct carries the signal parameters that stages need to interpret
// frame sample buffers correctly.  All level fields are in the
// CVBS_U10_4FSC 10-bit domain (0–1023).
//
// Fields marked [DEPRECATED — Phase 1→4 migration] are retained only to keep
// existing consumers compiling while they are migrated to the new fields in
// subsequent phases.  They will be removed once all consumers have migrated.
struct SourceParameters {
  // ---------------------------------------------------------------------------
  // Identity
  // ---------------------------------------------------------------------------
  VideoSystem system = VideoSystem::Unknown;
  bool is_widescreen = false;

  // ---------------------------------------------------------------------------
  // Frame geometry (CVBS_U10_4FSC domain)
  // ---------------------------------------------------------------------------

  // Nominal samples per line (1135 PAL, 910 NTSC, 909 PAL_M).
  // PAL frames may have up to 4 lines with one extra sample.
  int32_t frame_width_nominal = -1;

  // Total lines per frame (625 PAL, 525 NTSC/PAL_M).
  int32_t frame_height = -1;

  // Total number of frames available from this source.
  int32_t number_of_sequential_frames = -1;

  // ---------------------------------------------------------------------------
  // Signal levels (CVBS_U10_4FSC 10-bit domain)
  // ---------------------------------------------------------------------------
  // Spec-defined normative values are in cvbs_signal_constants.h.
  // These fields hold the values actually used by this source; for standard
  // signals they equal the spec constants.  has_nonstandard_values is set when
  // they differ.

  int32_t sync_tip_level = -1;   // Sync tip (e.g., kPalSyncTip = 4)
  int32_t blanking_level = -1;   // Blanking / 0 IRE (e.g., kPalBlanking = 256)
  int32_t black_level = -1;      // Black level (e.g., kPalBlack = 282)
  int32_t white_level = -1;      // White / 100 IRE (e.g., kPalWhite = 844)
  int32_t peak_level = -1;       // Peak white (e.g., kPalPeak = 1019)

  // True when black_level or white_level differs from the spec-defined
  // constant for this system (e.g., NTSC-J, or a video_params override).
  bool has_nonstandard_values = false;

  // ---------------------------------------------------------------------------
  // Active picture geometry
  // ---------------------------------------------------------------------------
  // Sample offsets within each line for the active video region.
  int32_t active_video_start = -1;
  int32_t active_video_end = -1;

  // 0-based frame-flat line indices for the active picture area.
  int32_t first_active_frame_line = -1;
  int32_t last_active_frame_line = -1;

  // ---------------------------------------------------------------------------
  // Source metadata
  // ---------------------------------------------------------------------------
  bool is_mapped = false;
  std::string tape_format;
  std::string decoder;    // e.g., "ld-decode", "vhs-decode"
  std::string git_branch;
  std::string git_commit;

  // True when active_video_start/end and first/last_active_frame_line have
  // been applied as a crop.  Decoder output stages write to 0-based
  // ComponentFrame coordinates when this is set.
  bool active_area_cropping_applied = false;

  // ---------------------------------------------------------------------------
  // DEPRECATED — retained for compilation while consumers migrate to the
  // CVBS_U10_4FSC field set above.  Will be removed in Phase 4.
  // ---------------------------------------------------------------------------
  bool is_subcarrier_locked = false;
  int32_t field_width = -1;
  int32_t field_height = -1;
  int32_t number_of_sequential_fields = -1;
  bool is_first_field_first = true;
  int32_t colour_burst_start = -1;
  int32_t colour_burst_end = -1;
  int32_t first_active_field_line = -1;
  int32_t last_active_field_line = -1;
  int32_t blanking_16b_ire = -1;
  int32_t black_16b_ire = -1;
  int32_t white_16b_ire = -1;
  double sample_rate = -1.0;
  double fsc = -1.0;

  bool is_valid() const {
    return system != VideoSystem::Unknown && frame_width_nominal > 0;
  }
};

}  // namespace orc
