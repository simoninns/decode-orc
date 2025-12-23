/************************************************************************

    sourcefield.h

    ld-chroma-decoder - Colourisation filter for ld-decode
    Copyright (C) 2019 Adam Sampson

    This file is part of ld-decode-tools.

    ld-chroma-decoder is free software: you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

************************************************************************/

#ifndef SOURCEFIELD_H
#define SOURCEFIELD_H

#include "tbc_metadata.h"
#include <QVector>
#include <cstdint>

// A field with metadata and data
// Data comes from VFR (VideoFieldRepresentation) in orc-core
struct SourceField {
    orc::FieldMetadata field;
    QVector<quint16> data;

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
