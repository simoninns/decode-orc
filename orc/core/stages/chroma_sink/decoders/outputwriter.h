/************************************************************************

    outputwriter.h

    ld-chroma-decoder - Colourisation filter for ld-decode
    Copyright (C) 2019-2021 Adam Sampson
    Copyright (C) 2021 Phillip Blucas

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

#ifndef OUTPUTWRITER_H
#define OUTPUTWRITER_H

#include <cstdint>
#include <vector>
#include <string>

#include "tbc_metadata.h"

class ComponentFrame;

// A frame (two interlaced fields), converted to one of the supported output formats.
// Since all the formats currently supported use 16-bit samples, this is just a
// vector of 16-bit numbers.
using OutputFrame = std::vector<uint16_t>;

class OutputWriter {
public:
    // Output pixel formats
    enum PixelFormat {
        RGB48 = 0,
        YUV444P16,
        GRAY16
    };

    // Output settings
    struct Configuration {
        int32_t paddingAmount = 8;
        PixelFormat pixelFormat = RGB48;
        bool outputY4m = false;
    };

    // Set the output configuration, and adjust the VideoParameters to suit.
    // (If usePadding is disabled, this will not change the VideoParameters.)
    void updateConfiguration(::orc::VideoParameters &videoParameters, const Configuration &config);

    // Print an info message about the output format
    void printOutputInfo() const;

    // Get the header data to be written at the start of the stream
    std::string getStreamHeader() const;

    // Get the header data to be written before each frame
    std::string getFrameHeader() const;

    // For worker threads: convert a component frame to the configured output format
    void convert(const ComponentFrame &componentFrame, OutputFrame &outputFrame) const;

    PixelFormat getPixelFormat() const {
        return config.pixelFormat;
    }

private:
    // Configuration parameters
    Configuration config;
    ::orc::VideoParameters videoParameters;

    // Number of blank lines to add at the top and bottom of the output
    int32_t topPadLines;
    int32_t bottomPadLines;

    // Output size
    int32_t activeWidth;
    int32_t activeHeight;
    int32_t outputHeight;

    // Get a string representing the pixel format
    const char *getPixelName() const;

    // Clear padding lines
    void clearPadLines(int32_t firstLine, int32_t numLines, OutputFrame &outputFrame) const;

    // Convert one line
    void convertLine(int32_t lineNumber, const ComponentFrame &componentFrame, OutputFrame &outputFrame) const;
};

#endif // OUTPUTWRITER_H
