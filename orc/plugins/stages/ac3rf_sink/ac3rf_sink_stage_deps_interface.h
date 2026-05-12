/*
 * File:        ac3rf_sink_stage_deps_interface.h
 * Module:      orc-core
 * Purpose:     Interface for AC3RFSinkStage dependencies
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#ifndef ORC_CORE_AC3RF_SINK_STAGE_DEPS_INTERFACE_H
#define ORC_CORE_AC3RF_SINK_STAGE_DEPS_INTERFACE_H

#include "video_field_representation.h"
#include <cstdint>
#include <string>

namespace orc
{
    struct AC3RFSinkDecodeResult
    {
        bool success{false};
        uint64_t frames_written{0};
        std::string status_message;
    };

    class IAC3RFSinkStageDeps
    {
    public:
        virtual ~IAC3RFSinkStageDeps() = default;

        virtual AC3RFSinkDecodeResult decode_and_write_ac3(
            const VideoFieldRepresentation* representation,
            const std::string& output_path) = 0;
    };
} // namespace orc

#endif // ORC_CORE_AC3RF_SINK_STAGE_DEPS_INTERFACE_H
