/************************************************************************

    framecanvas.h

    ld-chroma-decoder - Colourisation filter for ld-decode
    Copyright (C) 2019-2021 Adam Sampson

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

#ifndef FRAMECANVAS_H
#define FRAMECANVAS_H

#include <cstdint>
#include <vector>

#include "tbc_metadata.h"

#include "componentframe.h"

// Context for drawing on top of a Y'UV ComponentFrame.
class FrameCanvas {
public:
    // componentFrame is the frame to draw upon, and videoParameters gives its parameters.
    // (Both parameters are captured by reference, not copied.)
    FrameCanvas(ComponentFrame &componentFrame, const ::orc::VideoParameters &videoParameters);

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
    const ::orc::VideoParameters &videoParameters;
};

#endif
