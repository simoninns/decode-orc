/*
 * File:        circ_boundary_attribution_test.cpp
 * Module:      orc-core-tests
 * Purpose:     End-to-end unit tests that a provably clean EFM stream reports
 *              no input defects: the CIRC warm-up and end-of-stream drain must
 *              be attributed to the decode boundary, never to the disc
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "dec_audiocorrection.h"
#include "dec_data24toaudio.h"
#include "dec_f1sectiontodata24section.h"
#include "dec_f2sectiontof1section.h"
#include "efm_constants.h"
#include "section.h"

namespace {

constexpr int kFramesPerSection = efm::kFramesPerSection;

// The decoder inverts the P and Q parity bytes (ECMA-130 issue 2 pages 35-36)
// before C1, so a frame carrying 0xFF in those positions and zero elsewhere
// becomes the all-zero word - a valid Reed-Solomon codeword - by the time it
// reaches the decoder. That gives a stream which is clean by construction, so
// any error the pipeline reports is necessarily its own.
std::vector<uint8_t> cleanFrameData() {
  std::vector<uint8_t> data(32, 0);
  for (int i = 12; i < 16; ++i) data[i] = 0xFF;
  for (int i = 28; i < 32; ++i) data[i] = 0xFF;
  return data;
}

enum class Kind { LeadIn, Programme, LeadOut };

SectionMetadata makeMetadata(Kind kind, uint8_t trackNumber,
                             int32_t absoluteFrame) {
  SectionMetadata metadata;
  switch (kind) {
    case Kind::LeadIn:
      metadata.setSectionType(SectionType(SectionType::LeadIn), 0);
      break;
    case Kind::LeadOut:
      metadata.setSectionType(SectionType(SectionType::LeadOut), 0);
      break;
    case Kind::Programme:
      metadata.setSectionType(SectionType(SectionType::UserData), trackNumber);
      metadata.setIndex(1);
      break;
  }
  metadata.setAbsoluteSectionTime(SectionTime(absoluteFrame));
  metadata.setSectionTime(SectionTime(absoluteFrame));
  metadata.setValid(true);
  return metadata;
}

F2Section makeCleanF2Section(const SectionMetadata& metadata) {
  F2Section section;
  section.metadata = metadata;
  for (int i = 0; i < kFramesPerSection; ++i) {
    F2Frame frame;
    frame.setData(cleanFrameData());
    frame.setErrorData(std::vector<uint8_t>(32, 0));
    frame.setPaddedData(std::vector<uint8_t>(32, 0));
    section.pushFrame(frame);
  }
  return section;
}

// The real audio decode chain, from F2 sections through to concealment.
struct Pipeline {
  F2SectionToF1Section f2ToF1;
  F1SectionToData24Section f1ToData24;
  Data24ToAudio data24ToAudio;
  AudioCorrection correction;

  void pump() {
    while (f2ToF1.isReady()) f1ToData24.pushSection(f2ToF1.popSection());
    while (f1ToData24.isReady())
      data24ToAudio.pushSection(f1ToData24.popSection());
    while (data24ToAudio.isReady())
      correction.pushSection(data24ToAudio.popSection());
    while (correction.isReady()) correction.popSection();
  }

  void finish() {
    f2ToF1.flush();
    pump();
    correction.flush();
    while (correction.isReady()) correction.popSection();
  }
};

// A clean disc: lead-in, programme (track 1), then lead-out - the layout that
// makes the de-interleave drain harmless, because the tail it cannot recover
// falls in the lead-out.
constexpr int kLeadInSections = 150;
constexpr int kProgrammeSections = 200;
constexpr int kLeadOutSections = 50;

void runCleanDisc(Pipeline& pipeline) {
  int32_t absolute = 0;
  for (int i = 0; i < kLeadInSections; ++i) {
    pipeline.f2ToF1.pushSection(
        makeCleanF2Section(makeMetadata(Kind::LeadIn, 0, absolute++)));
  }
  pipeline.pump();
  for (int i = 0; i < kProgrammeSections; ++i) {
    pipeline.f2ToF1.pushSection(
        makeCleanF2Section(makeMetadata(Kind::Programme, 1, absolute++)));
  }
  pipeline.pump();
  for (int i = 0; i < kLeadOutSections; ++i) {
    pipeline.f2ToF1.pushSection(
        makeCleanF2Section(makeMetadata(Kind::LeadOut, 0, absolute++)));
  }
  pipeline.pump();
  pipeline.finish();
}

// Acceptance criterion 1: a stream that is clean by construction must report
// zero uncorrectable C1/C2 over the codewords that can actually be scored.
TEST(CircBoundaryAttribution, CleanStreamHasNoUncorrectableCodewords) {
  Pipeline pipeline;
  runCleanDisc(pipeline);

  EXPECT_EQ(pipeline.f2ToF1.errorC1s(), 0);
  EXPECT_EQ(pipeline.f2ToF1.errorC2s(), 0);
  EXPECT_EQ(pipeline.f2ToF1.fixedC1s(), 0);
  EXPECT_EQ(pipeline.f2ToF1.fixedC2s(), 0);
  // The boundary codewords are still counted, just not scored.
  EXPECT_GT(pipeline.f2ToF1.paddedC1s(), 0);
  EXPECT_GT(pipeline.f2ToF1.paddedC2s(), 0);
}

// The regression this guards: a drain codeword mixes genuine symbols with
// filler, fails, and flags all 24 outputs as errors. Unless those outputs are
// also marked as filler, the genuine-looking ones are scored as data loss - on
// a stream with no data loss at all.
TEST(CircBoundaryAttribution, CleanStreamReportsNoDataLoss) {
  Pipeline pipeline;
  runCleanDisc(pipeline);

  EXPECT_EQ(pipeline.f1ToData24.populatedCorruptBytes(), 0u);
  EXPECT_GT(pipeline.f1ToData24.populatedBytes(), 0u);
}

TEST(CircBoundaryAttribution, CleanStreamMutesAndConcealsNothing) {
  Pipeline pipeline;
  runCleanDisc(pipeline);

  for (size_t i = 0; i < static_cast<size_t>(DiscRegion::Count); ++i) {
    const RegionLoss& loss =
        pipeline.correction.regionLoss(static_cast<DiscRegion>(i));
    EXPECT_EQ(loss.concealed, 0u) << "region " << i;
    EXPECT_EQ(loss.silenced, 0u) << "region " << i;
  }
  EXPECT_TRUE(pipeline.correction.trackLosses().empty());
}

// The boundaries must be reported at their true size: the warm-up is the
// de-interleave latency, and the drain is every frame flush() emits.
TEST(CircBoundaryAttribution, ReportsWarmUpAndDrainFrameCountsCorrectly) {
  Pipeline pipeline;
  runCleanDisc(pipeline);

  EXPECT_EQ(pipeline.f2ToF1.warmupLostFrames(),
            static_cast<uint64_t>(efm::kDeinterleaveLatencyF1Frames));
  // flush() rounds the latency up to whole sections, so the drain is longer
  // than the latency itself - but it must never be reported as zero.
  EXPECT_GE(pipeline.f2ToF1.drainLostFrames(),
            static_cast<uint64_t>(efm::kDeinterleaveLatencyF1Frames));
}

// On a disc that carries a lead-out, the tail the de-interleaver cannot recover
// falls in that lead-out and costs the listener nothing.
TEST(CircBoundaryAttribution, DrainOfADiscWithLeadOutFallsInTheLeadOut) {
  Pipeline pipeline;
  runCleanDisc(pipeline);

  EXPECT_GT(pipeline.correction.drainSamples(), 0u);
  EXPECT_EQ(pipeline.correction.drainSamplesIn(DiscRegion::LeadOut),
            pipeline.correction.drainSamples());
  EXPECT_EQ(pipeline.correction.drainSamplesIn(DiscRegion::Programme), 0u);
}

// F2SectionCorrection::flush() emits whatever is left in its buffer at end of
// stream without checking validity, so a capture can end with sections whose
// Q-channel CRC failed. Those carry subcode.cpp's defaults: UserData, track 0,
// absolute time 00:00:00. Attributing samples with that metadata invents a
// "track 0 at 00:00:00" that is nowhere on the disc.
TEST(CircBoundaryAttribution, IgnoresInvalidTailMetadataWhenAttributing) {
  Pipeline pipeline;

  int32_t absolute = 0;
  for (int i = 0; i < kLeadInSections; ++i) {
    pipeline.f2ToF1.pushSection(
        makeCleanF2Section(makeMetadata(Kind::LeadIn, 0, absolute++)));
  }
  pipeline.pump();
  for (int i = 0; i < kProgrammeSections; ++i) {
    pipeline.f2ToF1.pushSection(
        makeCleanF2Section(makeMetadata(Kind::Programme, 1, absolute++)));
  }
  pipeline.pump();
  for (int i = 0; i < kLeadOutSections; ++i) {
    pipeline.f2ToF1.pushSection(
        makeCleanF2Section(makeMetadata(Kind::LeadOut, 0, absolute++)));
  }
  pipeline.pump();

  // The CRC-fail tail: default-constructed metadata, explicitly invalid.
  for (int i = 0; i < 3; ++i) {
    SectionMetadata invalid;  // UserData, track 0, INDEX 01, 00:00:00
    invalid.setValid(false);
    pipeline.f2ToF1.pushSection(makeCleanF2Section(invalid));
  }
  pipeline.pump();
  pipeline.finish();

  // Nothing may be credited to the phantom track 0.
  EXPECT_EQ(pipeline.correction.trackLosses().count(0), 0u);
  // The tail follows the lead-out, so the drain still belongs to the lead-out
  // rather than being dragged into the programme area by bogus metadata.
  EXPECT_EQ(pipeline.correction.drainSamplesIn(DiscRegion::Programme), 0u);
  EXPECT_EQ(pipeline.correction.regionLoss(DiscRegion::Programme).silenced, 0u);
  EXPECT_EQ(pipeline.correction.regionLoss(DiscRegion::Programme).concealed,
            0u);
}

// Regions must still be told apart end to end, not collapsed into one bucket.
TEST(CircBoundaryAttribution, AttributesEachRegionSeparately) {
  Pipeline pipeline;
  runCleanDisc(pipeline);

  EXPECT_GT(pipeline.correction.regionLoss(DiscRegion::LeadIn).decoded, 0u);
  EXPECT_GT(pipeline.correction.regionLoss(DiscRegion::Programme).decoded, 0u);
  EXPECT_GT(pipeline.correction.regionLoss(DiscRegion::LeadOut).decoded, 0u);
  EXPECT_EQ(
      pipeline.correction.regionLoss(DiscRegion::OutsideProgrammeArea).decoded,
      0u);
}

}  // namespace
