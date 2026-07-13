/*
 * File:        audio_sample_feed_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for the video sink's carrier-to-encoder sample
 *              conversions and gain application
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "../../../../orc/plugins/stages/sinks/common/audio_sample_feed.h"

#include <gtest/gtest.h>

namespace orc_unit_test {

TEST(AudioSampleFeedTest, CarrierToFloat_MapsFullScale24BitToUnity) {
  // AAC FLTP/FLT feed: ±full scale maps to ±1.0 (−8388608 exactly, 8388607
  // one LSB short of +1.0).
  EXPECT_FLOAT_EQ(orc::audio_carrier_to_float(0), 0.0f);
  EXPECT_FLOAT_EQ(orc::audio_carrier_to_float(-8388608), -1.0f);
  EXPECT_FLOAT_EQ(orc::audio_carrier_to_float(8388607),
                  8388607.0f / 8388608.0f);
  EXPECT_FLOAT_EQ(orc::audio_carrier_to_float(4194304), 0.5f);
}

TEST(AudioSampleFeedTest, CarrierToS32_ShiftsIntoTop3Bytes) {
  // FLAC 24-bit / PCM_S24LE feed: FFmpeg expects the 24-bit value in the
  // top 3 bytes of S32.
  EXPECT_EQ(orc::audio_carrier_to_s32(0), 0);
  EXPECT_EQ(orc::audio_carrier_to_s32(1), 256);
  EXPECT_EQ(orc::audio_carrier_to_s32(-1), -256);
  EXPECT_EQ(orc::audio_carrier_to_s32(8388607), 2147483392);
  EXPECT_EQ(orc::audio_carrier_to_s32(-8388608), -2147483648);
}

TEST(AudioSampleFeedTest, CarrierToS16_DropsLow8Bits) {
  // Exact inverse of the << 8 ingest widening for 16-bit source material.
  EXPECT_EQ(orc::audio_carrier_to_s16(256), 1);
  EXPECT_EQ(orc::audio_carrier_to_s16(-256), -1);
  EXPECT_EQ(orc::audio_carrier_to_s16(8388607), 32767);
  EXPECT_EQ(orc::audio_carrier_to_s16(-8388608), -32768);
}

TEST(AudioSampleFeedTest, ApplyGain_ScalesInCarrierDomain) {
  EXPECT_EQ(orc::audio_apply_gain(1000, 2.0), 2000);
  EXPECT_EQ(orc::audio_apply_gain(-1000, 0.5), -500);
  // Silence stays silence under any gain.
  EXPECT_EQ(orc::audio_apply_gain(0, 15.848931924611133), 0);
}

TEST(AudioSampleFeedTest, ApplyGain_SaturatesAt24BitFullScale) {
  EXPECT_EQ(orc::audio_apply_gain(8000000, 2.0), 8388607);
  EXPECT_EQ(orc::audio_apply_gain(-8000000, 2.0), -8388608);
  EXPECT_EQ(orc::audio_apply_gain(8388607, 1000.0), 8388607);
}

}  // namespace orc_unit_test
