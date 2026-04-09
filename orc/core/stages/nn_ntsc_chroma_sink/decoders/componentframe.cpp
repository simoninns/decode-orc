/*
 * File:        componentframe.cpp
 * Module:      orc-core
 * Purpose:     Decoded component frame (Y/U/V) for NN NTSC chroma decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2021 Adam Sampson
 * SPDX-FileCopyrightText: 2026 Simon Inns (Modified for decode-orc)
 */

#include "componentframe.h"

namespace orc::nn_ntsc {

ComponentFrame::ComponentFrame()
    : width(-1), height(-1)
{
}

void ComponentFrame::init(const NnVideoParameters &videoParameters, bool mono)
{
    width  = videoParameters.fieldWidth;
    height = (videoParameters.fieldHeight * 2) - 1;

    const int32_t size = width * height;

    yData.assign(size, 0.0);

    if (!mono) {
        uData.assign(size, 0.0);
        vData.assign(size, 0.0);
    } else {
        uData.clear();
        uData.shrink_to_fit();
        vData.clear();
        vData.shrink_to_fit();
    }
}

}  // namespace orc::nn_ntsc
