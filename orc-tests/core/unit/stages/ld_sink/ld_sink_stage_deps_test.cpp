/*
 * File:        ld_sink_stage_deps_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for LDSinkStageDeps
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "ld_sink_stage_deps.h"

#include <gtest/gtest.h>

#include "../../include/file_io_interface_mock.h"
#include "../../include/observation_context_interface_mock.h"
#include "../../include/video_field_representation_mock.h"
#include "../../stage_services_mock.h"
#include "tbc_metadata_writer_interface_mock.h"

using testing::_;  // NOLINT(bugprone-reserved-identifier)
using testing::Ref;
using testing::Return;
using testing::StrictMock;

namespace orc_unit_test {
// test fixture for LDSinkStageDeps suite of tests
class LDSinkStageDepsTest : public ::testing::Test {
 public:
  void SetUp() override {
    pMockFileWriterUint16_ =
        std::make_shared<StrictMock<MockFileWriterUint16>>();
    pMockMetadataWriter_ =
        std::make_shared<StrictMock<MockTBCMetadataWriter>>();

    instance_ = std::make_unique<orc::LDSinkStageDeps>(&mockStageServices_,
                                                       pMockMetadataWriter_);
    instance_->init({}, &isProcessing_, &cancelRequested_);

    cancelRequested_.store(false);
    isProcessing_.store(true);
  }

  void TearDown() override { instance_.reset(); }

 protected:
  MockStageServices mockStageServices_;
  std::shared_ptr<StrictMock<MockFileWriterUint16>> pMockFileWriterUint16_;
  std::shared_ptr<StrictMock<MockTBCMetadataWriter>> pMockMetadataWriter_;
  MockObservationContext mockObservationContext_;

  StrictMock<MockVideoFieldRepresentation> mockRepresentation_;

  std::atomic<bool> cancelRequested_{};
  std::atomic<bool> isProcessing_{};
  std::unique_ptr<orc::LDSinkStageDeps> instance_;
};

////////////////////////////////////////////////////////////////////////////////////////////

TEST_F(LDSinkStageDepsTest,
       writeTbcAddsExtensionAndSucceedsWithEmptyRange) {
  EXPECT_CALL(mockRepresentation_, field_range())
      .Times(1)
      .WillOnce(Return(orc::FieldIDRange(orc::FieldID(0), orc::FieldID(0))));

  EXPECT_CALL(mockStageServices_,
              create_buffered_file_writer_uint16(16UL * 1024 * 1024))
      .Times(1)
      .WillOnce(Return(pMockFileWriterUint16_));

  EXPECT_CALL(*pMockFileWriterUint16_, open("out_path.tbc"))
      .Times(1)
      .WillOnce(Return(true));

  EXPECT_CALL(*pMockMetadataWriter_, open("out_path.tbc.db"))
      .Times(1)
      .WillOnce(Return(true));

  orc::SourceParameters video_params;
  video_params.system = orc::VideoSystem::PAL;
  EXPECT_CALL(mockRepresentation_, get_video_parameters())
      .Times(1)
      .WillOnce(Return(video_params));

  EXPECT_CALL(*pMockMetadataWriter_, write_video_parameters(_))
      .Times(1)
      .WillOnce(Return(true));

  EXPECT_CALL(*pMockMetadataWriter_, begin_transaction())
      .Times(1)
      .WillOnce(Return(true));

  EXPECT_CALL(*pMockMetadataWriter_, commit_transaction())
      .Times(1)
      .WillOnce(Return(true));

  EXPECT_CALL(*pMockMetadataWriter_, close()).Times(1);

  EXPECT_CALL(*pMockFileWriterUint16_, close()).Times(1);

  const bool result = instance_->write_tbc_and_metadata(
      &mockRepresentation_, "out_path", mockObservationContext_);

  EXPECT_TRUE(result);
}

TEST_F(LDSinkStageDepsTest, writeTbcReturnsFalseWhenTbcFileOpenFails) {
  EXPECT_CALL(mockRepresentation_, field_range())
      .Times(1)
      .WillOnce(Return(orc::FieldIDRange(orc::FieldID(0), orc::FieldID(0))));

  EXPECT_CALL(mockStageServices_,
              create_buffered_file_writer_uint16(16UL * 1024 * 1024))
      .Times(1)
      .WillOnce(Return(pMockFileWriterUint16_));

  EXPECT_CALL(*pMockFileWriterUint16_, open("out_path.tbc"))
      .Times(1)
      .WillOnce(Return(false));

  const bool result = instance_->write_tbc_and_metadata(
      &mockRepresentation_, "out_path", mockObservationContext_);

  EXPECT_FALSE(result);
}

TEST_F(LDSinkStageDepsTest,
       writeTbcReturnsFalseWhenMetadataDbOpenFails) {
  EXPECT_CALL(mockRepresentation_, field_range())
      .Times(1)
      .WillOnce(Return(orc::FieldIDRange(orc::FieldID(0), orc::FieldID(0))));

  EXPECT_CALL(mockStageServices_,
              create_buffered_file_writer_uint16(16UL * 1024 * 1024))
      .Times(1)
      .WillOnce(Return(pMockFileWriterUint16_));

  EXPECT_CALL(*pMockFileWriterUint16_, open("out_path.tbc"))
      .Times(1)
      .WillOnce(Return(true));

  EXPECT_CALL(*pMockMetadataWriter_, open("out_path.tbc.db"))
      .Times(1)
      .WillOnce(Return(false));

  EXPECT_CALL(*pMockFileWriterUint16_, close()).Times(1);

  const bool result = instance_->write_tbc_and_metadata(
      &mockRepresentation_, "out_path", mockObservationContext_);

  EXPECT_FALSE(result);
}

TEST_F(LDSinkStageDepsTest,
       writeTbcReturnsFalseWhenGetVideoParametersReturnsNullopt) {
  EXPECT_CALL(mockRepresentation_, field_range())
      .Times(1)
      .WillOnce(Return(orc::FieldIDRange(orc::FieldID(0), orc::FieldID(0))));

  EXPECT_CALL(mockStageServices_,
              create_buffered_file_writer_uint16(16UL * 1024 * 1024))
      .Times(1)
      .WillOnce(Return(pMockFileWriterUint16_));

  EXPECT_CALL(*pMockFileWriterUint16_, open("out_path.tbc"))
      .Times(1)
      .WillOnce(Return(true));

  EXPECT_CALL(*pMockMetadataWriter_, open("out_path.tbc.db"))
      .Times(1)
      .WillOnce(Return(true));

  EXPECT_CALL(mockRepresentation_, get_video_parameters())
      .Times(1)
      .WillOnce(Return(std::nullopt));

  EXPECT_CALL(*pMockMetadataWriter_, close()).Times(1);
  EXPECT_CALL(*pMockFileWriterUint16_, close()).Times(1);

  const bool result = instance_->write_tbc_and_metadata(
      &mockRepresentation_, "out_path", mockObservationContext_);

  EXPECT_FALSE(result);
}

TEST_F(LDSinkStageDepsTest,
       writeTbcReturnsFalseWhenWriteVideoParametersFails) {
  EXPECT_CALL(mockRepresentation_, field_range())
      .Times(1)
      .WillOnce(Return(orc::FieldIDRange(orc::FieldID(0), orc::FieldID(0))));

  EXPECT_CALL(mockStageServices_,
              create_buffered_file_writer_uint16(16UL * 1024 * 1024))
      .Times(1)
      .WillOnce(Return(pMockFileWriterUint16_));

  EXPECT_CALL(*pMockFileWriterUint16_, open("out_path.tbc"))
      .Times(1)
      .WillOnce(Return(true));

  EXPECT_CALL(*pMockMetadataWriter_, open("out_path.tbc.db"))
      .Times(1)
      .WillOnce(Return(true));

  orc::SourceParameters video_params;
  video_params.system = orc::VideoSystem::PAL;
  EXPECT_CALL(mockRepresentation_, get_video_parameters())
      .Times(1)
      .WillOnce(Return(video_params));

  EXPECT_CALL(*pMockMetadataWriter_, write_video_parameters(_))
      .Times(1)
      .WillOnce(Return(false));

  EXPECT_CALL(*pMockMetadataWriter_, close()).Times(1);
  EXPECT_CALL(*pMockFileWriterUint16_, close()).Times(1);

  const bool result = instance_->write_tbc_and_metadata(
      &mockRepresentation_, "out_path", mockObservationContext_);

  EXPECT_FALSE(result);
}

TEST_F(LDSinkStageDepsTest,
       writeTbcClosesFilesAndMarksProcessingFalseWhenCancelled) {
  EXPECT_CALL(mockRepresentation_, field_range())
      .Times(1)
      .WillOnce(Return(orc::FieldIDRange(orc::FieldID(0), orc::FieldID(1))));

  EXPECT_CALL(mockRepresentation_, has_field(orc::FieldID(0)))
      .Times(1)
      .WillOnce(Return(true));

  EXPECT_CALL(mockStageServices_,
              create_buffered_file_writer_uint16(16UL * 1024 * 1024))
      .Times(1)
      .WillOnce(Return(pMockFileWriterUint16_));

  EXPECT_CALL(*pMockFileWriterUint16_, open("cancel_path.tbc"))
      .Times(1)
      .WillOnce(Return(true));

  EXPECT_CALL(*pMockMetadataWriter_, open("cancel_path.tbc.db"))
      .Times(1)
      .WillOnce(Return(true));

  orc::SourceParameters video_params;
  video_params.system = orc::VideoSystem::PAL;
  EXPECT_CALL(mockRepresentation_, get_video_parameters())
      .Times(1)
      .WillOnce(Return(video_params));

  EXPECT_CALL(*pMockMetadataWriter_, write_video_parameters(_))
      .Times(1)
      .WillOnce(Return(true));

  EXPECT_CALL(*pMockMetadataWriter_, begin_transaction())
      .Times(1)
      .WillOnce(Return(true));

  // On cancel: commit + close metadata, close tbc
  EXPECT_CALL(*pMockMetadataWriter_, commit_transaction())
      .Times(1)
      .WillOnce(Return(true));
  EXPECT_CALL(*pMockMetadataWriter_, close()).Times(1);
  EXPECT_CALL(*pMockFileWriterUint16_, close()).Times(1);

  cancelRequested_.store(true);

  const bool result = instance_->write_tbc_and_metadata(
      &mockRepresentation_, "cancel_path", mockObservationContext_);

  EXPECT_FALSE(result);
  EXPECT_FALSE(isProcessing_.load());
}
}  // namespace orc_unit_test
