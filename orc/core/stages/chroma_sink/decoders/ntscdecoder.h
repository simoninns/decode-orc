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

#include <QObject>
#include <atomic>
#include <thread>
#include <QDebug>

#include "componentframe.h"
#include "lddecodemetadata.h"
#include "sourcevideo.h"

#include "comb.h"
#include "decoder.h"
#include "sourcefield.h"

class DecoderPool;

// 2D/3D NTSC decoder using Comb
class NtscDecoder : public Decoder {
public:
    NtscDecoder(const Comb::Configuration &combConfig);
    bool configure(const LdDecodeMetaData::VideoParameters &videoParameters) override;
    int32_t getLookBehind() const override;
    int32_t getLookAhead() const override;
    std::thread makeThread(std::atomic<bool>& abort, DecoderPool& decoderPool) override;

    // Parameters used by NtscDecoder and NtscThread
    struct Configuration : public Decoder::Configuration {
        Comb::Configuration combConfig;
    };

private:
    Configuration config;
};

class NtscThread : public DecoderThread
{
public:
    explicit NtscThread(std::atomic<bool> &abort, DecoderPool &decoderPool,
                        const NtscDecoder::Configuration &config);

protected:
    void decodeFrames(const std::vector<SourceField> &inputFields, int32_t startIndex, int32_t endIndex,
                      std::vector<ComponentFrame> &componentFrames) override;

private:
    // Settings
    const NtscDecoder::Configuration &config;

    // NTSC decoder
    Comb comb;
};

#endif // NTSCDECODER_H
