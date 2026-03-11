/*
* File:        factories_interface_mock.h
 * Module:      orc-core-tests
 * Purpose:     Mock to support unit tests
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Matt Ownby
 */

#ifndef DECODE_ORC_ROOT_FACTORIES_INTERFACE_MOCK_H
#define DECODE_ORC_ROOT_FACTORIES_INTERFACE_MOCK_H

#include "../../orc/core/factories_interface.h"
#include <gmock/gmock.h>

// using different namespace from module-under-test so that we can use the same class names in the tests as in the module-under-test
namespace orc_unit_test
{
    class MockFactories : public orc::IFactories
    {
    public:
        MOCK_METHOD(orc::IStageFactories*, get_instance_stage_factories, (), (override));
        MOCK_METHOD(std::shared_ptr<orc::IFileWriter<uint8_t>>, create_instance_buffered_file_writer_uint8, (size_t), (override));
        MOCK_METHOD(std::shared_ptr<orc::IFileWriter<uint16_t>>, create_instance_buffered_file_writer_uint16, (size_t), (override));
    };
}

#endif //DECODE_ORC_ROOT_FACTORIES_INTERFACE_MOCK_H