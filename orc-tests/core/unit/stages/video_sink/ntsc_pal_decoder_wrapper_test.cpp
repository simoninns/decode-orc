/*
 * File:        ntsc_pal_decoder_wrapper_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit test(s) for NTSC/PAL decoder wrapper configuration
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>

#include "../../../../orc/plugins/stages/sinks/common/decoders/ntscdecoder.h"
#include "../../../../orc/plugins/stages/sinks/common/decoders/paldecoder.h"

namespace orc_unit_test {
namespace {
class TestableNtscDecoder : public NtscDecoder {
 public:
  explicit TestableNtscDecoder(const Comb::Configuration& config)
      : NtscDecoder(config) {}

  void decodeFrames(const std::vector<SourceField>&, int32_t, int32_t,
                    std::vector<ComponentFrame>&) override {}
};

class TestablePalDecoder : public PalDecoder {
 public:
  explicit TestablePalDecoder(const PalColour::Configuration& config)
      : PalDecoder(config) {}

  void decodeFrames(const std::vector<SourceField>&, int32_t, int32_t,
                    std::vector<ComponentFrame>&) override {}
};

orc::SourceParameters make_ntsc_params() {
  orc::SourceParameters p;
  p.system = orc::VideoSystem::NTSC;
  p.frame_width_nominal = 32;
  return p;
}

orc::SourceParameters make_pal_params() {
  orc::SourceParameters p;
  p.system = orc::VideoSystem::PAL;
  p.frame_width_nominal = 32;
  return p;
}
}  // namespace

TEST(NtscDecoderWrapperTest, Configure_AcceptsNtscAndRejectsPal) {
  Comb::Configuration config;
  TestableNtscDecoder decoder(config);

  EXPECT_TRUE(decoder.configure(make_ntsc_params()));
  EXPECT_FALSE(decoder.configure(make_pal_params()));
}

TEST(NtscDecoderWrapperTest, Look_AroundFollowsCombConfiguration) {
  Comb::Configuration config;
  config.dimensions = 3;
  TestableNtscDecoder decoder(config);

  EXPECT_EQ(decoder.getLookBehind(), 1);
  EXPECT_EQ(decoder.getLookAhead(), 1);
}

TEST(PalDecoderWrapperTest, Configure_AcceptsPalAndRejectsNtsc) {
  PalColour::Configuration config;
  TestablePalDecoder decoder(config);

  EXPECT_TRUE(decoder.configure(make_pal_params()));
  EXPECT_FALSE(decoder.configure(make_ntsc_params()));
}

TEST(PalDecoderWrapperTest, Look_AroundDependsOnPalFilterMode) {
  PalColour::Configuration config_2d;
  config_2d.chromaFilter = PalColour::transform2DFilter;
  TestablePalDecoder decoder_2d(config_2d);

  EXPECT_EQ(decoder_2d.getLookBehind(), 0);
  EXPECT_EQ(decoder_2d.getLookAhead(), 0);

  PalColour::Configuration config_3d;
  config_3d.chromaFilter = PalColour::transform3DFilter;
  TestablePalDecoder decoder_3d(config_3d);

  EXPECT_GT(decoder_3d.getLookBehind(), 0);
  EXPECT_GT(decoder_3d.getLookAhead(), 0);
}

TEST(NtscDecoderWrapperTest, Configure_RejectsInvalidGeometry) {
  Comb::Configuration config;
  TestableNtscDecoder decoder(config);

  auto params = make_ntsc_params();
  params.frame_width_nominal = 8;

  EXPECT_FALSE(decoder.configure(params));
}

TEST(PalDecoderWrapperTest, Configure_RejectsInvalidGeometry) {
  PalColour::Configuration config;
  TestablePalDecoder decoder(config);

  auto params = make_pal_params();
  params.frame_width_nominal = 8;

  EXPECT_FALSE(decoder.configure(params));
}
}  // namespace orc_unit_test
