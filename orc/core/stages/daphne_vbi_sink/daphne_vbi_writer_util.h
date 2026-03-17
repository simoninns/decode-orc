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
#include "daphne_vbi_writer_util_interface.h"
#include "logging.h"

namespace orc
{

/**
* @brief Writer utilities for .VBI binary files ( see https://www.daphne-emu.com:9443/mediawiki/index.php/VBIInfo )
 *
 * Writes the 4-byte header and 10-byte VBI entries for each field.
 */
class DaphneVBIWriterUtil : public IDaphneVBIWriterUtil
{
public:
    DaphneVBIWriterUtil() = default;
    ~DaphneVBIWriterUtil() override = default;

	/**
	 * @brief Sets dependencies that aren't interfaces.
	 *
	 * @param pWriter The file writer to be used by this object
	 */
    void init(IFileWriter<uint8_t> *pWriter);

    void write_header() const override;
    void write_observations(FieldID field_id, const IObservationContext &context) const override;

private:
    IFileWriter<uint8_t> *pWriter_ = nullptr;
};

}

#endif //DECODE_ORC_ROOT_VBI_WRITER_UTIL_H