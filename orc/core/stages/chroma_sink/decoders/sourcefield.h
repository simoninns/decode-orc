/*
 * File:        sourcefield.h
 * Module:      orc-core
 * Purpose:     Source field container
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */



#ifndef SOURCEFIELD_H
#define SOURCEFIELD_H

#include "tbc_metadata.h"
#include <vector>
#include <cstdint>

// A field with metadata and data
// Data comes from VFR (VideoFieldRepresentation) in orc-core
struct SourceField {
    orc::FieldMetadata field;
    std::vector<uint16_t> data;

    // Return the vertical offset of this field within the interlaced frame
    // (i.e. 0 for the top field, 1 for the bottom field).
    int32_t getOffset() const {
        return (field.is_first_field.value_or(true)) ? 0 : 1;
    }

    // Return the first/last active line numbers within this field's data,
    // given the video parameters.
    int32_t getFirstActiveLine(const ::orc::VideoParameters &videoParameters) const {
        return (videoParameters.first_active_frame_line + 1 - getOffset()) / 2;
    }
    int32_t getLastActiveLine(const ::orc::VideoParameters &videoParameters) const {
        return (videoParameters.last_active_frame_line + 1 - getOffset()) / 2;
    }
};

#endif
