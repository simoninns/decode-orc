/*
 * File:        f2_section_correction_no_timecodes_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for No Timecodes mode: a stream whose Q-channel
 *              never decodes (early CAV discs pre-dating the EFM timecode
 *              specification) must still be given a synthesised timeline and
 *              emitted rather than being silently discarded
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

// What Subcode::fromData() leaves behind when the Q-channel CRC fails or
// DATA-Q is an all-zero mode-0 block: every field at its default, flagged
// invalid.
SectionMetadata crcFailureMetadata() {
  SectionMetadata metadata;
  metadata.setValid(false);
  return metadata;
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

std::vector<F2Section> drain(F2SectionCorrection& correction) {
  std::vector<F2Section> out;
  while (correction.isReady()) out.push_back(correction.popSection());
  return out;
}

constexpr int kSectionCount = 30;

// The regression behind "EFM not decoding audio, even with 'no timecode'
// selected": on a disc with no decodable Q-channel, No Timecodes mode dropped
// every section while waiting for a valid one to seed the timeline, so the
// decode produced no audio at all.
TEST(F2SectionCorrectionNoTimecodes,
     EmitsAllSectionsWhenNoMetadataEverDecodes) {
  F2SectionCorrection correction;
  correction.setNoTimecodes(true);

  std::vector<F2Section> emitted;
  for (int i = 0; i < kSectionCount; ++i) {
    correction.pushSection(makeSection(crcFailureMetadata()));
    for (auto& section : drain(correction)) emitted.push_back(section);
  }
  correction.flush();
  for (auto& section : drain(correction)) emitted.push_back(section);

  ASSERT_EQ(static_cast<int>(emitted.size()), kSectionCount);

  // Every section must carry the synthesised timeline: valid, track 1,
  // contiguous absolute time from 00:00:00.
  for (size_t i = 0; i < emitted.size(); ++i) {
    EXPECT_TRUE(emitted[i].metadata.isValid()) << "section " << i;
    EXPECT_EQ(emitted[i].metadata.trackNumber(), 1) << "section " << i;
    EXPECT_EQ(emitted[i].metadata.sectionType().type(), SectionType::UserData)
        << "section " << i;
    EXPECT_EQ(emitted[i].metadata.absoluteSectionTime().frames(),
              static_cast<int32_t>(i))
        << "section " << i;
  }
}

// No Timecodes mode must treat the synthesised timeline as authoritative even
// when the Q-channel decodes: a disc in this mode carries meaningless
// timestamps, so discontinuities in them must not trigger the missing-section
// or out-of-order handling.
TEST(F2SectionCorrectionNoTimecodes, OverridesDecodedTimestamps) {
  F2SectionCorrection correction;
  correction.setNoTimecodes(true);

  std::vector<F2Section> emitted;
  for (int i = 0; i < kSectionCount; ++i) {
    // Constant (repeating) timestamps, as an early disc with a frozen
    // Q-channel would present.
    correction.pushSection(makeSection(validMetadata(0)));
    for (auto& section : drain(correction)) emitted.push_back(section);
  }
  correction.flush();
  for (auto& section : drain(correction)) emitted.push_back(section);

  ASSERT_EQ(static_cast<int>(emitted.size()), kSectionCount);
  for (size_t i = 0; i < emitted.size(); ++i) {
    EXPECT_EQ(emitted[i].metadata.absoluteSectionTime().frames(),
              static_cast<int32_t>(i))
        << "section " << i;
  }
  EXPECT_EQ(correction.missingSections(), 0u);
  EXPECT_EQ(correction.uncorrectableSections(), 0u);
}

// Without the flag, a stream with no decodable metadata cannot settle and
// nothing is emitted - the documented reason the No Timecodes option exists.
TEST(F2SectionCorrectionNoTimecodes,
     DefaultModeStillRequiresDecodableMetadata) {
  F2SectionCorrection correction;

  std::vector<F2Section> emitted;
  for (int i = 0; i < kSectionCount; ++i) {
    correction.pushSection(makeSection(crcFailureMetadata()));
    for (auto& section : drain(correction)) emitted.push_back(section);
  }
  correction.flush();
  for (auto& section : drain(correction)) emitted.push_back(section);

  EXPECT_TRUE(emitted.empty());
}

}  // namespace
