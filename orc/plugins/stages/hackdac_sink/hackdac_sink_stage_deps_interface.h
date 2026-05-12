/*
 * File:        hackdac_sink_stage_deps_interface.h
 * Module:      orc-core
 * Purpose:     Interface for HackdacSinkStage dependencies
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#ifndef ORC_CORE_HACKDAC_SINK_STAGE_DEPS_INTERFACE_H
#define ORC_CORE_HACKDAC_SINK_STAGE_DEPS_INTERFACE_H

#include "video_field_representation.h"
#include "triggerable_stage.h"

#include <atomic>
#include <optional>
#include <string>

namespace orc
{
    struct HackdacSinkExportOptions
    {
        std::string output_path;
        std::string report_path;
    };

    struct HackdacSinkExportResult
    {
        bool success{false};
        size_t fields_exported{0};
        std::string status_message;
    };

    class IHackdacSinkStageDeps
    {
    public:
        virtual ~IHackdacSinkStageDeps() = default;

        virtual void init(TriggerProgressCallback progress_callback, std::atomic<bool>* cancel_requested) = 0;

        virtual HackdacSinkExportResult export_hackdac(
            const VideoFieldRepresentation* representation,
            const HackdacSinkExportOptions& options) = 0;
    };
} // namespace orc

#endif // ORC_CORE_HACKDAC_SINK_STAGE_DEPS_INTERFACE_H
