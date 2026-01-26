/*
 * File:        framecanvas.h
 * Module:      orc-core
 * Purpose:     Frame canvas for comb filtering
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2018-2026 Simon Inns
 * SPDX-FileCopyrightText: 2021 Adam Sampson
 */


#ifndef FRAMECANVAS_H
#define FRAMECANVAS_H

#include <cstdint>
#include <vector>

#include <orc_source_parameters.h>

#include "componentframe.h"

// Context for drawing on top of a Y'UV ComponentFrame.
class FrameCanvas {
public:
    // componentFrame is the frame to draw upon, and videoParameters gives its parameters.
    // (Both parameters are captured by reference, not copied.)
    FrameCanvas(ComponentFrame &componentFrame, const ::orc::SourceParameters &videoParameters);

    // Return the edges of the active area.
    int32_t top();
    int32_t bottom();
    int32_t left();
    int32_t right();

    // Colour representation
    struct Colour {
        double y, u, v;
    };

    // Convert a 16-bit R'G'B' colour to Colour form
    Colour rgb(uint16_t r, uint16_t g, uint16_t b);

    // Convert a 16-bit greyscale value to Colour form
    Colour grey(uint16_t value);

    // Plot a pixel
    void drawPoint(int32_t x, int32_t y, const Colour& colour);

    // Draw an empty rectangle
    void drawRectangle(int32_t x, int32_t y, int32_t w, int32_t h, const Colour& colour);

    // Draw a filled rectangle
    void fillRectangle(int32_t x, int32_t y, int32_t w, int32_t h, const Colour& colour);

private:
    double *yData, *uData, *vData;
    int32_t width, height;
    double ireRange, blackIre;
    const ::orc::SourceParameters &videoParameters;
};

#endif
