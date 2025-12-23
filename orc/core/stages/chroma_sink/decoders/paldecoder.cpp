/*
 * File:        paldecoder.cpp
 * Module:      orc-core
 * Purpose:     PAL decoder wrapper
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2018-2025 Simon Inns
 * SPDX-FileCopyrightText: 2019-2020 Adam Sampson
 */


#include "paldecoder.h"


PalDecoder::PalDecoder(const PalColour::Configuration &palConfig)
{
    config.pal = palConfig;
}

bool PalDecoder::configure(const ::orc::VideoParameters &videoParameters) {
    // Ensure the source video is PAL
    if (videoParameters.system != orc::VideoSystem::PAL && videoParameters.system != orc::VideoSystem::PAL_M) {
        std::cerr << "ERROR: This decoder is for PAL video sources only" << std::endl;
        return false;
    }

    config.videoParameters = videoParameters;

    return true;
}

int32_t PalDecoder::getLookBehind() const
{
    return config.pal.getLookBehind();
}

int32_t PalDecoder::getLookAhead() const
{
    return config.pal.getLookAhead();
}



