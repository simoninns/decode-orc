/************************************************************************

    paldecoder.cpp

    ld-chroma-decoder - Colourisation filter for ld-decode
    Copyright (C) 2018-2019 Simon Inns
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

#include "paldecoder.h"

#include "decoderpool.h"

PalDecoder::PalDecoder(const PalColour::Configuration &palConfig)
{
    config.pal = palConfig;
}

bool PalDecoder::configure(const LdDecodeMetaData::VideoParameters &videoParameters) {
    // Ensure the source video is PAL
    if (videoParameters.system != PAL && videoParameters.system != PAL_M) {
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

std::thread PalDecoder::makeThread(std::atomic<bool>& abort, DecoderPool& decoderPool) {
    return std::thread(&PalThread::run, PalThread(abort, decoderPool, config));
}

PalThread::PalThread(std::atomic<bool>& _abort, DecoderPool& _decoderPool,
                     const PalDecoder::Configuration &_config)
    : DecoderThread(_abort, _decoderPool), config(_config)
{
    // Configure PALcolour
    palColour.updateConfiguration(config.videoParameters, config.pal);
}

void PalThread::decodeFrames(const std::vector<SourceField> &inputFields, int32_t startIndex, int32_t endIndex,
                             std::vector<ComponentFrame> &componentFrames)
{
    palColour.decodeFrames(inputFields, startIndex, endIndex, componentFrames);
}
