// Copyright 2025-2026 Staffan Ulfberg
// This file is licensed under the provisions of the Gnu General Public License v3 (see gpl-3.0.txt)

#ifndef AC3RF_DECODE_AC3BLOCKHANDLER_H
#define AC3RF_DECODE_AC3BLOCKHANDLER_H

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "../rs/ReedSolomon.h"
#include "Ac3InputFraming.h"

class Logger;

class Ac3BlockHandler {
public:
    Ac3BlockHandler(Logger &log);
    ~Ac3BlockHandler() = default;
    Ac3BlockHandler(const Ac3BlockHandler &) = delete;
    Ac3BlockHandler(Ac3BlockHandler &&) = delete;
    Ac3BlockHandler &operator=(const Ac3BlockHandler &) = delete;
    Ac3BlockHandler &operator=(Ac3BlockHandler &&) = delete;

    // A block of 72 consecutive frames starting with sequence number 0.
    struct UncorrectedBlock {
        size_t global_symbol_index;
        std::array<std::array<uint8_t, 37>, 72> block_data;
    };

    // After error correction we have 66 words of length 32
    struct CorrectedBlock {
        size_t global_symbol_index;
        std::array<std::array<uint8_t, 32>, 66> block_data;
    };

    // Keeps track of received frames and returns a block of 72 frames when they have been collected
    std::optional<UncorrectedBlock> handleFrame(const Ac3InputFraming::NumberedFrame &frame);

    // Runs Reed Solomon error correction on a single block, including de-interleaving
    // This method has no state, but cannot be static since it uses the RS decoders and also the logger.
    std::optional<CorrectedBlock> errorCorrectBlock(const UncorrectedBlock &block);

    // Parses corrected blocks to find the AC3 preample and returns data bursts.
    std::vector<std::array<uint8_t, 1536>> handleCorrectedBlock(const CorrectedBlock &block);

    [[nodiscard]] std::string reedSolomonStatistics();

private:
    Logger &m_log;
    ReedSolomon<0x187, 2> m_c1;
    ReedSolomon<0x187, 2> m_c2;

    // handleFrame state
    UncorrectedBlock m_current_block;
    int m_expected_seq = 0;
    int m_consecutive_in_sequence = 0;

    // handleCorrectedBlock state
    size_t m_last_burst_symbol_index = -1;
    size_t m_first_symbol_in_burst = -1;
    int m_ac3_output_block_index = 0;
    std::array<uint8_t, 1536> m_ac3_output_block;
    bool m_logged_incorrect_start = false;
};


#endif //AC3RF_DECODE_AC3BLOCKHANDLER_H