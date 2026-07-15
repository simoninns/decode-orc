/*
 * File:        dec_audiocorrection.h
 * Purpose:     efm-decoder-audio - EFM Data24 to Audio decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef DEC_AUDIOCORRECTION_H
#define DEC_AUDIOCORRECTION_H

#include "decoders.h"
#include "section.h"

class AudioCorrection : public Decoder {
 public:
  AudioCorrection();
  void pushSection(const AudioSection& audioSection);
  AudioSection popSection();
  bool isReady() const;
  void flush();

  void showStatistics() const;

 private:
  void processQueue();

  std::deque<AudioSection> m_inputBuffer;
  std::deque<AudioSection> m_outputBuffer;

  std::vector<AudioSection> m_correctionBuffer;

  // Statistics (P-10: 64-bit so sample counters do not wrap on long captures).
  uint64_t m_concealedSamplesCount;
  uint64_t m_silencedSamplesCount;
  uint64_t m_validSamplesCount;
};

#endif  // DEC_AUDIOCORRECTION_H