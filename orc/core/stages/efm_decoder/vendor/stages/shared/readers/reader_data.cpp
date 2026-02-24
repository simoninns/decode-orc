/*
 * File:        reader_data.cpp
 * Module:      efm-decoder-f2
 * Purpose:     EFM T-values to F2 Section decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "reader_data.h"
#include "core/logging.h"
#include <iostream>

ReaderData::ReaderData() : m_usingStdin(false), m_fileSize(0) { }

ReaderData::~ReaderData()
{
    close();
}

bool ReaderData::open(const std::string &filename)
{
    if (filename == "-") {
        // Use stdin
        m_usingStdin = true;
        LOG_DEBUG("ReaderData::open() - Opened stdin for data reading");
        return true;
    } else {
        // Use regular file
        m_usingStdin = false;
        m_file.open(filename, std::ios::binary);
        if (!m_file.is_open()) {
            LOG_ERROR("ReaderData::open() - Could not open file {} for reading", filename);
            return false;
        }
        
        // Get file size
        m_file.seekg(0, std::ios::end);
        m_fileSize = m_file.tellg();
        m_file.seekg(0, std::ios::beg);
        
        LOG_DEBUG("ReaderData::open() - Opened file {} for data reading with size {} bytes", filename, m_fileSize);
        return true;
    }
}

std::vector<uint8_t> ReaderData::read(uint32_t chunkSize)
{
    std::vector<uint8_t> data;
    
    if (m_usingStdin) {
        // Read from stdin
        std::vector<char> buffer(chunkSize);
        std::cin.read(buffer.data(), chunkSize);
        std::streamsize bytesRead = std::cin.gcount();
        if (bytesRead > 0) {
            data.insert(data.end(), buffer.begin(), buffer.begin() + bytesRead);
        }
    } else {
        // Read from file
        if (!m_file.is_open()) {
            LOG_ERROR("ReaderData::read() - File is not open for reading");
            return data;
        }
        std::vector<char> buffer(chunkSize);
        m_file.read(buffer.data(), chunkSize);
        std::streamsize bytesRead = m_file.gcount();
        if (bytesRead > 0) {
            data.insert(data.end(), buffer.begin(), buffer.begin() + bytesRead);
        }
    }
    
    return data;
}

void ReaderData::close()
{
    if (m_usingStdin) {
        LOG_DEBUG("ReaderData::close(): Closed stdin");
    } else {
        if (m_file.is_open()) {
            LOG_DEBUG("ReaderData::close(): Closed the data file");
            m_file.close();
        }
    }
    m_usingStdin = false;
    m_fileSize = 0;
}

int64_t ReaderData::size() const
{
    if (m_usingStdin) {
        // Cannot determine size of stdin
        return -1;
    }
    return m_fileSize;
}

bool ReaderData::isStdin() const
{
    return m_usingStdin;
}

