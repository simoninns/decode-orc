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

#include "lddecodemetadata.h"
#include "sourcevideo.h"

// A field read from the input, with metadata and data
struct SourceField {
    LdDecodeMetaData::Field field;
    SourceVideo::Data data;

    // Load a sequence of frames from the input files.
    //
    // fields will contain {lookbehind fields... [startIndex] real fields... [endIndex] lookahead fields...}.
    // Fields requested outside the bounds of the file will have dummy metadata and black data.
    static void loadFields(SourceVideo &sourceVideo, LdDecodeMetaData &ldDecodeMetaData,
                           int32_t firstFrameNumber, int32_t numFrames,
                           int32_t lookBehindFrames, int32_t lookAheadFrames,
                           std::vector<SourceField> &fields, int32_t &startIndex, int32_t &endIndex);

    // Return the vertical offset of this field within the interlaced frame
    // (i.e. 0 for the top field, 1 for the bottom field).
    int32_t getOffset() const {
        return field.isFirstField ? 0 : 1;
    }

    // Return the first/last active line numbers within this field's data,
    // given the video parameters.
    int32_t getFirstActiveLine(const LdDecodeMetaData::VideoParameters &videoParameters) const {
        return (videoParameters.firstActiveFrameLine + 1 - getOffset()) / 2;
    }
    int32_t getLastActiveLine(const LdDecodeMetaData::VideoParameters &videoParameters) const {
        return (videoParameters.lastActiveFrameLine + 1 - getOffset()) / 2;
    }
};

#endif
