/*
 * File:        dec_f1sectiontodata24section.h
 * Module:      ld-efm-decoder
 * Purpose:     EFM data decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef DEC_F1SECTIONTODATA24SECTION_H
#define DEC_F1SECTIONTODATA24SECTION_H

#include "decoders.h"
#include "section.h"
#include <string>

class F1SectionToData24Section : public Decoder
{
public:
    F1SectionToData24Section();
    void pushSection(const F1Section &f1Section);
    Data24Section popSection();
    bool isReady() const;

    void showStatistics() const;
    std::string statisticsText() const;

private:
    void processQueue();

    std::deque<F1Section> m_inputBuffer;
    std::deque<Data24Section> m_outputBuffer;

    uint64_t m_invalidF1FramesCount;
    uint64_t m_validF1FramesCount;
    uint64_t m_corruptBytesCount;

    uint64_t m_paddedBytesCount;
    uint64_t m_unpaddedF1FramesCount;
    uint64_t m_paddedF1FramesCount;
};

#endif // DEC_F1SECTIONTODATA24SECTION_H