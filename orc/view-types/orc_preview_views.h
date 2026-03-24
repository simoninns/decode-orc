/*
 * File:        orc_preview_views.h
 * Module:      orc-view-types
 * Purpose:     Shared preview-view contracts for registry-driven preview tools.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#pragma once

#include <string>
#include <vector>
#include <optional>

#include <node_id.h>
#include "orc_preview_types.h"
#include "orc_rendering.h"
#include "orc_vectorscope.h"

namespace orc {

/**
 * @brief Data payload category produced by a preview view request.
 */
enum class PreviewViewPayloadKind {
    None,
    Image,
    Vectorscope,
};

/**
 * @brief Lightweight descriptor for a registered preview view.
 */
struct PreviewViewDescriptor {
    std::string id;                     ///< Stable identifier used for routing
    std::string display_name;           ///< Human-readable view name for UI/CLI
    std::vector<VideoDataType> supported_data_types;
};

/**
 * @brief Result payload returned by preview-view request_data().
 */
struct PreviewViewDataResult {
    bool success{false};
    std::string error_message;
    PreviewViewPayloadKind payload_kind{PreviewViewPayloadKind::None};
    std::optional<PreviewImage> image;
    std::optional<VectorscopeData> vectorscope;

    bool is_valid() const
    {
        if (!success) {
            return false;
        }

        if (payload_kind == PreviewViewPayloadKind::Image) {
            return image.has_value() && image->is_valid();
        }

        if (payload_kind == PreviewViewPayloadKind::Vectorscope) {
            return vectorscope.has_value();
        }

        return false;
    }
};

/**
 * @brief Result of preview-view export operation.
 */
struct PreviewViewExportResult {
    bool success{false};
    std::string error_message;
};

// =============================================================================
// Live Preview Tweak Panel Types
// =============================================================================

/**
 * @brief Cost class for a live-tweakable stage parameter.
 *
 * Mirrors orc::PreviewTweakClass from the core layer. Defined here in view-types
 * so that the GUI and coordinator layers can express update cost without
 * depending directly on core stage headers.
 *
 * - DisplayPhase: re-apply display conversion to cached decoded output only (no re-decode).
 * - DecodePhase: re-decode the current preview field/frame only (no full DAG rebuild).
 */
enum class LiveTweakClass {
    DisplayPhase,  ///< Conversion-only update; no re-decode
    DecodePhase,   ///< Single-field re-decode; no full DAG rebuild
};

/**
 * @brief View-model for a single live-tweakable parameter declared by a stage.
 *
 * Returned by the presenter/coordinator layer so the GUI can build a parameter
 * widget for each entry and categorise changes by their update cost.
 */
struct LiveTweakableParameterView {
    std::string   parameter_name;                          ///< Must match ParameterDescriptor::name
    LiveTweakClass tweak_class{LiveTweakClass::DecodePhase}; ///< Update cost class
};

// =============================================================================
// Preview View Contract
// =============================================================================

/**
 * @brief Preview view contract used by the core registry.
 *
 * Implementations are bound to a specific node at construction time. The
 * request_data interface intentionally only accepts data type + coordinate so
 * callers can use a uniform contract across GUI and CLI.
 */
class IPreviewView {
public:
    virtual ~IPreviewView() = default;

    virtual std::vector<VideoDataType> supported_data_types() const = 0;

    virtual PreviewViewDataResult request_data(
        VideoDataType data_type,
        const PreviewCoordinate& coordinate) = 0;

    virtual PreviewViewExportResult export_as(
        const std::string& format,
        const std::string& path) const = 0;
};

} // namespace orc
