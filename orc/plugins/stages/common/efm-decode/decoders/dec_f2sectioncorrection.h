/*
 * File:        dec_f2sectioncorrection.h
 * Purpose:     efm-decoder-f2 - EFM T-values to F2 Section decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef DEC_F2SECTIONCORRECTION_H
#define DEC_F2SECTIONCORRECTION_H

#include <cstdint>
#include <deque>
#include <queue>
#include <vector>

#include "decoders.h"
#include "section_metadata.h"

class F2SectionCorrection : public Decoder {
 public:
  F2SectionCorrection();
  void pushSection(const F2Section& data);
  void pushSection(F2Section&& data);
  F2Section popSection();
  bool isReady() const;
  bool isValid() const;
  void flush();
  void setNoTimecodes(bool noTimecodes);
  void showStatistics() const;

  // Diagnostics used to build a meaningful error when lead-in never locks:
  //   receivedSections()      - total F2 sections handed to pushSection().  If
  //                             zero, EFM/F3 frame sync never happened at all.
  //   validMetadataSections() - of those, how many carried valid subcode
  //                             metadata.  If zero (but sections were received)
  //                             the stream decoded but has no usable timecodes.
  int32_t receivedSections() const { return m_receivedSections; }
  int32_t validMetadataSections() const { return m_validMetadataSections; }

 private:
  void processQueue();

  void waitForInputToSettle(F2Section& f2Section);
  void waitingForSection(F2Section& f2Section);
  SectionTime getExpectedAbsoluteTime() const;

  void processInternalBuffer();
  void outputSections();
  void emitSection();

  std::queue<F2Section> m_inputBuffer;
  std::deque<F2Section> m_leadinBuffer;
  std::queue<F2Section> m_outputBuffer;

  std::deque<F2Section> m_internalBuffer;

  bool m_leadinComplete;

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

  // Diagnostic counters (see receivedSections() / validMetadataSections())
  int32_t m_receivedSections;
  int32_t m_validMetadataSections;
};

#endif  // DEC_F2SECTIONCORRECTION_H