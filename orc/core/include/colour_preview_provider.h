/*
 * File:        colour_preview_provider.h
 * Module:      orc-core
 * Purpose:     Interface for stages exposing colour-domain preview carriers.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#pragma once

#include <cstdint>
#include <optional>

#include "orc_preview_carriers.h"
#include "previewable_stage.h"

namespace orc {

/**
 * @brief Interface for colour-domain preview providers.
 *
 * Stages implementing this interface expose decoded component planes through a
 * structured carrier. The render layer is responsible for display conversion.
 */
class IColourPreviewProvider {
 public:
  virtual ~IColourPreviewProvider() = default;

  /**
   * @brief Return a colour-domain carrier for the requested frame index.
   *
   * @param frame_index Frame index in the stage's navigation domain.
   * @param hint Navigation hint from preview consumers.
   * @return Carrier when available; std::nullopt on decode/fetch failure.
   */
  virtual std::optional<ColourFrameCarrier> get_colour_preview_carrier(
      uint64_t frame_index, PreviewNavigationHint hint) const = 0;

  /// Convenience overload — defaults hint to Random
  std::optional<ColourFrameCarrier> get_colour_preview_carrier(
      uint64_t frame_index) const {
    return get_colour_preview_carrier(frame_index,
                                     PreviewNavigationHint::Random);
  }
};

}  // namespace orc
