/*
* File:        video_field_representation_mock.h
 * Module:      orc-core-tests
 * Purpose:     Mock to support unit tests
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Matt Ownby
 */

#ifndef DECODE_ORC_ROOT_VIDEO_FIELD_REPRESENTATION_MOCK_H
#define DECODE_ORC_ROOT_VIDEO_FIELD_REPRESENTATION_MOCK_H

#include "video_field_representation.h"
#include <gmock/gmock.h>

// using different namespace from module-under-test so that we can use the same class names in the tests as in the module-under-test
namespace orc_unit_test
{
    class MockVideoFieldRepresentation : public orc::VideoFieldRepresentation
    {
    public:
        MockVideoFieldRepresentation() : VideoFieldRepresentation(orc::ArtifactID("test_artifact"), orc::Provenance{}) {}

        MOCK_METHOD(std::string, type_name, (), (const, override));
        MOCK_METHOD(orc::FieldIDRange, field_range, (), (const, override));
        MOCK_METHOD(size_t, field_count, (), (const, override));
        MOCK_METHOD(bool, has_field, (orc::FieldID), (const, override));
        MOCK_METHOD(std::optional<orc::FieldDescriptor>, get_descriptor, (orc::FieldID), (const, override));
        MOCK_METHOD(const sample_type*, get_line, (orc::FieldID, size_t), (const, override));
        MOCK_METHOD((std::vector<sample_type>), get_field, (orc::FieldID), (const, override));
        MOCK_METHOD((std::optional<orc::SourceParameters>), get_video_parameters, (), (const, override));
        MOCK_METHOD(bool, has_audio, (), (const, override));
        MOCK_METHOD(uint32_t, get_audio_sample_count, (orc::FieldID), (const, override));
        MOCK_METHOD((std::vector<int16_t>), get_audio_samples, (orc::FieldID), (const, override));
        MOCK_METHOD(bool, has_efm, (), (const, override));
        MOCK_METHOD(uint32_t, get_efm_sample_count, (orc::FieldID), (const, override));
        MOCK_METHOD((std::vector<uint8_t>), get_efm_samples, (orc::FieldID), (const, override));
        MOCK_METHOD(bool, has_ac3_rf, (), (const, override));
        MOCK_METHOD(uint32_t, get_ac3_symbol_count, (orc::FieldID), (const, override));
        MOCK_METHOD((std::vector<uint8_t>), get_ac3_symbols, (orc::FieldID), (const, override));
        MOCK_METHOD((std::vector<orc::DropoutRegion>), get_dropout_hints, (orc::FieldID), (const, override));
        MOCK_METHOD((std::optional<orc::ActiveLineHint>), get_active_line_hint, (), (const, override));
    };
}

#endif //DECODE_ORC_ROOT_VIDEO_FIELD_REPRESENTATION_MOCK_H