/*
 * File:        ntscdecoder.h
 * Module:      orc-core
 * Purpose:     NTSC decoder wrapper
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2018-2026 Simon Inns
 * SPDX-FileCopyrightText: 2019-2020 Adam Sampson
 */


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
