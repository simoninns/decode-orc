/*
 * File:        daphne_vbi_writer_util_interface.h
 * Module:      orc-core
 * Purpose:     VBI Writer Util interface
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Matt Ownby
 */

#ifndef DECODE_ORC_ROOT_DAPHNE_VBI_WRITER_UTIL_INTERFACE_H
#define DECODE_ORC_ROOT_DAPHNE_VBI_WRITER_UTIL_INTERFACE_H

#include "field_id.h"
#include "observation_context.h"
#include "file_io_interface.h"

namespace orc
{
    /**
    * @brief Writer utilities for .VBI binary files ( see https://www.daphne-emu.com:9443/mediawiki/index.php/VBIInfo )
     *
     * Writes the 4-byte header and 10-byte VBI entries for each field.
     */
    class IDaphneVBIWriterUtil
    {
    public:
        virtual ~IDaphneVBIWriterUtil() = default;

        /**
         * @brief Sets file writer to be used by this object. This must be called at least once.
         *
         * It would be safer to always pass this value into every method that needs it, but that is slightly less performant.
         * So to improve performance, we'll only set it once via this method.
         *
         * We could pass it into the concrete constructor, but then that would clutter up the IStageFactories interface.
         *
         * @param pWriter The file writer to be used by this object
         */
        virtual void set_writer(IFileWriter<uint8_t> *pWriter) = 0;

        virtual void write_header() const = 0;
        virtual void write_observations(FieldID field_id, const IObservationContext *pContext) const = 0;
    };

}

#endif //DECODE_ORC_ROOT_DAPHNE_VBI_WRITER_UTIL_INTERFACE_H