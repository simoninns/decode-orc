/************************************************************************

    ntscdecoder.cpp

    ld-chroma-decoder - Colourisation filter for ld-decode
    Copyright (C) 2018 Chad Page
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

#include "ntscdecoder.h"


NtscDecoder::NtscDecoder(const Comb::Configuration &combConfig)
{
    config.combConfig = combConfig;
}

bool NtscDecoder::configure(const ::orc::VideoParameters &videoParameters) {
    // Ensure the source video is NTSC
    if (videoParameters.system != orc::VideoSystem::NTSC) {
        std::cerr << "ERROR: This decoder is for NTSC video sources only" << std::endl;
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



