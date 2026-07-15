/*
 * File:        dec_f2sectiontof1section.h
 * Purpose:     ld-efm-decoder - EFM data decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef DEC_F2SECTIONTOF1SECTION_H
#define DEC_F2SECTIONTOF1SECTION_H

#include <string>

#include "decoders.h"
#include "delay_lines.h"
#include "interleave.h"
#include "inverter.h"
#include "reedsolomon.h"

class F2SectionToF1Section : public Decoder {
 public:
  F2SectionToF1Section();
  void pushSection(const F2Section& f2Section);
  F1Section popSection();
  bool isReady() const;

  // E-7: at end of stream the newest ~111 genuine F2 frames are still held
  // inside the CIRC delay lines. flush() pushes padding frames through the
  // chain to carry that trapped tail out as F1 sections (normally hidden by
  // lead-out, but real data for a truncated capture).
  void flush();

  void showStatistics() const;

 private:
  void processQueue();

  // Push a single F2 frame's data through the CIRC delay-line / Reed-Solomon
  // chain, appending exactly one F1 frame to f1Section (a padded substitute
  // while the delay lines are still filling, otherwise the decoded frame).
  void processF2FrameData(std::vector<uint8_t> data,
                          std::vector<uint8_t> errorData,
                          std::vector<uint8_t> paddedData,
                          F1Section& f1Section);

  // Append a padded substitute F1 frame (fabricated filler emitted while the
  // CIRC delay lines fill; marked padded=1 so downstream never mistakes the
  // zeros for genuine data).
  void pushSubstituteF1Frame(F1Section& f1Section);

  void showData(const std::string& description, int32_t index,
                const std::string& timeString, std::vector<uint8_t>& data,
                std::vector<uint8_t>& dataError);

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

  // Metadata of the most recently processed F2 section; used to synthesise
  // continuing metadata for the sections emitted by flush(). m_haveSection
  // Metadata stays false until at least one real section has been processed.
  SectionMetadata m_lastSectionMetadata;
  bool m_haveSectionMetadata;
};

#endif  // DEC_F2SECTIONTOF1SECTION_H