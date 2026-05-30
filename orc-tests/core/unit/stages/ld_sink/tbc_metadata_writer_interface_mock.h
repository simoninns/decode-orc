/*
 * File:        tbc_metadata_writer_interface_mock.h
 * Module:      orc-core-tests
 * Purpose:     Mock to support unit tests
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#ifndef ORC_CORE_TESTS_TBC_METADATA_WRITER_INTERFACE_MOCK_H
#define ORC_CORE_TESTS_TBC_METADATA_WRITER_INTERFACE_MOCK_H

#include "tbc_metadata_writer_interface.h"
#include <gmock/gmock.h>

using orc::FieldID;
using orc::FieldMetadata;
using orc::SourceParameters;
using orc::DropoutInfo;
using orc::IObservationContext;

// using different namespace from module-under-test so that we can use the same class names in the tests as in the module-under-test
namespace orc_unit_test
{
    /**
     * See https://google.github.io/googletest/gmock_cook_book.html
     */
    class MockTBCMetadataWriter : public orc::ITBCMetadataWriter {
    public:
        // virtual bool open(const std::string& filename) = 0;
        MOCK_METHOD(bool, open, (const std::string&), (override));

        // virtual void close() = 0;
        MOCK_METHOD(void, close, (), (override));

        // virtual bool write_video_parameters(const SourceParameters& params) = 0;
        MOCK_METHOD(bool, write_video_parameters, (const SourceParameters&), (override));

        // virtual bool write_field_metadata(const FieldMetadata& field) = 0;
        MOCK_METHOD(bool, write_field_metadata, (const FieldMetadata&), (override));

        // virtual bool write_observations(FieldID source_field_id, FieldID db_field_id, const IObservationContext& context) = 0;
        MOCK_METHOD(bool, write_observations, (FieldID, FieldID, const IObservationContext&), (override));

        // virtual bool write_dropout(FieldID field_id, const DropoutInfo& dropout) = 0;
        MOCK_METHOD(bool, write_dropout, (FieldID, const DropoutInfo&), (override));

        // virtual bool begin_transaction() = 0;
        MOCK_METHOD(bool, begin_transaction, (), (override));

        // virtual bool commit_transaction() = 0;
        MOCK_METHOD(bool, commit_transaction, (), (override));
    };
}

#endif // ORC_CORE_TESTS_TBC_METADATA_WRITER_INTERFACE_MOCK_H
