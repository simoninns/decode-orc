/*
 * File:        daphne_vbi_writer_util.cpp
 * Module:      orc-core
 * Purpose:     VBI Writer Util implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Matt Ownby
 */

#include "daphne_vbi_writer_util.h"
#include "video_metadata_types.h"

namespace orc
{

void DaphneVBIWriterUtil::write_header() const
{
    const std::vector<uint8_t> header = { '1', 'V', 'B', 'I' };
    pWriter_->write(header);
}

void DaphneVBIWriterUtil::write_observations(FieldID field_id,
                                        const IObservationContext *pContext) const
{
    VbiData vbi;
    bool whiteFlagPresent = false;
    uint8_t line_buf[10];  // each entry is 10-bytes in size

    // grab whiteflag data
    auto whiteflag_obs = pContext->get(field_id, "white_flag", "present");
    if (whiteflag_obs && std::holds_alternative<bool>(*whiteflag_obs))
    {
        whiteFlagPresent = std::get<bool>(*whiteflag_obs);
    }

    // Extract and write VBI data (from BiphaseObserver)
    // The biphase observer stores the raw VBI words in "biphase" namespace
    auto vbi0_obs = pContext->get(field_id, "biphase", "vbi_line_16");
    auto vbi1_obs = pContext->get(field_id, "biphase", "vbi_line_17");
    auto vbi2_obs = pContext->get(field_id, "biphase", "vbi_line_18");

    if (vbi0_obs && vbi1_obs && vbi2_obs &&
    std::holds_alternative<int32_t>(*vbi0_obs) &&
    std::holds_alternative<int32_t>(*vbi1_obs) &&
    std::holds_alternative<int32_t>(*vbi2_obs)) {
        vbi.in_use = true;
        vbi.vbi_data[0] = std::get<int32_t>(*vbi0_obs);
        vbi.vbi_data[1] = std::get<int32_t>(*vbi1_obs);
        vbi.vbi_data[2] = std::get<int32_t>(*vbi2_obs);
    }

    int idxEntry = 0;
    line_buf[idxEntry++] = whiteFlagPresent ? 1 : 0;

    for (int i = 0; i < 3; i++)
    {
        // Line value to be stored in big endian format.
        // Unparsable lines to be stored as 0x7FFFFF.

        if (vbi.vbi_data[i] != -1)
        {
            line_buf[idxEntry++] = (vbi.vbi_data[i] >> 16) & 0xFF;
            line_buf[idxEntry++] = (vbi.vbi_data[i] >> 8) & 0xFF;
            line_buf[idxEntry++] = (vbi.vbi_data[i] & 0xFF);
        }
        else
        {
            line_buf[idxEntry++] = 0x7F;
            line_buf[idxEntry++] = 0xFF;
            line_buf[idxEntry++] = 0xFF;
        }
    }

    pWriter_->write(line_buf, sizeof(line_buf));
}

}
