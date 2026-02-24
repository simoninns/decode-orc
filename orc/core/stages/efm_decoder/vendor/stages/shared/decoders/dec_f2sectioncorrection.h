/*
 * File:        dec_f2sectioncorrection.h
 * Module:      efm-decoder-f2
 * Purpose:     EFM T-values to F2 Section decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef DEC_F2SECTIONCORRECTION_H
#define DEC_F2SECTIONCORRECTION_H

#include <deque>
#include <queue>
#include <vector>
#include <cstdint>
#include "decoders.h"
#include "section_metadata.h"
#include "../../../core/timecode_probe.h"

class F2SectionCorrection : public Decoder
{
public:
    F2SectionCorrection();
    void pushSection(const F2Section &data);
    F2Section popSection();
    bool isReady() const;
    bool isValid() const;
    void flush();
    void setNoTimecodes(bool noTimecodes);
    void showStatistics() const;
    void recordProbeSection(bool isValid, int32_t absoluteFrames) { m_probeStats.recordSection(isValid, absoluteFrames); }
    TimecodeProbeStats getProbeStats() const { return m_probeStats; }

private:
    void processQueue();

    void waitForInputToSettle(F2Section &f2Section);
    void waitingForSection(F2Section &f2Section);
    SectionTime getExpectedAbsoluteTime() const;

    void processInternalBuffer();
    void outputSections();

    std::queue<F2Section> m_inputBuffer;
    std::deque<F2Section> m_leadinBuffer;
    std::queue<F2Section> m_outputBuffer;

    std::deque<F2Section> m_internalBuffer;

    bool m_leadinComplete;

    std::deque<F2Section> m_window;
    int32_t m_maximumGapSize;
    int32_t m_paddingWatermark;

    // Statistics
    uint32_t m_totalSections;
    uint32_t m_correctedSections;
    uint32_t m_uncorrectableSections;
    uint32_t m_preLeadinSections;
    uint32_t m_missingSections;
    uint32_t m_paddingSections;
    uint32_t m_outOfOrderSections;

    uint32_t m_qmode1Sections;
    uint32_t m_qmode2Sections;
    uint32_t m_qmode3Sections;
    uint32_t m_qmode4Sections;

    // Time statistics
    SectionTime m_absoluteStartTime;
    SectionTime m_absoluteEndTime;
    std::vector<uint8_t> m_trackNumbers;
    std::vector<SectionTime> m_trackStartTimes;
    std::vector<SectionTime> m_trackEndTimes;

    // Timecode handling
    bool m_noTimecodes;
    TimecodeProbeStats m_probeStats;
};

#endif // DEC_F2SECTIONCORRECTION_H