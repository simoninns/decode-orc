/*
 * File:        dec_channeltof3frame.h
 * Module:      efm-decoder-f2
 * Purpose:     EFM T-values to F2 Section decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef DEC_CHANNELTOF3FRAME_H
#define DEC_CHANNELTOF3FRAME_H

#include <vector>
#include <queue>
#include <cstdint>
#include <string>
#include "decoders.h"
#include "efm.h"

class ChannelToF3Frame : public Decoder
{
public:
    ChannelToF3Frame();
    void pushFrame(const std::vector<uint8_t> &data);
    F3Frame popFrame();
    bool isReady() const;

    void showStatistics() const;
    std::string statisticsText() const;

private:
    void processQueue();
    F3Frame createF3Frame(const std::vector<uint8_t> &data);

    std::vector<uint8_t> tvaluesToData(const std::vector<uint8_t> &tvalues);
    uint16_t getBits(const std::vector<uint8_t> &data, int startBit, int endBit);

    Efm m_efm;

    std::queue<std::vector<uint8_t>> m_inputBuffer;
    std::queue<F3Frame> m_outputBuffer;

    // Statistics
    uint32_t m_goodFrames;
    uint32_t m_undershootFrames;
    uint32_t m_overshootFrames;
    uint32_t m_validEfmSymbols;
    uint32_t m_invalidEfmSymbols;
    uint32_t m_validSubcodeSymbols;
    uint32_t m_invalidSubcodeSymbols;
};

#endif // DEC_CHANNELTOF3FRAME_H