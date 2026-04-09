/*
 * File:        ntscdecoder.cpp
 * Module:      orc-core
 * Purpose:     NTSC decoder wrapper for NN chroma decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2018 Chad Page
 * SPDX-FileCopyrightText: 2018-2019 Simon Inns
 * SPDX-FileCopyrightText: 2019-2021 Adam Sampson
 * SPDX-FileCopyrightText: 2026 Simon Inns (Modified for decode-orc)
 */

#include "ntscdecoder.h"
#include "logging.h"

namespace orc::nn_ntsc {

NtscDecoder::NtscDecoder(const Comb::Configuration &combConfig)
{
    config.combConfig = combConfig;
}

bool NtscDecoder::configure(const NnVideoParameters &videoParameters)
{
    config.videoParameters = videoParameters;
    comb.updateConfiguration(videoParameters, config.combConfig);
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

void NtscDecoder::decodeFrames(const std::vector<NnSourceField> &inputFields,
                                int32_t startIndex, int32_t endIndex,
                                std::vector<ComponentFrame>      &componentFrames)
{
    comb.decodeFrames(inputFields, startIndex, endIndex, componentFrames);
}

}  // namespace orc::nn_ntsc
