/*
 * File:        amplitude_unit_helpers.h
 * Module:      orc-gui
 * Purpose:     Qt wrappers for amplitude unit conversion helpers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <amplitude_conversion.h>
#include <orc_source_parameters.h>

#include <QString>

namespace orc::gui {

/// Qt wrapper for format_amplitude; delegates to orc::format_amplitude.
inline QString format_amplitude_q(int32_t sample10,
                                  const orc::SourceParameters& p,
                                  orc::AmplitudeDisplayUnit unit) {
  return QString::fromStdString(orc::format_amplitude(
      sample10, p.blanking_level, p.white_level, p.system, unit));
}

/// Qt wrapper for tick-label formatting; delegates to orc::format_tick_label.
/// display_value is already in display-unit terms (produced by snap_ceil).
inline QString format_tick_label_q(double display_value,
                                   orc::AmplitudeDisplayUnit unit) {
  return QString::fromStdString(orc::format_tick_label(display_value, unit));
}

/// Qt wrapper for amplitude_unit_suffix; delegates to
/// orc::amplitude_unit_suffix.
inline QString amplitude_unit_suffix_q(orc::AmplitudeDisplayUnit unit) {
  return QString::fromStdString(orc::amplitude_unit_suffix(unit));
}

}  // namespace orc::gui
