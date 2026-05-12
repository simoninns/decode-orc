/*
 * File:        paldecoder.cpp
 * Module:      orc-core
 * Purpose:     PAL decoder wrapper
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2018-2026 Simon Inns
 * SPDX-FileCopyrightText: 2019-2020 Adam Sampson
 */


#include "paldecoder.h"
#include "logging.h"
#include "../video_parameter_safety.h"


PalDecoder::PalDecoder(const PalColour::Configuration &palConfig)
{
    config.pal = palConfig;
}

bool PalDecoder::configure(const ::orc::SourceParameters &videoParameters) {
    // Ensure the source video is PAL
    if (videoParameters.system != orc::VideoSystem::PAL && videoParameters.system != orc::VideoSystem::PAL_M) {
        ORC_LOG_ERROR("This decoder is for PAL video sources only");
        return false;
    }

    const auto safety = ::orc::chroma_sink::sanitize_video_parameters(
        videoParameters,
        ::orc::chroma_sink::DecoderVideoProfile::PalColour);

    if (!safety.warnings.empty()) {
        ORC_LOG_WARN("PalDecoder::configure(): Adjusted unsafe video parameters: {}",
                     ::orc::chroma_sink::join_issues(safety.warnings));
    }

    if (!safety.ok) {
        ORC_LOG_ERROR("PalDecoder::configure(): Invalid video parameters: {}",
                      ::orc::chroma_sink::join_issues(safety.errors));
        return false;
    }

    config.videoParameters = safety.params;

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



