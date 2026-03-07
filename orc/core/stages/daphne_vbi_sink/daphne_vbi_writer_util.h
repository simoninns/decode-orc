/*
 * File:        daphne_vbi_writer_util.h
 * Module:      orc-core
 * Purpose:     VBI Writer Util implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Matt Ownby
 */

#ifndef DECODE_ORC_ROOT_VBI_WRITER_UTIL_H
#define DECODE_ORC_ROOT_VBI_WRITER_UTIL_H

#include "buffered_file_io.h"
#include "field_id.h"
#include "observation_context.h"

namespace orc
{

/**
* @brief Writer utilities for .VBI binary files ( see https://www.daphne-emu.com:9443/mediawiki/index.php/VBIInfo )
 *
 * Writes the 4-byte header and 10-byte VBI entries for each field.
 */
class DaphneVBIWriterUtil
{
public:
    DaphneVBIWriterUtil(BufferedFileWriter<uint8_t>& writer) : writer_(writer) {}
    ~DaphneVBIWriterUtil() = default;

    void write_header() const;
    void write_observations(FieldID field_id, const ObservationContext& context) const;

private:
    BufferedFileWriter<uint8_t>& writer_;
};

}

#endif //DECODE_ORC_ROOT_VBI_WRITER_UTIL_H