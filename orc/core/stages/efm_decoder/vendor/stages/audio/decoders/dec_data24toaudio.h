/*
 * File:        dec_data24toaudio.h
 * Module:      efm-decoder-audio
 * Purpose:     EFM Data24 to Audio decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef DEC_DATA24TOAUDIO_H
#define DEC_DATA24TOAUDIO_H

#include "decoders.h"
#include "section.h"

class Data24ToAudio : public Decoder
{
public:
    Data24ToAudio();
    void pushSection(const Data24Section &data24Section);
    AudioSection popSection();
    bool isReady() const;

    void showStatistics() const;

private:
    void processQueue();

    std::deque<Data24Section> m_inputBuffer;
    std::deque<AudioSection> m_outputBuffer;

    // Statistics
    int64_t m_invalidData24FramesCount;
    int64_t m_validData24FramesCount;
    int64_t m_invalidSamplesCount;
    int64_t m_validSamplesCount;
    int64_t m_invalidByteCount;

    SectionTime m_startTime;
    SectionTime m_endTime;
};

#endif // DEC_DATA24TOAUDIO_H