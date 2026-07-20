/*
 * File:        f2_section_correction_tail_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for end-of-stream metadata handling: trailing
 *              sections whose Q-channel never decoded must have their timeline
 *              continued rather than being emitted with CRC-failure defaults
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "dec_f2sectioncorrection.h"
#include "efm_constants.h"
#include "section.h"

namespace {

constexpr int kFramesPerSection = efm::kFramesPerSection;

// The correction stage only inspects metadata, so the payload can be anything
// well-formed.
F2Section makeSection(const SectionMetadata& metadata) {
  F2Section section;
  section.metadata = metadata;
  for (int i = 0; i < kFramesPerSection; ++i) {
    F2Frame frame;
    frame.setData(std::vector<uint8_t>(32, 0));
    frame.setErrorData(std::vector<uint8_t>(32, 0));
    frame.setPaddedData(std::vector<uint8_t>(32, 0));
    section.pushFrame(frame);
  }
  return section;
}

SectionMetadata validMetadata(int32_t absoluteFrame) {
  SectionMetadata metadata;
  metadata.setSectionType(SectionType(SectionType::UserData), 1);
  metadata.setIndex(1);
  metadata.setAbsoluteSectionTime(SectionTime(absoluteFrame));
  metadata.setSectionTime(SectionTime(absoluteFrame));
  metadata.setValid(true);
  return metadata;
}

// What Subcode::fromData() leaves behind when the Q-channel CRC fails: every
// field at its default, flagged invalid.
SectionMetadata crcFailureMetadata() {
  SectionMetadata metadata;
  metadata.setValid(false);
  return metadata;
}

std::vector<F2Section> drain(F2SectionCorrection& correction) {
  std::vector<F2Section> out;
  while (correction.isReady()) out.push_back(correction.popSection());
  return out;
}

// Push enough valid sections for the timeline to settle, then a short run of
// CRC-failure sections, then end the stream.
std::vector<F2Section> runWithInvalidTail(F2SectionCorrection& correction,
                                          int validSections,
                                          int invalidTailSections) {
  std::vector<F2Section> emitted;
  int32_t absolute = 0;
  for (int i = 0; i < validSections; ++i) {
    correction.pushSection(makeSection(validMetadata(absolute++)));
    for (auto& section : drain(correction)) emitted.push_back(section);
  }
  for (int i = 0; i < invalidTailSections; ++i) {
    correction.pushSection(makeSection(crcFailureMetadata()));
    for (auto& section : drain(correction)) emitted.push_back(section);
  }
  correction.flush();
  for (auto& section : drain(correction)) emitted.push_back(section);
  return emitted;
}

constexpr int kValidSections = 30;
constexpr int kInvalidTail = 3;

// The regression: these sections used to reach the rest of the pipeline
// carrying UserData / track 0 / 00:00:00, which surfaced as a phantom "track 0
// at 00:00:00" in the decode report and in the WAV/Audacity metadata.
TEST(F2SectionCorrectionTail, ContinuesTheTimelineOverAnInvalidTail) {
  F2SectionCorrection correction;
  const std::vector<F2Section> emitted =
      runWithInvalidTail(correction, kValidSections, kInvalidTail);

  ASSERT_EQ(static_cast<int>(emitted.size()), kValidSections + kInvalidTail);

  // Every emitted section must carry a usable Q position...
  for (size_t i = 0; i < emitted.size(); ++i) {
    EXPECT_TRUE(emitted[i].metadata.isValid()) << "section " << i;
    EXPECT_EQ(emitted[i].metadata.trackNumber(), 1) << "section " << i;
  }

  // ...and the absolute timeline must stay contiguous across the tail rather
  // than collapsing to 00:00:00.
  for (size_t i = 0; i < emitted.size(); ++i) {
    EXPECT_EQ(emitted[i].metadata.absoluteSectionTime().frames(),
              static_cast<int32_t>(i))
        << "section " << i;
  }
}

// The tail is a consequence of where the capture stops, not a defect in the
// disc, so it must be reported on its own rather than inflating the
// uncorrectable-section count that drives the quality grade.
TEST(F2SectionCorrectionTail, CountsTheTailSeparatelyFromUncorrectable) {
  F2SectionCorrection correction;
  runWithInvalidTail(correction, kValidSections, kInvalidTail);

  EXPECT_EQ(correction.tailFilledSections(),
            static_cast<uint32_t>(kInvalidTail));
  EXPECT_EQ(correction.uncorrectableSections(), 0u);
  EXPECT_EQ(correction.missingSections(), 0u);
}

TEST(F2SectionCorrectionTail, LeavesACleanStreamUntouched) {
  F2SectionCorrection correction;
  const std::vector<F2Section> emitted =
      runWithInvalidTail(correction, kValidSections, 0);

  EXPECT_EQ(correction.tailFilledSections(), 0u);
  ASSERT_EQ(static_cast<int>(emitted.size()), kValidSections);
  for (size_t i = 0; i < emitted.size(); ++i) {
    EXPECT_EQ(emitted[i].metadata.absoluteSectionTime().frames(),
              static_cast<int32_t>(i));
  }
}

}  // namespace
