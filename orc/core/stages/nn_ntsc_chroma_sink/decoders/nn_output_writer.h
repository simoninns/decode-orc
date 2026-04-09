/*
 * File:        nn_output_writer.h
 * Module:      orc-core
 * Purpose:     Output format writer for NN NTSC chroma decoder
 *              (RGB48, YUV444P16, Y4M — no Qt I/O)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2019-2021 Adam Sampson
 * SPDX-FileCopyrightText: 2021 Phillip Blucas
 * SPDX-FileCopyrightText: 2026 Simon Inns (Modified for decode-orc)
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "nn_source_field.h"

namespace orc::nn_ntsc {

class ComponentFrame;

// A frame (two interlaced fields) converted to one of the supported output
// formats.  All currently supported formats use 16-bit samples.
using NnOutputFrame = std::vector<uint16_t>;

class NnOutputWriter
{
public:
    // Supported pixel formats
    enum PixelFormat {
        RGB48    = 0,
        YUV444P16,
        GRAY16
    };

    // Output settings
    struct Configuration {
        int32_t     paddingAmount = 8;
        PixelFormat pixelFormat  = RGB48;
        bool        outputY4m    = false;
    };

    // Configure the writer and optionally adjust NnVideoParameters to suit
    // the padding requirements.
    void updateConfiguration(NnVideoParameters      &videoParameters,
                              const Configuration    &config);

    // Print an informational message about the configured output format.
    void printOutputInfo() const;

    // Return the byte sequence that must be written once at the start of the
    // output stream (non-empty only for Y4M).
    std::string getStreamHeader() const;

    // Return the per-frame header bytes (non-empty only for Y4M).
    std::string getFrameHeader() const;

    // Convert a decoded component frame to the configured output format.
    void convert(const ComponentFrame &componentFrame,
                 NnOutputFrame        &outputFrame) const;

    PixelFormat getPixelFormat() const { return config.pixelFormat; }

private:
    Configuration     config;
    NnVideoParameters videoParameters;

    int32_t topPadLines    = 0;
    int32_t bottomPadLines = 0;
    int32_t activeWidth    = 0;
    int32_t activeHeight   = 0;
    int32_t outputHeight   = 0;

    const char *getPixelName() const;

    void clearPadLines(int32_t firstLine, int32_t numLines,
                       NnOutputFrame &outputFrame) const;

    void convertLine(int32_t lineNumber,
                     const ComponentFrame &componentFrame,
                     NnOutputFrame        &outputFrame) const;
};

}  // namespace orc::nn_ntsc
