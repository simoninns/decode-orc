/*
 * File:        line_navigation_mapper.cpp
 * Module:      orc-gui
 * Purpose:     Centralized line-scope navigation mapping helper
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "line_navigation_mapper.h"

#include <algorithm>

namespace orc::gui {

LineNavigationTarget computeLineNavigationTarget(
    const LineNavigationRequest& request,
    const std::function<orc::FieldToImageMappingResult(
        uint64_t field_index, int field_line, int image_height)>&
        map_field_to_image,
    const std::function<orc::ImageToFieldMappingResult(
        int image_y, int image_height)>& map_image_to_field) {
  LineNavigationTarget target;

  if (request.direction == 0 || request.image_height <= 0) {
    return target;
  }

  const auto current_image_pos = map_field_to_image(
      request.current_field, request.current_line, request.image_height);
  if (!current_image_pos.is_valid) {
    return target;
  }

  // Defensive normalization: some frame mappings can report the asymmetric
  // extra-line position just outside the image range (e.g. NTSC bottom line).
  // Clamp to the nearest valid pixel row so reverse navigation still works.
  const int current_image_y =
      std::clamp(current_image_pos.image_y, 0, request.image_height - 1);

  const int step = (request.direction > 0) ? 1 : -1;
  const int new_image_y = current_image_y + step;
  if (new_image_y < 0 || new_image_y >= request.image_height) {
    return target;
  }

  const auto next_mapping =
      map_image_to_field(new_image_y, request.image_height);
  if (!next_mapping.is_valid) {
    return target;
  }

  // In interlaced frame modes two consecutive image rows both map to the same
  // field_line (one per field). If stepping one row lands on the same field
  // line as the starting position we are at the adjacent field for the same
  // scan line — visually unchanged. Step one more row so that a single button
  // press always moves to a different line.
  if (next_mapping.field_line == request.current_line) {
    const int retry_y = new_image_y + step;
    if (retry_y >= 0 && retry_y < request.image_height) {
      const auto retry_mapping =
          map_image_to_field(retry_y, request.image_height);
      if (retry_mapping.is_valid &&
          retry_mapping.field_line != request.current_line) {
        target.is_valid = true;
        target.field_index = retry_mapping.field_index;
        target.line_number = retry_mapping.field_line;
        return target;
      }
    }
    // If the retry also gives the same line (boundary or degenerate mapping),
    // fall through and return the single-step result anyway.
  }

  target.is_valid = true;
  target.field_index = next_mapping.field_index;
  target.line_number = next_mapping.field_line;
  return target;
}

}  // namespace orc::gui