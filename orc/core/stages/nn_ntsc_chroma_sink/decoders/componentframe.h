/*
 * File:        componentframe.h
 * Module:      orc-core
 * Purpose:     Decoded component frame (Y/U/V) for NN NTSC chroma decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2021 Adam Sampson
 * SPDX-FileCopyrightText: 2026 Simon Inns (Modified for decode-orc)
 */

#pragma once

#include <cassert>
#include <cstdint>
#include <vector>

#include "nn_source_field.h"

namespace orc::nn_ntsc {

// Two complete interlaced fields' worth of decoded luma and chroma information.
//
// The luma and chroma samples have the same scaling as in the original
// composite signal (i.e. they're not in Y'CbCr form yet). You can recover
// the chroma signal by subtracting Y from the composite signal.
class ComponentFrame
{
public:
    ComponentFrame();

    // Set the frame's size and clear it to black.
    // If mono is true, only Y is set to black; U and V are cleared/freed.
    void init(const NnVideoParameters &videoParameters, bool mono = false);

    // Get a pointer to a line of samples. Line numbers are 0-based within the
    // frame.  Lines are stored in a contiguous array, so it is safe to get a
    // pointer to line 0 and read through later lines.
    double *y(int32_t line) {
        return yData.data() + getLineOffset(line);
    }
    double *u(int32_t line) {
        return uData.data() + getLineOffset(line);
    }
    double *v(int32_t line) {
        return vData.data() + getLineOffset(line);
    }
    const double *y(int32_t line) const {
        return yData.data() + getLineOffset(line);
    }
    const double *u(int32_t line) const {
        return uData.data() + getLineOffset(line);
    }
    const double *v(int32_t line) const {
        return vData.data() + getLineOffset(line);
    }

    int32_t getWidth()  const { return width;  }
    int32_t getHeight() const { return height; }

private:
    int32_t getLineOffset(int32_t line) const {
        assert(line >= 0 && line < height);
        return line * width;
    }

    int32_t width  = -1;
    int32_t height = -1;

    std::vector<double> yData;
    std::vector<double> uData;
    std::vector<double> vData;
};

}  // namespace orc::nn_ntsc
