/*
 * File:        ntscdecoder.h
 * Module:      orc-core
 * Purpose:     NTSC decoder wrapper for NN chroma decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2018 Chad Page
 * SPDX-FileCopyrightText: 2018-2019 Simon Inns
 * SPDX-FileCopyrightText: 2019-2021 Adam Sampson
 * SPDX-FileCopyrightText: 2026 Simon Inns (Modified for decode-orc)
 */

#pragma once

#include "comb.h"
#include "decoder.h"
#include "nn_source_field.h"

namespace orc::nn_ntsc {

// 3D NTSC decoder using the NN-enhanced Comb filter.
// NnNtscChromaSinkStage calls configure() then decodeFrames() directly
// in its trigger loop — no DecoderPool or Qt threading required.
class NtscDecoder : public Decoder
{
public:
    explicit NtscDecoder(const Comb::Configuration &combConfig);

    bool    configure(const NnVideoParameters &videoParameters) override;
    int32_t getLookBehind() const override;
    int32_t getLookAhead()  const override;

    void decodeFrames(const std::vector<NnSourceField> &inputFields,
                      int32_t startIndex, int32_t endIndex,
                      std::vector<ComponentFrame>      &componentFrames) override;

    struct Configuration : public Decoder::Configuration {
        Comb::Configuration combConfig;
    };

private:
    Configuration config;
    Comb          comb;
};

}  // namespace orc::nn_ntsc
