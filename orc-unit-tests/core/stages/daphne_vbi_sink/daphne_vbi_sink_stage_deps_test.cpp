/*
* File:        daphne_vbi_sink_stage_deps_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit test(s) for Daphne VBI sink stage dependencies
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Matt Ownby
 */

#include "../../include/file_io_interface_mock.h"
#include "../../include/observation_context_interface_mock.h"
#include "../../include/video_field_representation_mock.h"
#include "../../factories_interface_mock.h"
#include "../../stages/stage_factories_interface_mock.h"
#include <gtest/gtest.h>
#include "daphne_vbi_sink_stage_deps.h"
#include "daphne_vbi_writer_util_interface_mock.h"

using testing::_;
using testing::Return;
using testing::StrictMock;
using testing::Ref;

// using different namespace from module-under-test so that we can use the same class names in the tests as in the module-under-test
namespace orc_unit_test
{
    // test fixture for DaphneVBISinkStageDeps suite of tests
    class DaphneVBISinkStageDeps : public ::testing::Test
    {
    public:
        void SetUp() override
        {
            pMockFileWriterUint8_ = std::make_shared<StrictMock<MockFileWriter<uint8_t>>>();

            pMockVBIWriterUtil_ = std::make_shared<MockDaphneVBIWriterUtil>();
            pMockFileWriterUint8_ = std::make_shared<MockFileWriter<uint8_t>>();
            instance_ = std::make_unique<orc::DaphneVBISinkStageDeps>(mockFactories_, mockStageFactories_);
            instance_->init({}, &isProcessing_, &cancelRequested_);

            cancelRequested_.store(false);
            isProcessing_.store(true);
        }

        void TearDown() override
        {
            instance_.reset();
        }

    protected:
        MockFactories mockFactories_;
        MockStageFactories mockStageFactories_;
        std::shared_ptr<MockFileWriter<uint8_t>> pMockFileWriterUint8_; // must be shared_ptr because at least one mocked method will return a shared_ptr of this mock
        std::shared_ptr<MockDaphneVBIWriterUtil> pMockVBIWriterUtil_;   // must be shared_ptr because instance class requires such
        MockObservationContext mockObservationContext_;

        StrictMock<MockVideoFieldRepresentation> mockRepresentation_;

        std::atomic<bool> cancelRequested_{};
        std::atomic<bool> isProcessing_{};
        std::unique_ptr<orc::DaphneVBISinkStageDeps> instance_;
    };

    ////////////////////////////////////////////////////////////////////////////////////////////

    TEST_F(DaphneVBISinkStageDeps, write_vbi_adds_extension_and_writes_header_when_successful)
    {
        EXPECT_CALL(mockRepresentation_, field_range())
            .Times(1)
            .WillOnce(Return(orc::FieldIDRange(orc::FieldID(0), orc::FieldID(0))));

        EXPECT_CALL(mockFactories_, create_instance_buffered_file_writer_uint8(1 * 1024 * 1024))
            .Times(1)
            .WillOnce(Return(pMockFileWriterUint8_));

        EXPECT_CALL(*pMockFileWriterUint8_, open("out_path.vbi", _))
            .Times(1)
            .WillOnce(Return(true));

        EXPECT_CALL(mockStageFactories_, CreateInstanceDaphneVBIWriterUtil(Ref(*pMockFileWriterUint8_)))
            .Times(1)
            .WillOnce(Return(pMockVBIWriterUtil_));

        EXPECT_CALL(*pMockVBIWriterUtil_, write_header())
            .Times(1);
        EXPECT_CALL(*pMockFileWriterUint8_, close())
            .Times(1);

        const bool result = instance_->write_vbi(&mockRepresentation_, "out_path", mockObservationContext_);

        EXPECT_TRUE(result);
    }

    TEST_F(DaphneVBISinkStageDeps, write_vbi_returns_false_when_open_fails)
    {
        EXPECT_CALL(mockRepresentation_, field_range())
            .Times(1)
            .WillOnce(Return(orc::FieldIDRange(orc::FieldID(0), orc::FieldID(0))));

        EXPECT_CALL(mockFactories_, create_instance_buffered_file_writer_uint8(1 * 1024 * 1024))
            .Times(1)
            .WillOnce(Return(pMockFileWriterUint8_));

        EXPECT_CALL(*pMockFileWriterUint8_, open("out_path.vbi", _))
            .Times(1)
            .WillOnce(Return(false));

        EXPECT_CALL(*pMockVBIWriterUtil_, write_header()).Times(0);

        const bool result = instance_->write_vbi(&mockRepresentation_, "out_path", mockObservationContext_);

        EXPECT_FALSE(result);
    }

    TEST_F(DaphneVBISinkStageDeps, write_vbi_closes_writer_and_marks_processing_false_when_cancelled)
    {
        EXPECT_CALL(mockRepresentation_, field_range())
            .Times(1)
            .WillOnce(Return(orc::FieldIDRange(orc::FieldID(0), orc::FieldID(1))));

        EXPECT_CALL(mockRepresentation_, has_field(orc::FieldID(0)))
            .Times(1)
            .WillOnce(Return(true));

        EXPECT_CALL(mockFactories_, create_instance_buffered_file_writer_uint8(1 * 1024 * 1024))
            .Times(1)
            .WillOnce(Return(pMockFileWriterUint8_));

        EXPECT_CALL(*pMockFileWriterUint8_, open("cancel_path.vbi", _))
            .Times(1)
            .WillOnce(Return(true));

        EXPECT_CALL(mockStageFactories_, CreateInstanceDaphneVBIWriterUtil(Ref(*pMockFileWriterUint8_)))
            .Times(1)
            .WillOnce(Return(pMockVBIWriterUtil_));

        EXPECT_CALL(*pMockVBIWriterUtil_, write_header())
            .Times(1);
        EXPECT_CALL(*pMockVBIWriterUtil_, write_observations(_, _))
            .Times(0);
        EXPECT_CALL(*pMockFileWriterUint8_, close())
            .Times(1);

        cancelRequested_.store(true);

        const bool result = instance_->write_vbi(&mockRepresentation_, "cancel_path", mockObservationContext_);

        EXPECT_FALSE(result);
        EXPECT_FALSE(isProcessing_.load());
    }
}