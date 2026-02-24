/*
 * File:        writer_wav_metadata.h
 * Module:      efm-decoder-audio
 * Purpose:     EFM Data24 to Audio decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef WRITER_WAV_METADATA_H
#define WRITER_WAV_METADATA_H

#include <string>
#include <fstream>
#include <vector>

#include "section.h"

class WriterWavMetadata
{
public:
    WriterWavMetadata();
    ~WriterWavMetadata();

    bool open(const std::string &filename, bool noAudioConcealment);
    void write(const AudioSection &audioSection);
    void close();
    int64_t size();
    bool isOpen() const { return m_file.is_open(); };

private:
    std::ofstream m_file;
    bool m_noAudioConcealment;

    bool m_inErrorRange;
    std::string m_errorRangeStart;

    bool m_inConcealedRange;
    std::string m_concealedRangeStart;

    SectionTime m_absoluteSectionTime;
    SectionTime m_sectionTime;
    SectionTime m_prevAbsoluteSectionTime;
    SectionTime m_prevSectionTime;

    bool m_haveStartTime;
    SectionTime m_startTime;

    std::vector<uint8_t> m_trackNumbers;
    std::vector<SectionTime> m_trackAbsStartTimes;
    std::vector<SectionTime> m_trackAbsEndTimes;
    std::vector<SectionTime> m_trackStartTimes;
    std::vector<SectionTime> m_trackEndTimes;

    void flush();
    std::string convertToAudacityTimestamp(int32_t minutes, int32_t seconds, int32_t frames,
        int32_t subsection, int32_t sample);
};

#endif // WRITER_WAV_METADATA_H