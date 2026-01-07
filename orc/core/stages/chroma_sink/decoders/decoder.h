/*
 * File:        decoder.h
 * Module:      orc-core
 * Purpose:     Base decoder interface
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2018-2026 Simon Inns
 * SPDX-FileCopyrightText: 2019-2020 Adam Sampson
 */


#ifndef DECODER_H
#define DECODER_H

#include <vector>
#include <cstdint>
#include <atomic>
#include <thread>
#include <iostream>
#include <cassert>

#include "tbc_metadata.h"

#include "componentframe.h"
#include "outputwriter.h"
#include "sourcefield.h"

// Abstract base class for chroma decoders.
//
// ChromaSinkStage creates decoder instances and calls:
// 1. configure() with video parameters
// 2. getLookBehind()/getLookAhead() to determine field context needed
// 3. decodeFrames() to decode fields into component frames
//
// For multi-threading, ChromaSinkStage creates multiple decoder instances
// (one per worker thread), each operating independently.
class Decoder {
public:
    virtual ~Decoder() = default;

    // Configure the decoder given input video parameters.
    // If the video is not compatible, print an error message and return false.
    virtual bool configure(const ::orc::VideoParameters &videoParameters) = 0;

    // After configuration, return the number of frames that the decoder needs
    // to be able to see into the past (each frame being two SourceFields).
    // The default implementation returns 0, which is appropriate for 1D/2D decoders.
    virtual int32_t getLookBehind() const;

    // After configuration, return the number of frames that the decoder needs
    // to be able to see into the future (each frame being two SourceFields).
    // The default implementation returns 0, which is appropriate for 1D/2D decoders.
    virtual int32_t getLookAhead() const;

    // Decode a sequence of composite fields into a sequence of component frames
    virtual void decodeFrames(const std::vector<SourceField> &inputFields, int32_t startIndex, int32_t endIndex,
                              std::vector<ComponentFrame> &componentFrames) = 0;

    // Parameters used by the decoder and its threads.
    // This may be subclassed by decoders to add extra parameters.
    struct Configuration {
        ::orc::VideoParameters videoParameters;
    };
};

#endif
