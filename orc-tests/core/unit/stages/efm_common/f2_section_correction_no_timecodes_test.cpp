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

// What Subcode::fromData() produces for a mode-1/4 block whose track number is
// 0: a *valid* section classified as lead-in (IEC 60908 - TNO 00 is the
// lead-in). On early discs pre-dating the EFM timecode spec every programme
// block carries TNO 00, so the whole disc arrives looking like valid lead-in.
SectionMetadata validLeadInMetadata(int32_t absoluteFrame) {
  SectionMetadata metadata;
  metadata.setSectionType(SectionType(SectionType::LeadIn), 0);
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

// The regression behind "an NTSC disc with no EFM timecodes collapses to a
// ~63-frame track and loses ~all data": every programme block carries TNO 00
// and so is classified as a *valid* lead-in section. In No Timecodes mode the
// lead-in skip must NOT fire - otherwise the whole disc is dropped before the
// synthesised timeline can re-stamp it, leaving only the handful of
// CRC-failed sections to be decoded. Each lead-in-typed section must instead
// be re-stamped UserData/track 1 and emitted on a contiguous timeline, and its
// meaningless "TOC" must not be harvested.
TEST(F2SectionCorrectionNoTimecodes,
     EmitsLeadInTypedSectionsInsteadOfDroppingThem) {
  F2SectionCorrection correction;
  correction.setNoTimecodes(true);

  std::vector<F2Section> emitted;
  for (int i = 0; i < kSectionCount; ++i) {
    // Frozen lead-in timestamp, as a disc with a non-programme Q-channel would
    // present; the value is irrelevant because No Timecodes overrides it.
    correction.pushSection(makeSection(validLeadInMetadata(0)));
    for (auto& section : drain(correction)) emitted.push_back(section);
  }
  correction.flush();
  for (auto& section : drain(correction)) emitted.push_back(section);

  // Not one lead-in-typed section is dropped.
  ASSERT_EQ(static_cast<int>(emitted.size()), kSectionCount);
  for (size_t i = 0; i < emitted.size(); ++i) {
    EXPECT_TRUE(emitted[i].metadata.isValid()) << "section " << i;
    EXPECT_EQ(emitted[i].metadata.trackNumber(), 1) << "section " << i;
    EXPECT_EQ(emitted[i].metadata.sectionType().type(), SectionType::UserData)
        << "section " << i;
    EXPECT_EQ(emitted[i].metadata.absoluteSectionTime().frames(),
              static_cast<int32_t>(i))
        << "section " << i;
  }
  // The "TOC" on such a disc is noise; No Timecodes mode must not harvest it.
  EXPECT_EQ(correction.leadinSections(), 0u);
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

// Guard the other side of the No Timecodes gate: in the default mode a valid
// lead-in section is genuine lead-in and its TOC must still be harvested (the
// !m_noTimecodes gate must not disable normal-disc lead-in handling).
TEST(F2SectionCorrectionNoTimecodes, DefaultModeStillHarvestsLeadInToc) {
  F2SectionCorrection correction;

  for (int i = 0; i < kSectionCount; ++i) {
    correction.pushSection(makeSection(validLeadInMetadata(i)));
    (void)drain(correction);
  }
  correction.flush();
  (void)drain(correction);

  EXPECT_EQ(correction.leadinSections(), static_cast<uint32_t>(kSectionCount));
}

}  // namespace
