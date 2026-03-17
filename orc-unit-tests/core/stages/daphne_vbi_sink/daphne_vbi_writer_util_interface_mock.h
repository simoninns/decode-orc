/*
* File:        daphne_vbi_writer_util_interface_mock.h
 * Module:      orc-core-tests
 * Purpose:     Mock to support unit tests
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Matt Ownby
 */

#ifndef DECODE_ORC_ROOT_DAPHNE_VBI_WRITER_UTIL_INTERFACE_MOCK_H
#define DECODE_ORC_ROOT_DAPHNE_VBI_WRITER_UTIL_INTERFACE_MOCK_H

#include "daphne_vbi_writer_util_interface.h"
#include <gmock/gmock.h>

using orc::FieldID;
using orc::IFileWriter;
using orc::IObservationContext;

// using different namespace from module-under-test so that we can use the same class names in the tests as in the module-under-test
namespace orc_unit_test
{
    /**
     * See https://google.github.io/googletest/gmock_cook_book.html
     */
    class MockDaphneVBIWriterUtil : public orc::IDaphneVBIWriterUtil {
    public:
        // virtual void write_header() const = 0;
        MOCK_METHOD(void, write_header, (), (override, const));

        // virtual void write_observations(FieldID field_id, const IObservationContext *pContext) const = 0;
        MOCK_METHOD(void, write_observations, (FieldID, const IObservationContext &), (override, const));
    };
}

#endif //DECODE_ORC_ROOT_DAPHNE_VBI_WRITER_UTIL_INTERFACE_MOCK_H
