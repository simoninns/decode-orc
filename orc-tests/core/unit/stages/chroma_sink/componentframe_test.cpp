/*
 * File:        componentframe_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit test(s) for ComponentFrame
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "../../../../orc/plugins/stages/sinks/common/decoders/componentframe.h"

#include <gtest/gtest.h>

namespace orc_unit_test {
// Test fixture for ComponentFrame unit tests
class ComponentFrameTest : public ::testing::Test {
 public:
  void SetUp() override {
    sourceParameters_.field_width = 4;
    sourceParameters_.field_height = 3;

    uvFrame_.init(sourceParameters_, false);
    yFrame_.init(sourceParameters_, false);

    const int32_t frameSize = sourceParameters_.field_width *
                              ((sourceParameters_.field_height * 2) - 1);

    originalY_ = std::vector<double>(frameSize, 1.0);
    replacementY_ = std::vector<double>(frameSize, 2.0);
    originalU_ = std::vector<double>(frameSize, 3.0);
    originalV_ = std::vector<double>(frameSize, 4.0);

    uvFrame_.setY(originalY_);
    uvFrame_.setU(originalU_);
    uvFrame_.setV(originalV_);
    yFrame_.setY(replacementY_);
  }

  void TearDown() override {
    originalY_.clear();
    replacementY_.clear();
    originalU_.clear();
    originalV_.clear();
  }

 protected:
  orc::SourceParameters sourceParameters_;
  ComponentFrame uvFrame_;
  ComponentFrame yFrame_;

  std::vector<double> originalY_;
  std::vector<double> replacementY_;
  std::vector<double> originalU_;
  std::vector<double> originalV_;
};

TEST_F(ComponentFrameTest, mergeLumaFromReplacesOnlyYPlane) {
  uvFrame_.merge_luma_from(yFrame_);

  ASSERT_EQ(uvFrame_.getY()->size(), replacementY_.size());
  ASSERT_EQ(uvFrame_.getU()->size(), originalU_.size());
  ASSERT_EQ(uvFrame_.getV()->size(), originalV_.size());

  EXPECT_EQ(*uvFrame_.getY(), replacementY_);
  EXPECT_EQ(*uvFrame_.getU(), originalU_);
  EXPECT_EQ(*uvFrame_.getV(), originalV_);
}

TEST_F(ComponentFrameTest, mergeLumaFromIgnoresSourceUAndVPlanes) {
  std::vector<double> sourceU(originalU_.size(), 9.0);
  std::vector<double> sourceV(originalV_.size(), 8.0);

  yFrame_.setU(sourceU);
  yFrame_.setV(sourceV);

  uvFrame_.merge_luma_from(yFrame_);

  EXPECT_EQ(*uvFrame_.getY(), replacementY_);
  EXPECT_EQ(*uvFrame_.getU(), originalU_);
  EXPECT_EQ(*uvFrame_.getV(), originalV_);
}

TEST_F(ComponentFrameTest, mergeLumaFromCopiesLumaDataByValue) {
  uvFrame_.merge_luma_from(yFrame_);

  ASSERT_FALSE(yFrame_.getY()->empty());
  (*yFrame_.getY())[0] = 1234.0;

  EXPECT_EQ((*uvFrame_.getY())[0], 2.0);
  EXPECT_EQ(*uvFrame_.getU(), originalU_);
  EXPECT_EQ(*uvFrame_.getV(), originalV_);
}
}  // namespace orc_unit_test
