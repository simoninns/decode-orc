/*
 * File:        line_navigation_mapper.h
 * Module:      orc-gui
 * Purpose:     Centralized line-scope navigation mapping helper
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#ifndef LINE_NAVIGATION_MAPPER_H
#define LINE_NAVIGATION_MAPPER_H

#include <cstdint>
#include <functional>

#include <orc_rendering.h>

namespace orc::gui {

struct LineNavigationRequest {
    int direction{0};
    uint64_t current_field{0};
    int current_line{0};
    int image_height{0};
};

struct LineNavigationTarget {
    bool is_valid{false};
    uint64_t field_index{0};
    int line_number{0};
};

LineNavigationTarget computeLineNavigationTarget(
    const LineNavigationRequest& request,
    const std::function<orc::FieldToImageMappingResult(uint64_t field_index, int field_line, int image_height)>& map_field_to_image,
    const std::function<orc::ImageToFieldMappingResult(int image_y, int image_height)>& map_image_to_field);

} // namespace orc::gui

#endif // LINE_NAVIGATION_MAPPER_H