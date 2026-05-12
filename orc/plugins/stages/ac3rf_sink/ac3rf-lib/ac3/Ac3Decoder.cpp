// Copyright 2025-2026 Staffan Ulfberg
// This file is licensed under the provisions of the Gnu General Public License v3 (see gpl-3.0.txt)

#include "Ac3Decoder.h"

Ac3Decoder::Ac3Decoder(Logger &log)
    : m_input_framer(log)
    , m_block_handler(log)
{}

std::vector<std::array<uint8_t, 1536>> Ac3Decoder::decodeSymbols(const std::vector<uint8_t> &symbols) {
    auto frames = m_input_framer.arrangeInFrames(symbols);

    std::vector<std::array<uint8_t, 1536>> result;
    for (auto frame: frames)
        if (auto block = m_block_handler.handleFrame(frame); block.has_value())
            if (auto correctedBlock = m_block_handler.errorCorrectBlock(block.value()); correctedBlock.has_value()) {
                auto output = m_block_handler.handleCorrectedBlock(correctedBlock.value());
                result.insert(result.end(), output.cbegin(), output.cend());
            }

    return result;
}

std::string Ac3Decoder::reedSolomonStatistics() {
    return m_block_handler.reedSolomonStatistics();
}
