/*
* File:        daphne_vbi_sink_stage_deps_interface_mock.h
 * Module:      orc-core-tests
 * Purpose:     Mock to support unit tests
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Matt Ownby
 */

#ifndef DECODE_ORC_ROOT_DAPHNE_VBI_SINK_STAGE_DEPS_INTERFACE_MOCK_H
#define DECODE_ORC_ROOT_DAPHNE_VBI_SINK_STAGE_DEPS_INTERFACE_MOCK_H

#include "daphne_vbi_sink_stage_deps_interface.h"
#include <gmock/gmock.h>

using orc::IObservationContext;
using orc::VideoFieldRepresentation;

// using different namespace from module-under-test so that we can use the same class names in the tests as in the module-under-test
namespace orc_unit_test
{
    /**
     * See https://google.github.io/googletest/gmock_cook_book.html
     */
    class MockDaphneVBISinkStageDeps : public orc::IDaphneVBISinkStageDeps {
    public:
        // virtual bool write_vbi(const VideoFieldRepresentation* representation, const std::string& vbi_path, IObservationContext &ObservationContext) = 0;
        MOCK_METHOD(bool, write_vbi, (const VideoFieldRepresentation*, const std::string&, IObservationContext &), (override));
    };
}

#endif //DECODE_ORC_ROOT_DAPHNE_VBI_SINK_STAGE_DEPS_INTERFACE_MOCK_H