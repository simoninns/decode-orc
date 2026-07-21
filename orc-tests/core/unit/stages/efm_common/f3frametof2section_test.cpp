/*
 * File:        f3frametof2section_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for F3 frame to F2 section assembly framing
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "dec_f3frametof2section.h"
#include "efm_constants.h"
#include "frame.h"
#include "section.h"

namespace {

// Frames are tagged with a sequence number in every payload byte so that a
// popped section can be checked for exact frame content and order.
F3Frame makeFrame(uint8_t tag) {
  F3Frame frame;
  frame.setData(std::vector<uint8_t>(32, tag));
  frame.setErrorData(std::vector<uint8_t>(32, 0));
  frame.setPaddedData(std::vector<uint8_t>(32, 0));
  frame.setFrameTypeAsSubcode(0);
  return frame;
}

F3Frame makeSync0Frame(uint8_t tag) {
  F3Frame frame = makeFrame(tag);
  frame.setFrameTypeAsSync0();
  return frame;
}

F3Frame makeSync1Frame(uint8_t tag) {
  F3Frame frame = makeFrame(tag);
  frame.setFrameTypeAsSync1();
  return frame;
}

// Expects the section to consist of exactly the cleanly-received frames tagged
// firstTag, firstTag + 1, ... in their original stream order.
void expectSectionTags(const F2Section& section, uint8_t firstTag) {
  for (int32_t i = 0; i < efm::kFramesPerSection; ++i) {
    const uint8_t expected = static_cast<uint8_t>(firstTag + i);
    EXPECT_EQ(section.frame(i).data()[0], expected)
        << "frame " << i << " has wrong content or is out of order";
    for (uint8_t errorByte : section.frame(i).errorData()) {
      EXPECT_EQ(errorByte, 0) << "frame " << i << " unexpectedly padded";
    }
  }
}

TEST(F3FrameToF2Section, RetainsSync1FrameWhenSync0IsMissing) {
  F3FrameToF2Section decoder;
  uint8_t tag = 0;

  // Section A with an intact sync0/sync1 boundary.
  decoder.pushFrame(makeSync0Frame(tag++));
  decoder.pushFrame(makeSync1Frame(tag++));
  for (int i = 0; i < 96; ++i) decoder.pushFrame(makeFrame(tag++));

  // Section B's sync0 symbol was destroyed: the frame arrives as an ordinary
  // subcode frame, followed by a normal sync1.
  decoder.pushFrame(makeFrame(tag++));       // tag 98: the damaged sync0
  decoder.pushFrame(makeSync1Frame(tag++));  // tag 99: triggers the repair
  for (int i = 0; i < 96; ++i) decoder.pushFrame(makeFrame(tag++));

  // Section C boundary carves section B; a few extra frames let the state
  // machine emit its output.
  decoder.pushFrame(makeSync0Frame(tag++));
  decoder.pushFrame(makeSync1Frame(tag++));
  for (int i = 0; i < 4; ++i) decoder.pushFrame(makeFrame(tag++));

  ASSERT_TRUE(decoder.isReady());
  expectSectionTags(decoder.popSection(), 0);

  // The repaired section must contain all 98 frames, including the sync1
  // frame (tag 99), with nothing dropped or padded.
  ASSERT_TRUE(decoder.isReady());
  expectSectionTags(decoder.popSection(), 98);
}

TEST(F3FrameToF2Section, PreservesFrameOrderWhenIgnoringUndershootSync0) {
  F3FrameToF2Section decoder;
  uint8_t tag = 0;

  decoder.pushFrame(makeSync0Frame(tag++));
  decoder.pushFrame(makeSync1Frame(tag++));
  for (int i = 0; i < 40; ++i) decoder.pushFrame(makeFrame(tag++));

  // A corrupted subcode symbol aliases to sync0 in mid-section (tag 42). The
  // resulting 42-frame undershoot section must be merged back without
  // disturbing the stream order.
  decoder.pushFrame(makeSync0Frame(tag++));
  for (int i = 0; i < 55; ++i) decoder.pushFrame(makeFrame(tag++));

  // The true boundary arrives on the 98-frame grid.
  decoder.pushFrame(makeSync0Frame(tag++));  // tag 98
  decoder.pushFrame(makeSync1Frame(tag++));
  for (int i = 0; i < 4; ++i) decoder.pushFrame(makeFrame(tag++));

  ASSERT_TRUE(decoder.isReady());
  expectSectionTags(decoder.popSection(), 0);
}

}  // namespace
