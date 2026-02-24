/*
 * File:        dec_tvaluestochannel.h
 * Module:      efm-decoder-f2
 * Purpose:     EFM T-values to F2 Section decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef DEC_TVALUESTOCHANNEL_H
#define DEC_TVALUESTOCHANNEL_H

#include <vector>
#include <queue>
#include <cstdint>
#include <string>
#include "decoders.h"
#include "tvalues.h"

class TvaluesToChannel : public Decoder
{
public:
    TvaluesToChannel();
    void pushFrame(const std::vector<uint8_t> &data);
    std::vector<uint8_t> popFrame();
    bool isReady() const;

    void showStatistics() const;
    std::string statisticsText() const;

private:
    void processStateMachine();
    void attemptToFixOvershootFrame(std::vector<uint8_t> &frameData);
    void attemptToFixUndershootFrame(uint32_t startIndex, uint32_t endIndex, std::vector<uint8_t> &frameData);
    uint32_t countBits(const std::vector<uint8_t> &data, int32_t startPosition = 0, int32_t endPosition = -1);

    // State machine states
    enum State { ExpectingInitialSync, ExpectingSync, HandleOvershoot, HandleUndershoot };

    // Statistics
    uint32_t m_consumedTValues;
    uint32_t m_discardedTValues;
    uint32_t m_channelFrameCount;

    uint32_t m_perfectFrames;
    uint32_t m_longFrames;
    uint32_t m_shortFrames;

    uint32_t m_overshootSyncs;
    uint32_t m_undershootSyncs;
    uint32_t m_perfectSyncs;

    State m_currentState;
    std::vector<uint8_t> m_internalBuffer;
    std::vector<uint8_t> m_frameData;

    std::queue<std::vector<uint8_t>> m_inputBuffer;
    std::queue<std::vector<uint8_t>> m_outputBuffer;

    Tvalues m_tvalues;
    uint32_t m_tvalueDiscardCount;

    State expectingInitialSync();
    State expectingSync();
    State handleUndershoot();
    State handleOvershoot();
};

#endif // DEC_TVALUESTOCHANNEL_H