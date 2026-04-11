// Copyright 2025-2026 Staffan Ulfberg
// This file is licensed under the provisions of the Gnu General Public License v3 (see gpl-3.0.txt)

#ifndef AC3RF_DECODE_AC3DECODER_H
#define AC3RF_DECODE_AC3DECODER_H

#include <array>
#include <string>
#include <vector>

#include "../Logger.h"
#include "Ac3BlockHandler.h"
#include "Ac3InputFraming.h"

/*
 * Digital processing for AC3-RF: framing, de-interleaving, Reed-Solomon.
*
* Takes raw QPSK symbols (as produced by Ac3RfDemodulator::demodulateToSymbols)
* and returns decoded AC3 audio bursts.
*/
class Ac3Decoder {
public:
    explicit Ac3Decoder(Logger &log);
    ~Ac3Decoder() = default;

    Ac3Decoder(const Ac3Decoder &) = delete;
    Ac3Decoder &operator=(const Ac3Decoder &) = delete;
    Ac3Decoder(Ac3Decoder &&) = delete;
    Ac3Decoder &operator=(Ac3Decoder &&) = delete;

    std::vector<std::array<uint8_t, 1536>> decodeSymbols(const std::vector<uint8_t> &symbols);

    std::string reedSolomonStatistics();

private:
    Ac3InputFraming m_input_framer;
    Ac3BlockHandler m_block_handler;
};

#endif //AC3RF_DECODE_AC3DECODER_H
