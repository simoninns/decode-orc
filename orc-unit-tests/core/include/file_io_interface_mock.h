/*
* File:        file_io_interface_mock.h
 * Module:      orc-core-tests
 * Purpose:     Mock to support unit tests
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Matt Ownby
 */

#ifndef DECODE_ORC_ROOT_FILE_IO_INTERFACE_MOCK_H
#define DECODE_ORC_ROOT_FILE_IO_INTERFACE_MOCK_H

#include "file_io_interface.h"
#include <gmock/gmock.h>

// using different namespace from module-under-test so that we can use the same class names in the tests as in the module-under-test
namespace orc_unit_test
{
    /**
     * See https://google.github.io/googletest/gmock_cook_book.html
     */
    template<typename T>
    class MockFileWriter : public orc::IFileWriter<T> {
    public:
        //virtual bool open(const std::string& filepath, std::ios::openmode mode = std::ios::binary | std::ios::trunc) = 0;
        MOCK_METHOD(bool, open, (const std::string&, std::ios::openmode), (override));

        //virtual void write(const T* data, size_t count) = 0;
        MOCK_METHOD(void, write, (const T *, size_t), (override));

        //virtual void write(const std::vector<T>& data) = 0;
        MOCK_METHOD(void, write, (const std::vector<T> &), (override));

        //virtual void flush() = 0;
        MOCK_METHOD(void, flush, (), (override));

        //virtual void close() = 0;
        MOCK_METHOD(void, close, (), (override));

        //virtual uint64_t bytes_written() const = 0;
        MOCK_METHOD(uint64_t, bytes_written, (), (override, const));

        //virtual bool is_open() const = 0;
        MOCK_METHOD(bool, is_open, (), (override, const));

        //virtual const std::string& filepath() const = 0;
        MOCK_METHOD(const std::string &, filepath, (), (override, const));
    };
}

#endif //DECODE_ORC_ROOT_FILE_IO_INTERFACE_MOCK_H