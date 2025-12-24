/*
 * File:        previewable_sink.h
 * Module:      orc-core
 * Purpose:     Interface for sink stages that support preview rendering
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#pragma once

#include "video_field_representation.h"
#include "field_id.h"
#include <memory>

namespace orc {

/**
 * @brief Interface for sink stages that can render previews
 * 
 * TEMPORARILY DISABLED - Sink previews are disabled while we refactor
 * the preview system for sources and transforms.
 */
class PreviewableSink {
public:
    virtual ~PreviewableSink() = default;
    
    /**
     * @brief Check if this sink supports preview rendering
     * 
     * @return Always false - sink previews are temporarily disabled
     */
    virtual bool supports_preview() const { return false; }
};

} // namespace orc
