/*
 * File:        dec_f3frametof2section.h
 * Module:      efm-decoder-f2
 * Purpose:     EFM T-values to F2 Section decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef DEC_F3FRAMETOF2SECTION_H
#define DEC_F3FRAMETOF2SECTION_H

#include <vector>
#include <queue>
#include <cstdint>
#include <string>
#include "decoders.h"
#include "section.h"
#include "subcode.h"

class F3FrameToF2Section : public Decoder
{
public:
    F3FrameToF2Section();
    void pushFrame(const F3Frame &data);
    F2Section popSection();
    bool isReady() const;

    void showStatistics() const;
    std::string statisticsText() const;

private:
    void processStateMachine();
    void outputSection(bool showAddress);

    std::queue<F3Frame> m_inputBuffer;
    std::queue<F2Section> m_outputBuffer;

    std::vector<F3Frame> m_internalBuffer;
    std::vector<F3Frame> m_sectionFrames;

    int32_t m_badSyncCounter;
    SectionMetadata m_lastSectionMetadata;

    // State machine states
    enum State { ExpectingInitialSync, ExpectingSync, HandleValid, HandleOvershoot, HandleUndershoot, LostSync };

    State m_currentState;

    // State machine state processing functions
    State expectingInitialSync();
    State expectingSync();
    State handleValid();
    State handleUndershoot();
    State handleOvershoot();
    State lostSync();

    // Statistics
    uint64_t m_inputF3Frames;
    uint64_t m_presyncDiscardedF3Frames;
    uint64_t m_goodSync0;
    uint64_t m_missingSync0;
    uint64_t m_undershootSync0;
    uint64_t m_overshootSync0;
    uint64_t m_discardedF3Frames;
    uint64_t m_paddedF3Frames;
    uint64_t m_lostSyncCounter;
};

#endif // DEC_F3FRAMETOF2SECTION_H