/*
 * File:        ntscdecoder.cpp
 * Module:      orc-core
 * Purpose:     NTSC decoder wrapper
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2018-2026 Simon Inns
 * SPDX-FileCopyrightText: 2019-2020 Adam Sampson
 */


#include "ntscdecoder.h"
#include "logging.h"


NtscDecoder::NtscDecoder(const Comb::Configuration &combConfig)
{
    config.combConfig = combConfig;
}

bool NtscDecoder::configure(const ::orc::VideoParameters &videoParameters) {
    // Ensure the source video is NTSC
    if (videoParameters.system != orc::VideoSystem::NTSC) {
        ORC_LOG_ERROR("This decoder is for NTSC video sources only");
        return false;
    }

    config.videoParameters = videoParameters;

    return true;
}

int32_t NtscDecoder::getLookBehind() const
{
    return config.combConfig.getLookBehind();
}

int32_t NtscDecoder::getLookAhead() const
{
    return config.combConfig.getLookAhead();
}



