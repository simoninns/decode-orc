/*
 * File:        decoder.h
 * Module:      orc-core
 * Purpose:     Abstract base decoder interface for NN NTSC chroma decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2019-2021 Adam Sampson
 * SPDX-FileCopyrightText: 2026 Simon Inns (Modified for decode-orc)
 */

#pragma once

#include <cstdint>
#include <vector>

#include "componentframe.h"
#include "nn_source_field.h"

namespace orc::nn_ntsc {

// Abstract base class for NN NTSC chroma decoders.
//
// NnNtscChromaSinkStage calls configure() then decodeFrames() directly in its
// trigger loop; there is no DecoderPool threading layer — the stage manages
// concurrency at a higher level.
class Decoder
{
public:
    virtual ~Decoder() = default;

    // Configure the decoder given input video parameters.
    // Returns false if the video is incompatible.
    virtual bool configure(const NnVideoParameters &videoParameters) = 0;

    // Return the number of extra frames of look-behind context required.
    // Default: 0 (suitable for 1D/2D decoders).
    virtual int32_t getLookBehind() const;

    // Return the number of extra frames of look-ahead context required.
    // Default: 0 (suitable for 1D/2D decoders).
    virtual int32_t getLookAhead() const;

    // Decode a window of fields into component frames.
    // inputFields covers [startIndex – lookBehind … endIndex + lookAhead].
    virtual void decodeFrames(const std::vector<NnSourceField> &inputFields,
                               int32_t startIndex, int32_t endIndex,
                               std::vector<ComponentFrame> &componentFrames) = 0;

    // Base parameters shared by all concrete decoder subclasses.
    struct Configuration {
        NnVideoParameters videoParameters;
    };
};

}  // namespace orc::nn_ntsc
