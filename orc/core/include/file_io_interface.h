/*
* File:        file_io_interface.h
 * Module:      orc-core
 * Purpose:     Interface(s) for file I/O to make unit testing easier
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef DECODE_ORC_ROOT_FILE_IO_INTERFACE_H
#define DECODE_ORC_ROOT_FILE_IO_INTERFACE_H

#include <fstream>
#include <vector>
#include <string>
#include <cstdint>

namespace orc
{
    /**
     * @brief Interface for file writer
     *
     * Purpose: To make mocking within unit-tests easier
     *
     * Template parameter T is the data type being written (e.g., uint16_t, int16_t, uint8_t)
     */
    template<typename T>
    class IFileWriter
    {
    public:
        virtual ~IFileWriter() = default;

        /**
         * @brief Open a file for buffered writing
         * @param filepath Path to output file
         * @param mode Open mode flags (default: binary | trunc)
         * @return true if opened successfully
         */
        virtual bool open(const std::string& filepath, std::ios::openmode mode = std::ios::binary | std::ios::trunc) = 0;

        /**
         * @brief Write data to buffer (will auto-flush when buffer is full)
         * @param data Pointer to data
         * @param count Number of elements to write
         */
        virtual void write(const T* data, size_t count) = 0;

        /**
         * @brief Write a vector of data
         * @param data Vector of data to write
         */
        virtual void write(const std::vector<T>& data) = 0;

        /**
         * @brief Flush buffered data to disk
         */
        virtual void flush() = 0;

        /**
         * @brief Close the file (automatically flushes)
         */
        virtual void close() = 0;

        /**
         * @brief Get total bytes written to file
         */
        virtual uint64_t bytes_written() const = 0;

        /**
         * @brief Check if file is open
         */
        virtual bool is_open() const = 0;

        /**
         * @brief Get the filepath
         */
        virtual const std::string& filepath() const = 0;
    };
}

#endif //DECODE_ORC_ROOT_FILE_IO_INTERFACE_H