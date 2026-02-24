/*
 * File:        dec_f2sectiontof1section.h
 * Module:      ld-efm-decoder
 * Purpose:     EFM data decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef DEC_F2SECTIONTOF1SECTION_H
#define DEC_F2SECTIONTOF1SECTION_H

#include "decoders.h"
#include "reedsolomon.h"
#include "delay_lines.h"
#include "interleave.h"
#include "inverter.h"
#include <string>

class F2SectionToF1Section : public Decoder
{
public:
    F2SectionToF1Section();
    void pushSection(const F2Section &f2Section);
    F1Section popSection();
    bool isReady() const;

    void showStatistics() const;

private:
    void processQueue();
    void showData(const std::string &description, int32_t index, const std::string &timeString,
                  std::vector<uint8_t> &data, std::vector<bool> &dataError);

    std::deque<F2Section> m_inputBuffer;
    std::deque<F1Section> m_outputBuffer;

    ReedSolomon m_circ;

    DelayLines m_delayLine1;
    DelayLines m_delayLine2;
    DelayLines m_delayLineM;

    Interleave m_interleave;
    Inverter m_inverter;

    // Statistics
    uint64_t m_invalidInputF2FramesCount;
    uint64_t m_validInputF2FramesCount;
    uint64_t m_invalidOutputF1FramesCount;
    uint64_t m_validOutputF1FramesCount;
    uint64_t m_dlLostFramesCount;
    uint64_t m_continuityErrorCount;

    uint64_t m_inputByteErrors;
    uint64_t m_outputByteErrors;

    uint64_t m_invalidPaddedF1FramesCount;
    uint64_t m_invalidNonPaddedF1FramesCount;

    // Continuity check
    int32_t m_lastFrameNumber;
};

#endif // DEC_F2SECTIONTOF1SECTION_H