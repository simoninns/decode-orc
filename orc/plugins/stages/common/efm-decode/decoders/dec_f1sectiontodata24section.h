/*
 * File:        dec_f1sectiontodata24section.h
 * Purpose:     ld-efm-decoder - EFM data decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef DEC_F1SECTIONTODATA24SECTION_H
#define DEC_F1SECTIONTODATA24SECTION_H

#include "decoders.h"
#include "section.h"

class F1SectionToData24Section : public Decoder {
 public:
  F1SectionToData24Section();
  void pushSection(const F1Section& f1Section);
  void pushSection(F1Section&& f1Section);
  Data24Section popSection();
  bool isReady() const;

  void showStatistics() const;

  // Accessors for the curated decode report (byte-level data integrity).
  uint64_t totalBytes() const {
    return (m_validF1FramesCount + m_invalidF1FramesCount) *
           static_cast<uint64_t>(24);
  }
  uint64_t corruptBytes() const { return m_corruptBytesCount; }
  uint64_t paddedBytes() const { return m_paddedBytesCount; }

  // Data-loss figures restricted to bytes that actually carry disc data.
  // Bytes supplied by the CIRC warm-up fill or the end-of-stream drain are
  // excluded from BOTH the numerator and the denominator: they are a property
  // of where the decode starts and stops, not of the input.
  uint64_t populatedBytes() const { return m_populatedBytesCount; }
  uint64_t populatedCorruptBytes() const {
    return m_populatedCorruptBytesCount;
  }

 private:
  void processQueue();

  std::deque<F1Section> m_inputBuffer;
  std::deque<Data24Section> m_outputBuffer;

  uint64_t m_invalidF1FramesCount;
  uint64_t m_validF1FramesCount;
  uint64_t m_corruptBytesCount;

  uint64_t m_populatedBytesCount;
  uint64_t m_populatedCorruptBytesCount;

  uint64_t m_paddedBytesCount;
  uint64_t m_unpaddedF1FramesCount;
  uint64_t m_paddedF1FramesCount;
};

#endif  // DEC_F1SECTIONTODATA24SECTION_H