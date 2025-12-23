/************************************************************************

    ntscdecoder.h

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

#ifndef NTSCDECODER_H
#define NTSCDECODER_H

#include <atomic>
#include <thread>
#include <iostream>

#include "componentframe.h"
#include "tbc_metadata.h"

#include "comb.h"
#include "decoder.h"
#include "sourcefield.h"


// 2D/3D NTSC decoder using Comb
class NtscDecoder : public Decoder {
public:
    NtscDecoder(const Comb::Configuration &combConfig);
    bool configure(const ::orc::VideoParameters &videoParameters) override;
    int32_t getLookBehind() const override;
    int32_t getLookAhead() const override;

    // Parameters used by NtscDecoder and NtscThread
    struct Configuration : public Decoder::Configuration {
        Comb::Configuration combConfig;
    };

private:
    Configuration config;
};


#endif // NTSCDECODER_H
