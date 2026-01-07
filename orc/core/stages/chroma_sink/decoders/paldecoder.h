/*
 * File:        paldecoder.h
 * Module:      orc-core
 * Purpose:     PAL decoder wrapper
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2018-2026 Simon Inns
 * SPDX-FileCopyrightText: 2019-2020 Adam Sampson
 */


#ifndef PALDECODER_H
#define PALDECODER_H

#include <atomic>
#include <thread>
#include <iostream>

#include "componentframe.h"
#include "tbc_metadata.h"

#include "decoder.h"
#include "palcolour.h"
#include "sourcefield.h"


// 2D PAL decoder using PALcolour
class PalDecoder : public Decoder {
public:
    PalDecoder(const PalColour::Configuration &palConfig);
    bool configure(const ::orc::VideoParameters &videoParameters) override;
    int32_t getLookBehind() const override;
    int32_t getLookAhead() const override;

    // Parameters used by PalDecoder and PalThread
    struct Configuration : public Decoder::Configuration {
        PalColour::Configuration pal;
    };

private:
    Configuration config;
};


#endif // PALDECODER
