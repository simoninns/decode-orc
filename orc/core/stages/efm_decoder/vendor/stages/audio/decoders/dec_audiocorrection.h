/*
 * File:        dec_audiocorrection.h
 * Module:      efm-decoder-audio
 * Purpose:     EFM Data24 to Audio decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef DEC_AUDIOCORRECTION_H
#define DEC_AUDIOCORRECTION_H

#include "decoders.h"
#include "section.h"

class AudioCorrection : public Decoder
{
public:
    AudioCorrection();
    void pushSection(const AudioSection &audioSection);
    AudioSection popSection();
    bool isReady() const;
    void flush();

    void showStatistics() const;

private:
    void processQueue();
    std::string convertToAudacityTimestamp(int32_t minutes, int32_t seconds, int32_t frames, int32_t subsection, int32_t sample);

    std::deque<AudioSection> m_inputBuffer;
    std::deque<AudioSection> m_outputBuffer;

    std::vector<AudioSection> m_correctionBuffer;

    bool m_firstSectionFlag;

    // Statistics
    uint32_t m_concealedSamplesCount;
    uint32_t m_silencedSamplesCount;
    uint32_t m_validSamplesCount;
};

#endif // DEC_AUDIOCORRECTION_H