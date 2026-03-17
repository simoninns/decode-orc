/*
* File:        daphne_vbi_writer_util_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit test(s) for VBI Writer Util
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Matt Ownby
 */

#include "../../include/file_io_interface_mock.h"
#include "../../include/observation_context_interface_mock.h"
#include <gtest/gtest.h>
#include "daphne_vbi_writer_util.h"
#include "field_id.h"

using testing::Return;
using testing::Mock;
using testing::NotNull;
using testing::Invoke;

// using different namespace from module-under-test so that we can use the same class names in the tests as in the module-under-test
namespace orc_unit_test
{
	// test fixture for DaphneVBIWriterUtil suite of tests
	class DaphneVBIWriterUtil : public ::testing::Test
	{
	public:
		void SetUp() override
		{
			instance_ = std::make_unique<orc::DaphneVBIWriterUtil>();
			instance_->init(&mockFileWriterUint8_);
		}

		void TearDown() override
		{
			instance_.reset();
		}

	protected:

		MockFileWriter<uint8_t> mockFileWriterUint8_;
		MockObservationContext mockObservationContext_;
		std::unique_ptr<orc::DaphneVBIWriterUtil> instance_;
	};

	////////////////////////////////////////////////////////////////////////////////////////////

	TEST_F(DaphneVBIWriterUtil, write_header)
	{
		const std::vector<uint8_t> expected = { '1', 'V', 'B', 'I' };

		EXPECT_CALL(mockFileWriterUint8_, write(expected))
			.Times(1);

		instance_->write_header();
	}

	TEST_F(DaphneVBIWriterUtil, write_observations_with_white_flag_and_vbi_data)
	{
		constexpr FieldID field_id(1);

		// Set up expectations for observation context
		EXPECT_CALL(mockObservationContext_, get(field_id, "white_flag", "present"))
			.WillOnce(Return(std::optional<ObservationValue>(true)));

		EXPECT_CALL(mockObservationContext_, get(field_id, "biphase", "vbi_line_16"))
			.WillOnce(Return(std::optional<ObservationValue>(0x123456)));

		EXPECT_CALL(mockObservationContext_, get(field_id, "biphase", "vbi_line_17"))
			.WillOnce(Return(std::optional<ObservationValue>(0x789ABC)));

		EXPECT_CALL(mockObservationContext_, get(field_id, "biphase", "vbi_line_18"))
			.WillOnce(Return(std::optional<ObservationValue>(0xDEF012)));

		// Expected buffer: white_flag=1, then big-endian VBI data
		const std::vector<uint8_t> expected = {
			1,                          // white flag present
			0x12, 0x34, 0x56,          // vbi_line_16 big-endian
			0x78, 0x9A, 0xBC,          // vbi_line_17 big-endian
			0xDE, 0xF0, 0x12           // vbi_line_18 big-endian
		};

		EXPECT_CALL(mockFileWriterUint8_, write(NotNull(), 10))
				.Times(1)
				.WillOnce(Invoke([&](const uint8_t* data, const size_t count) {
					// Convert the raw pointer to a vector for easy comparison (this also tests 'count')
					const std::vector actual(data, data + count);

					EXPECT_EQ(actual, expected);
				}));

		instance_->write_observations(field_id, mockObservationContext_);
	}

	TEST_F(DaphneVBIWriterUtil, write_observations_with_no_whiteflag_and_parse_errors)
	{
		constexpr FieldID field_id(0);

		// Set up expectations for observation context
		EXPECT_CALL(mockObservationContext_, get(field_id, "white_flag", "present"))
			.WillOnce(Return(std::optional<ObservationValue>(false)));

		EXPECT_CALL(mockObservationContext_, get(field_id, "biphase", "vbi_line_16"))
			.WillOnce(Return(std::optional<ObservationValue>(-1)));

		EXPECT_CALL(mockObservationContext_, get(field_id, "biphase", "vbi_line_17"))
			.WillOnce(Return(std::optional<ObservationValue>(-1)));

		EXPECT_CALL(mockObservationContext_, get(field_id, "biphase", "vbi_line_18"))
			.WillOnce(Return(std::optional<ObservationValue>(-1)));

		// Expected buffer: white_flag=0, then big-endian VBI data
		const std::vector<uint8_t> expected = {
			0,                          // white flag present
			0x7F, 0xFF, 0xFF,          // vbi_line_16 big-endian
			0x7F, 0xFF, 0xFF,          // vbi_line_17 big-endian
			0x7F, 0xFF, 0xFF           // vbi_line_18 big-endian
		};

		EXPECT_CALL(mockFileWriterUint8_, write(NotNull(), 10))
				.Times(1)
				.WillOnce(Invoke([&](const uint8_t* data, const size_t count) {
					// Convert the raw pointer to a vector for easy comparison (this also tests 'count')
					const std::vector actual(data, data + count);

					EXPECT_EQ(actual, expected);
				}));

		instance_->write_observations(field_id, mockObservationContext_);
	}

}