/*
 * File:        amplitude_conversion.h
 * Module:      orc-common
 * Purpose:     Amplitude unit conversion utilities for CVBS_U10_4FSC signals
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <common_types.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>
#include <utility>

namespace orc {

// ============================================================================
// Core conversion functions
// ============================================================================

/// Active video amplitude in mV for the given video system.
/// EBU Tech. 3280-E §1.1: PAL = 700 mV.
/// SMPTE 170M-2004 §11.4: NTSC/PAL_M = 7.143 mV/IRE × 100 = 714.3 mV.
inline double active_video_mv(VideoSystem system) {
  if (system == VideoSystem::NTSC || system == VideoSystem::PAL_M) {
    return 714.3;
  }
  return 700.0;
}

/// Convert a 10-bit sample to millivolts.
/// Blanking level maps to 0 mV; white level maps to active_video_mv(system).
inline double samples10_to_mv(int32_t sample, int32_t blanking, int32_t white,
                              VideoSystem system) {
  const double range = static_cast<double>(white - blanking);
  if (range <= 0.0) {
    return 0.0;
  }
  return static_cast<double>(sample - blanking) / range *
         active_video_mv(system);
}

/// Convert a 10-bit sample to IRE.
/// Blanking level maps to 0 IRE; white level maps to 100 IRE.
inline double samples10_to_ire(int32_t sample, int32_t blanking,
                               int32_t white) {
  const double range = static_cast<double>(white - blanking);
  if (range <= 0.0) {
    return 0.0;
  }
  return static_cast<double>(sample - blanking) / range * 100.0;
}

/// Convert millivolts to the nearest 10-bit sample (clamped to [0, 1023]).
inline int32_t mv_to_samples10(double mv, int32_t blanking, int32_t white,
                               VideoSystem system) {
  const double range = static_cast<double>(white - blanking);
  const double active = active_video_mv(system);
  if (active <= 0.0 || range <= 0.0) {
    return blanking;
  }
  const int32_t result =
      blanking + static_cast<int32_t>(std::round(mv / active * range));
  return std::clamp(result, 0, 1023);
}

/// Convert IRE to the nearest 10-bit sample (clamped to [0, 1023]).
inline int32_t ire_to_samples10(double ire, int32_t blanking, int32_t white) {
  const double range = static_cast<double>(white - blanking);
  const int32_t result =
      blanking + static_cast<int32_t>(std::round(ire / 100.0 * range));
  return std::clamp(result, 0, 1023);
}

/// Convert a 10-bit sample to the chosen display unit.
inline double samples10_to_display(int32_t sample, int32_t blanking,
                                   int32_t white, VideoSystem system,
                                   AmplitudeDisplayUnit unit) {
  switch (unit) {
    case AmplitudeDisplayUnit::Millivolts:
      return samples10_to_mv(sample, blanking, white, system);
    case AmplitudeDisplayUnit::Samples10Bit:
      return static_cast<double>(sample);
    default:
      return samples10_to_ire(sample, blanking, white);
  }
}

/// Convert a value in the chosen display unit to the nearest 10-bit sample.
inline int32_t display_to_samples10(double value, int32_t blanking,
                                    int32_t white, VideoSystem system,
                                    AmplitudeDisplayUnit unit) {
  switch (unit) {
    case AmplitudeDisplayUnit::Millivolts:
      return mv_to_samples10(value, blanking, white, system);
    case AmplitudeDisplayUnit::Samples10Bit:
      return std::clamp(static_cast<int32_t>(std::round(value)), 0, 1023);
    default:
      return ire_to_samples10(value, blanking, white);
  }
}

// ============================================================================
// Tick interval helpers
// ============================================================================

/// Default minor-tick interval in display units (grid lines, not labelled).
/// IRE: 5 IRE; mV: 50 mV; 10-bit: 128 samples.
inline double amplitude_minor_tick(AmplitudeDisplayUnit unit) {
  switch (unit) {
    case AmplitudeDisplayUnit::Millivolts:
      return 50.0;
    case AmplitudeDisplayUnit::Samples10Bit:
      return 128.0;
    default:
      return 5.0;
  }
}

/// Default major-tick interval in display units (labelled grid lines).
/// IRE: 20 IRE; mV: 100 mV; 10-bit: 128 samples.
inline double amplitude_major_tick(AmplitudeDisplayUnit unit) {
  switch (unit) {
    case AmplitudeDisplayUnit::Millivolts:
      return 100.0;
    case AmplitudeDisplayUnit::Samples10Bit:
      return 128.0;
    default:
      return 20.0;
  }
}

/// Snap a value upward to the nearest multiple of step (for first visible
/// tick). Always returns an integer multiple of step; no fractional results.
inline double snap_ceil(double value, double step) {
  return std::ceil(value / step) * step;
}

// ============================================================================
// String formatting helpers
// ============================================================================

/// Short suffix string for a display unit: "IRE", "mV", or "" (10-bit has no
/// suffix).
inline std::string amplitude_unit_suffix(AmplitudeDisplayUnit unit) {
  switch (unit) {
    case AmplitudeDisplayUnit::Millivolts:
      return "mV";
    case AmplitudeDisplayUnit::Samples10Bit:
      return "";
    default:
      return "IRE";
  }
}

/// Human-readable Y-axis title for the given display unit.
/// Suitable for plot axis labels: "IRE", "mV (millivolts)", or "10-bit".
inline std::string amplitude_axis_title(AmplitudeDisplayUnit unit) {
  switch (unit) {
    case AmplitudeDisplayUnit::Millivolts:
      return "mV (millivolts)";
    case AmplitudeDisplayUnit::Samples10Bit:
      return "10-bit";
    default:
      return "IRE";
  }
}

/// Input precision (decimal places): 1 for IRE, 1 for mV, 0 for 10-bit.
inline int amplitude_input_precision(AmplitudeDisplayUnit unit) {
  if (unit == AmplitudeDisplayUnit::Samples10Bit) {
    return 0;
  }
  return 1;
}

/// Display range [min, max] in the chosen unit, covering sync_tip to peak.
/// Requires sync_tip, blanking, white, and peak 10-bit values from
/// SourceParameters.
inline std::pair<double, double> amplitude_display_range(
    int32_t sync_tip, int32_t blanking, int32_t white, int32_t peak,
    VideoSystem system, AmplitudeDisplayUnit unit) {
  const double lo =
      samples10_to_display(sync_tip, blanking, white, system, unit);
  const double hi = samples10_to_display(peak, blanking, white, system, unit);
  return {lo, hi};
}

/// Format a 10-bit sample as a display string with suffix
/// (e.g., "82.3 IRE", "576.5 mV", "844").
/// Use this for signal level markers; shows one decimal place for IRE and mV.
inline std::string format_amplitude(int32_t sample10, int32_t blanking,
                                    int32_t white, VideoSystem system,
                                    AmplitudeDisplayUnit unit) {
  std::ostringstream oss;
  if (unit == AmplitudeDisplayUnit::Samples10Bit) {
    oss << sample10;
  } else {
    const double value =
        samples10_to_display(sample10, blanking, white, system, unit);
    oss << std::fixed;
    oss.precision(1);
    oss << value << " " << amplitude_unit_suffix(unit);
  }
  return oss.str();
}

/// Format a value that is already in display-unit terms (e.g., a tick position
/// produced by snap_ceil) as a label string with suffix.
/// Tick labels never need decimal places: "10 IRE", "100 mV", "256".
inline std::string format_tick_label(double display_value,
                                     AmplitudeDisplayUnit unit) {
  std::ostringstream oss;
  oss << static_cast<int64_t>(std::round(display_value));
  const std::string suffix = amplitude_unit_suffix(unit);
  if (!suffix.empty()) {
    oss << " " << suffix;
  }
  return oss.str();
}

}  // namespace orc
