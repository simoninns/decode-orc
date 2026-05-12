/*
 * File:        efm_sink_stage_deps_interface.h
 * Module:      orc-core
 * Purpose:     Interface for RawEFMSinkStage dependencies
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#ifndef ORC_CORE_RAW_EFM_SINK_STAGE_DEPS_INTERFACE_H
#define ORC_CORE_RAW_EFM_SINK_STAGE_DEPS_INTERFACE_H

#include "video_field_representation.h"
#include "triggerable_stage.h"

#include <atomic>
#include <cstdint>
#include <string>

namespace orc
{
    struct RawEFMSinkWriteResult
    {
        bool success{false};
        uint64_t tvalues_written{0};
        std::string status_message;
    };

    class IRawEFMSinkStageDeps
    {
    public:
        virtual ~IRawEFMSinkStageDeps() = default;

        virtual void init(TriggerProgressCallback progress_callback, std::atomic<bool>* cancel_requested) = 0;

        virtual RawEFMSinkWriteResult write_raw_efm(
            const VideoFieldRepresentation* representation,
            const std::string& output_path) = 0;
    };
} // namespace orc

#endif // ORC_CORE_RAW_EFM_SINK_STAGE_DEPS_INTERFACE_H
