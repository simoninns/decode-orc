/*
 * File:        daphne_vbi_sink_stage_deps_interface.h
 * Module:      orc-core
 * Purpose:     Interface for 'Generate .VBI binary files, dependencies'
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Matt Ownby
 */

#ifndef DECODE_ORC_ROOT_DAPHNE_VBI_SINK_STAGE_DEPS_INTERFACE_H
#define DECODE_ORC_ROOT_DAPHNE_VBI_SINK_STAGE_DEPS_INTERFACE_H

#include "observation_context_interface.h"
#include "video_field_representation.h"

namespace orc
{
    class IDaphneVBISinkStageDeps
    {
    public:
        virtual ~IDaphneVBISinkStageDeps() = default;

        virtual bool write_vbi(
            const VideoFieldRepresentation* representation,
            const std::string& vbi_path,
            IObservationContext *pObservationContext) = 0;
    };
}

#endif //DECODE_ORC_ROOT_DAPHNE_VBI_SINK_STAGE_DEPS_INTERFACE_H