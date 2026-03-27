/*
* File:        stage_factories_interface_mock.h
 * Module:      orc-core-tests
 * Purpose:     Mock to support unit tests
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Matt Ownby
 */

#ifndef DECODE_ORC_ROOT_STAGE_FACTORIES_INTERFACE_MOCK_H
#define DECODE_ORC_ROOT_STAGE_FACTORIES_INTERFACE_MOCK_H

#include "../../../orc/core/stages/stage_factories_interface.h"
#include <gmock/gmock.h>

// using different namespace from module-under-test so that we can use the same class names in the tests as in the module-under-test
namespace orc_unit_test
{
    class MockStageFactories : public orc::IStageFactories
    {
    public:
        MOCK_METHOD(std::shared_ptr<orc::IDaphneVBISinkStageDeps>, CreateInstanceDaphneVBISinkStageDeps,
            (orc::TriggerProgressCallback &, std::atomic<bool> &, std::atomic<bool> &), (override));

    	// virtual std::shared_ptr<IDaphneVBIWriterUtil> CreateInstanceDaphneVBIWriterUtil(IFileWriter<uint8_t> &writer) = 0;
    	MOCK_METHOD(std::shared_ptr<orc::IDaphneVBIWriterUtil>, CreateInstanceDaphneVBIWriterUtil,
    		(orc::IFileWriter<uint8_t> &), (override));

        MOCK_METHOD(std::shared_ptr<orc::ILDSinkStageDeps>, CreateInstanceLDSinkStageDeps,
            (orc::TriggerProgressCallback &, std::atomic<bool> &, std::atomic<bool> &), (override));
    };
}

#endif //DECODE_ORC_ROOT_STAGE_FACTORIES_INTERFACE_MOCK_H