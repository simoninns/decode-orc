// Copyright 2025-2026 Staffan Ulfberg
// This file is licensed under the provisions of the Gnu General Public License v3 (see gpl-3.0.txt)

#include "../rs/ByteWithErasureFlag.h"
#include "../rs/ReedSolomon.h"
#include "../Logger.h"
#include "Ac3BlockHandler.h"
#include "Ac3InputFraming.h"

Ac3BlockHandler::Ac3BlockHandler(Logger &log)
: m_log(log),
  m_c1(37, 33, 120, RS_C1, true),
  m_c2(36, 32, 120, RS_C2, true)
{
}

std::optional<Ac3BlockHandler::UncorrectedBlock>
Ac3BlockHandler::handleFrame(const Ac3InputFraming::NumberedFrame &frame) {
    int usedFrameNo;
    if (frame.frame_number != m_expected_seq) {
        if (m_consecutive_in_sequence < 3) {
            m_expected_seq = 0;
            m_consecutive_in_sequence = 0;
            return std::nullopt; // discard
        } else {
            m_consecutive_in_sequence = 0;
            usedFrameNo = m_expected_seq;
        }
    } else {
        usedFrameNo = frame.frame_number;
        m_consecutive_in_sequence++;
    }

    m_current_block.block_data[usedFrameNo] = frame.frame_data;

    if (usedFrameNo == 71) { // block completed
        m_expected_seq = 0;
        m_current_block.global_symbol_index = frame.global_symbol_index - 71 * 37 * 4;
        return std::make_optional(m_current_block);
    } else {
        m_expected_seq = usedFrameNo + 1;
        return std::nullopt;
    }
}

std::optional<Ac3BlockHandler::CorrectedBlock> Ac3BlockHandler::errorCorrectBlock(const UncorrectedBlock &block) {
    // `block` is interpreted as a matrix of size 36 (rows) x 74 (columns)

    // `c1CorrectedBlock` is a matrix of size 36 x 66
    ByteWithErasureFlag c1CorrectedBlock[36 * 66];

    std::vector<ByteWithErasureFlag> c1Data1;
    std::vector<ByteWithErasureFlag> c1Data2;
    c1Data1.resize(37);
    c1Data2.resize(37);
    for (int k = 0; k < 36; k++) {
        for (int i = 0; i < 37; i++) {
            int i1 = i * 2;
            int i2 = i * 2 + 1;
            c1Data1[i] = ByteWithErasureFlag(block.block_data[k * 2 + i1 / 37][i1 % 37]);
            c1Data2[i] = ByteWithErasureFlag(block.block_data[k * 2 + i2 / 37][i2 % 37]);
        }
        m_c1.decode(c1Data1);
        m_c1.decode(c1Data2);
        for (int i = 0; i < 33; i++) {
            c1CorrectedBlock[k * 66 + i * 2] = c1Data1[i];
            c1CorrectedBlock[k * 66 + i * 2 + 1] = c1Data2[i];
        }
    }

    // c2CorrectedBlock has the column codewords from c1CorrectedBlock in its rows (with parity bytes dropped)
    // Maybe we could do something if detecting erasures -- like marking the block as bad?
    CorrectedBlock c2CorrectedBlock;

    std::vector<ByteWithErasureFlag> c2Data;
    c2Data.resize(36);
    for (int k = 0; k < 66; k++) {
        for (int i = 0; i < 36; i++)
            c2Data[i] = c1CorrectedBlock[k + i * 66];
        m_c2.decode(c2Data);
        for (int i = 0; i < 32; i++)
            c2CorrectedBlock.block_data[k][i] = c2Data[i].byteValue();
    }

    if (c2CorrectedBlock.block_data[0][0] != 0x10 || c2CorrectedBlock.block_data[0][1] != 0x00) {
        m_log.warn(eAudio, "Block does not start with 0x10 0x00");
        return std::nullopt;
    }

    c2CorrectedBlock.global_symbol_index = block.global_symbol_index;
    return std::make_optional(c2CorrectedBlock);
}

std::vector<std::array<uint8_t, 1536>> Ac3BlockHandler::handleCorrectedBlock(const CorrectedBlock &block) {

    std::vector<std::array<uint8_t, 1536>> output;

    int symbol_in_block = 8;
    for (int i = 0; i < 66; i++) {
        // notice we skip the two first bytes in each block
        for (int j = i == 0 ? 2 : 0; j < 32; j++) {
            uint8_t byte = block.block_data[i][j];

            if (m_ac3_output_block_index == 0) {
                if (byte == 0x00) {
                    // just skip
                    continue;
                } else if (byte != 0x0b) {
                    if (m_last_burst_symbol_index != -1 && !m_logged_incorrect_start) {
                        m_log.warn(eAudio, "Block does not start with 0x0b");
                        m_logged_incorrect_start = true;
                    }
                    continue;
                }
            } else if (m_ac3_output_block_index == 1) {
                if (byte != 0x77) {
                    if (m_last_burst_symbol_index != -1)
                        m_log.warn(eAudio, "Block does not start with 0x0b 0x77");
                    m_ac3_output_block_index = 0;
                    continue;
                }
            } else if (m_ac3_output_block_index == 4) {
                if (byte != 0x1c) {
                    if (m_last_burst_symbol_index != -1)
                        m_log.warn(eAudio, "Block does not start with 0x0b 0x77 ? ? 0x1c");
                    m_ac3_output_block_index = 0;
                    continue;
                }
                m_first_symbol_in_burst = block.global_symbol_index + symbol_in_block - m_ac3_output_block_index * 4;

                // Data doesn't arrive equally spaced.  Output blocks appear 31.25 times per second and contain
                // 2880 bytes.  4 output blocks are made from 5 input blocks that are on average starting every 11520
                // input symbol (2304 bytes).  We warn if the space between input blocks is more than 3000.
                if (m_last_burst_symbol_index != -1 && m_first_symbol_in_burst - m_last_burst_symbol_index > 3000 * 4) {
                    m_log.warn(eAudio, fmt::format("Some input was not recognized as AC3 data and was discarded: {} symbols / {} bytes / {} bursts",
                        m_first_symbol_in_burst - m_last_burst_symbol_index,
                        (m_first_symbol_in_burst - m_last_burst_symbol_index) / 4,
                        (m_first_symbol_in_burst - m_last_burst_symbol_index) / (2880 * 4)));
                }
                m_last_burst_symbol_index = m_first_symbol_in_burst;
            }
            m_logged_incorrect_start = false;

            m_ac3_output_block[m_ac3_output_block_index++] = byte;

            if (m_ac3_output_block_index == 1536) {
                output.push_back(m_ac3_output_block);
                m_ac3_output_block_index = 0;
            }
            symbol_in_block += 4;
        }
    }
    return output;
}

std::string Ac3BlockHandler::reedSolomonStatistics() {
    return "C1 statistics: " + m_c1.statistics() + "C2 statistics: " + m_c2.statistics();
}
