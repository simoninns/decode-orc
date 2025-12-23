/*
 * File:        framecanvas.cpp
 * Module:      orc-core
 * Purpose:     Frame canvas for comb filtering
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2018-2025 Simon Inns
 * SPDX-FileCopyrightText: 2021 Adam Sampson
 */


#include "framecanvas.h"

FrameCanvas::FrameCanvas(ComponentFrame &_componentFrame, const ::orc::VideoParameters &_videoParameters)
    : yData(_componentFrame.y(0)), uData(_componentFrame.u(0)), vData(_componentFrame.v(0)),
      width(_componentFrame.getWidth()), height(_componentFrame.getHeight()),
      ireRange(_videoParameters.white_16b_ire - _videoParameters.black_16b_ire), blackIre(_videoParameters.black_16b_ire),
      videoParameters(_videoParameters)
{
}

int32_t FrameCanvas::top()
{
    return videoParameters.first_active_frame_line;
}

int32_t FrameCanvas::bottom()
{
    return videoParameters.last_active_frame_line;
}

int32_t FrameCanvas::left()
{
    return videoParameters.active_video_start;
}

int32_t FrameCanvas::right()
{
    return videoParameters.active_video_end;
}

FrameCanvas::Colour FrameCanvas::rgb(uint16_t r, uint16_t g, uint16_t b)
{
    // Scale R'G'B' to match the IRE range
    const double sr = (r / 65535.0) * ireRange;
    const double sg = (g / 65535.0) * ireRange;
    const double sb = (b / 65535.0) * ireRange;

    // Convert to Y'UV form [Poynton eq 28.5 p337]
    return Colour {
        ((sr * 0.299)    + (sg * 0.587)     + (sb * 0.114))    + blackIre,
        (sr * -0.147141) + (sg * -0.288869) + (sb * 0.436010),
        (sr * 0.614975)  + (sg * -0.514965) + (sb * -0.100010)
    };
}

FrameCanvas::Colour FrameCanvas::grey(uint16_t value)
{
    // Scale Y to match the IRE range
    return Colour {((value / 65535.0) * ireRange) + blackIre, 0.0, 0.0};
}

void FrameCanvas::drawPoint(int32_t x, int32_t y, const Colour& colour)
{
    if (x < 0 || x >= width || y < 0 || y >= height) {
        // Outside the frame
        return;
    }

    const int32_t offset = (y * width) + x;
    yData[offset] = colour.y;
    uData[offset] = colour.u;
    vData[offset] = colour.v;
}

void FrameCanvas::drawRectangle(int32_t xStart, int32_t yStart, int32_t w, int32_t h, const Colour& colour)
{
    for (int32_t y = yStart; y < yStart + h; y++) {
        drawPoint(xStart, y, colour);
        drawPoint(xStart + w - 1, y, colour);
    }
    for (int32_t x = xStart + 1; x < xStart + w - 1; x++) {
        drawPoint(x, yStart, colour);
        drawPoint(x, yStart + h - 1, colour);
    }
}

void FrameCanvas::fillRectangle(int32_t xStart, int32_t yStart, int32_t w, int32_t h, const Colour& colour)
{
    for (int32_t y = yStart; y < yStart + h; y++) {
        for (int32_t x = xStart; x < xStart + w; x++) {
            drawPoint(x, y, colour);
        }
    }
}
