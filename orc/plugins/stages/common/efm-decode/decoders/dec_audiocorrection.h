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
  void pushSection(AudioSection&& audioSection);
  AudioSection popSection();
  bool isReady() const;
  void flush();

  void showStatistics() const;

  // Accessors for the curated decode report.
  uint64_t concealedSamples() const { return m_concealedSamplesCount; }
  uint64_t silencedSamples() const { return m_silencedSamplesCount; }
  uint64_t validSamples() const { return m_validSamplesCount; }

 private:
  void processQueue();

  // Correct one section using its (optional) neighbours for concealment
  // context.  Either neighbour may be null at a stream edge (E-6 edge priming),
  // in which case bursts touching that edge are muted rather than interpolated.
  AudioSection correctSection(const AudioSection* preceding,
                              const AudioSection& correcting,
                              const AudioSection* following);

  // Flatten a section's 98 frames into contiguous per-channel sample streams so
  // that error bursts crossing a section boundary can be bridged from clean
  // samples in the neighbouring sections.
  static void appendChannelSamples(const AudioSection& section,
                                   std::vector<int16_t>& valLeft,
                                   std::vector<uint8_t>& errLeft,
                                   std::vector<int16_t>& valRight,
                                   std::vector<uint8_t>& errRight);

  // Conceal/mute one channel's flagged samples in the region [midStart,
  // midEnd).
  void correctChannel(std::vector<int16_t>& val, std::vector<uint8_t>& err,
                      std::vector<uint8_t>& concealed, int midStart,
                      int midEnd);

  std::deque<AudioSection> m_inputBuffer;
  std::deque<AudioSection> m_outputBuffer;

  std::vector<AudioSection> m_correctionBuffer;

  // E-6 edge priming: has the first section (which has no preceding neighbour)
  // been corrected yet?  Controls the flush-time correction of the stream
  // edges.
  bool m_firstSectionCorrected;

  // Statistics (P-10: 64-bit so sample counters do not wrap on long captures).
  uint64_t m_concealedSamplesCount;
  uint64_t m_silencedSamplesCount;
  uint64_t m_validSamplesCount;
};

#endif  // DEC_AUDIOCORRECTION_H