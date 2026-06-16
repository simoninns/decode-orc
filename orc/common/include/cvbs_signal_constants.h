/*
 * File:        cvbs_signal_constants.h
 * Module:      orc-common
 * Purpose:     Normative CVBS_U10_4FSC signal constants for PAL, NTSC, and
 * PAL_M
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <common_types.h>

#include <cstdint>
#include <utility>

// This header is the single authoritative source for CVBS_U10_4FSC signal
// constants.  No file other than this header may define these numeric values.
// Stages and tests must include this header and use the named constants.

namespace orc {

// ---------------------------------------------------------------------------
// PAL — EBU Tech. 3280-E
// ---------------------------------------------------------------------------

// EBU Tech. 3280-E §1.1: PAL colour subcarrier frequency.
constexpr double kPalFsc = 4'433'618.75;

// EBU Tech. 3280-E §1.1: 4FSC sample rate = 4 × fsc.
constexpr double kPalSampleRate = 17'734'475.0;

// EBU Tech. 3280-E §1.1: Nominal samples per line (non-integer due to
// non-orthogonal sampling).
constexpr double kPalSamplesPerLine = 1135.0064;

// EBU Tech. 3280-E §1.1: Total samples in a complete PAL frame.
// = 625 lines × 1135 samples + 4 extra samples on non-orthogonal lines.
constexpr int32_t kPalFrameSamples = 709'379;

// EBU Tech. 3280-E §1.1: Total lines in a PAL frame.
constexpr int32_t kPalFrameLines = 625;

// EBU Tech. 3280-E §1.3: Lines in PAL field 1 (the 313-line field; CVBS
// convention places the longer field first in the flat frame buffer).
constexpr int32_t kPalField1Lines = 313;

// EBU Tech. 3280-E §1.3: Nominal integer samples per line (floor of 1135.0064).
// Use this everywhere a fixed per-line width of 1135 is required (TBC storage,
// frame_line_sample_offset, display width, etc.).
constexpr int32_t kPalSamplesPerLineNominal = 1135;

// EBU Tech. 3280-E §1.3: Maximum samples on any PAL line.
// Lines 312 and 624 (0-based) each carry 2 extra samples → 1137.
constexpr int32_t kPalMaxSamplesPerLine = 1137;

// EBU Tech. 3280-E: Normative CVBS_U10_4FSC signal levels (10-bit domain).
constexpr int32_t kPalSyncTip = 4;
constexpr int32_t kPalBlanking = 256;
constexpr int32_t kPalBlack = 282;
constexpr int32_t kPalWhite = 844;
constexpr int32_t kPalPeak = 1019;

// EBU Tech. 3280-E §1.3.1: normative placement of the 4 extra samples per PAL
// frame.  The standard concentrates them on the last line of each field:
//   Frame-flat line 312 (0-based = line 313 1-based, last of field 1): +2
//   Frame-flat line 624 (0-based = line 625 1-based, last of field 2): +2
//
// The array holds one entry per extra sample so that frame_line_sample_offset
// and frame_line_sample_count can iterate with a simple +1-per-match rule;
// duplicate entries indicate the named line carries 2 extras.
// Verified: 623×1135 + 2×1137 = 625×1135 + 4 = 709,375 + 4 = 709,379. ✓
constexpr int32_t kPalExtraSampleLines[4] = {312, 312, 624, 624};

// ---------------------------------------------------------------------------
// NTSC — SMPTE 244M-2003
// ---------------------------------------------------------------------------

// SMPTE 244M-2003 §4.1: NTSC colour subcarrier frequency.
constexpr double kNtscFsc = 315.0e6 / 88.0;

// SMPTE 244M-2003 §4.1: 4FSC sample rate = 4 × fsc.
constexpr double kNtscSampleRate = 4.0 * kNtscFsc;

// SMPTE 244M-2003 §4.1: Samples per line (orthogonal, exact integer).
constexpr int32_t kNtscSamplesPerLine = 910;

// SMPTE 244M-2003 §4.1: Total samples in a complete NTSC frame.
// = 525 lines × 910 samples/line.
constexpr int32_t kNtscFrameSamples = 477'750;

// SMPTE 244M-2003 §3.1: Total lines in an NTSC frame.
constexpr int32_t kNtscFrameLines = 525;

// SMPTE 244M-2003 §3.2: Lines in NTSC VFR field 1 (top spatial field).
// ld-decode stores fields in temporal order; VFR reorders so the top spatial
// field (263 lines, even-scan) is field 1 and the earlier temporal field
// (262 lines, odd-scan) becomes VFR field 2.
constexpr int32_t kNtscField1Lines = 263;

// SMPTE 244M-2003: Normative CVBS_U10_4FSC signal levels (10-bit domain).
constexpr int32_t kNtscSyncTip = 16;
constexpr int32_t kNtscBlanking = 240;
constexpr int32_t kNtscBlack = 252;
constexpr int32_t kNtscWhite = 800;
constexpr int32_t kNtscPeak = 988;

// ---------------------------------------------------------------------------
// PAL_M — ITU-R BT.1700-1 Annex 1 Part B
// ---------------------------------------------------------------------------

// ITU-R BT.1700-1 Annex 1 Part B: PAL_M colour subcarrier frequency.
// Derived from: fsc = (909/4) × (525 × 30000/1001) Hz.
constexpr double kPalMFsc = (909.0 / 4.0) * (525.0 * 30000.0 / 1001.0);

// ITU-R BT.1700-1 Annex 1 Part B: 4FSC sample rate = 4 × fsc.
constexpr double kPalMSampleRate = 4.0 * kPalMFsc;

// ITU-R BT.1700-1 Annex 1 Part B: Samples per line (orthogonal, exact integer).
constexpr int32_t kPalMSamplesPerLine = 909;

// ITU-R BT.1700-1 Annex 1 Part B: Total samples in a complete PAL_M frame.
// = 525 lines × 909 samples/line.
constexpr int32_t kPalMFrameSamples = 477'225;

// ITU-R BT.1700-1 Annex 1 Part B: Total lines in a PAL_M frame.
constexpr int32_t kPalMFrameLines = 525;

// ITU-R BT.1700-1 Annex 1 Part B: Lines in PAL_M VFR field 1 (top spatial
// field). Same swap convention as NTSC: the 263-line even-scan field becomes
// VFR field 1 (top).
constexpr int32_t kPalMField1Lines = 263;

// PAL_M signal levels are identical to NTSC (same line count and blanking).
// Use kNtscSyncTip, kNtscBlanking, kNtscBlack, kNtscWhite, kNtscPeak.

// ---------------------------------------------------------------------------
// ld-decode 16-bit domain normative levels
// ---------------------------------------------------------------------------
// The ld-decode .tbc format stores samples as uint16_t with the following
// standard mapping.  kTbcBlanking / kTbcWhite are ONLY for use in the
// tbc_source and ld_sink stages when converting CVBS_U10_4FSC ↔ ld-decode
// 16-bit.  All other pipeline code must use the system-specific CVBS_U10_4FSC
// constants above (kPalBlanking/kPalWhite, kNtscBlanking/kNtscWhite) or the
// 10-bit values from SourceParameters (blanking_level, white_level).
constexpr int32_t kTbcBlanking = 16384;  // 0 IRE blanking (0x4000 = 25 %)
constexpr int32_t kTbcWhite = 54400;     // 100 IRE white  (≈ 83 % of 65535)

// ---------------------------------------------------------------------------
// Colour burst sample range constants
// ---------------------------------------------------------------------------
// Sample offsets within a source line that locate the colour burst window.
// These are nominal values fixed by the video standard; per-disc values may
// deviate by a few samples for damaged or non-standard tapes, but spec-defined
// ranges are sufficient for dropout-correction masking and observer windowing.

// EBU Tech. 3280-E Table 1: PAL colour burst sample range.
constexpr int32_t kPalColourBurstStart = 98;
constexpr int32_t kPalColourBurstEnd = 138;

// SMPTE 244M-2003 Table 1: NTSC colour burst sample range.
// PAL_M uses the same values as NTSC.
constexpr int32_t kNtscColourBurstStart = 72;
constexpr int32_t kNtscColourBurstEnd = 108;

// Return the (start, end) colour burst sample range for the given video system.
inline std::pair<int32_t, int32_t> colour_burst_range(VideoSystem sys) {
  if (sys == VideoSystem::PAL) {
    return {kPalColourBurstStart, kPalColourBurstEnd};
  }
  // NTSC and PAL_M share the same burst window.
  return {kNtscColourBurstStart, kNtscColourBurstEnd};
}

// ---------------------------------------------------------------------------
// System-derived sample rate and subcarrier frequency helpers
// ---------------------------------------------------------------------------
// Both values are fully determined by the video system; reading them from
// per-disc metadata and re-storing in SourceParameters adds round-trip noise.

// Return the 4FSC sample rate (Hz) for the given video system.
// EBU Tech. 3280-E §1.1 (PAL) / SMPTE 244M-2003 §4.1 (NTSC) /
// ITU-R BT.1700-1 Annex 1 Part B (PAL_M).
inline double sample_rate_from_system(VideoSystem sys) {
  switch (sys) {
    case VideoSystem::PAL:
      return kPalSampleRate;
    case VideoSystem::PAL_M:
      return kPalMSampleRate;
    default:
      return kNtscSampleRate;
  }
}

// Return the colour subcarrier frequency (Hz) for the given video system.
// EBU Tech. 3280-E §1.1 (PAL) / SMPTE 244M-2003 §4.1 (NTSC) /
// ITU-R BT.1700-1 Annex 1 Part B (PAL_M).
inline double fsc_from_system(VideoSystem sys) {
  switch (sys) {
    case VideoSystem::PAL:
      return kPalFsc;
    case VideoSystem::PAL_M:
      return kPalMFsc;
    default:
      return kNtscFsc;
  }
}

// Return the number of lines in field 1 (the larger/first field in the flat
// frame buffer) for the given video system.
inline size_t field1_lines(VideoSystem sys) {
  switch (sys) {
    case VideoSystem::PAL:
      return static_cast<size_t>(kPalField1Lines);
    case VideoSystem::PAL_M:
      return static_cast<size_t>(kPalMField1Lines);
    default:
      return static_cast<size_t>(kNtscField1Lines);
  }
}

}  // namespace orc
